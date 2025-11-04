

#include "Headers/ShaderDataUploader.h"

#include "Core/Headers/ByteBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
namespace Divide
{

    namespace Reflection
    {
        constexpr U16 BYTE_BUFFER_VERSION = 2u;

        bool UniformCompare::operator()( const UniformDeclaration& lhs, const UniformDeclaration& rhs ) const
        {
            //Note: this doesn't care about arrays so those won't sort properly to reduce wastage
            const auto g_TypePriority = []( const U64 typeHash ) -> I32
            {
                switch ( typeHash )
                {
                    case _ID( "dmat4" ):              //128 bytes
                    case _ID( "dmat4x3" ): return 0;  // 96 bytes
                    case _ID( "dmat3" )  : return 1;  // 72 bytes
                    case _ID( "dmat4x2" ):            // 64 bytes
                    case _ID( "mat4" )   : return 2;  // 64 bytes
                    case _ID( "dmat3x2" ):            // 48 bytes
                    case _ID( "mat4x3" ) : return 3;  // 48 bytes
                    case _ID( "mat3" )   : return 4;  // 36 bytes
                    case _ID( "dmat2" ):              // 32 bytes
                    case _ID( "dvec4" ):              // 32 bytes
                    case _ID( "mat4x2" ) : return 5;  // 32 bytes
                    case _ID( "dvec3" ):              // 24 bytes
                    case _ID( "mat3x2" ) : return 6;  // 24 bytes
                    case _ID( "mat2" ):               // 16 bytes
                    case _ID( "dvec2" ):              // 16 bytes
                    case _ID( "bvec4" ):              // 16 bytes
                    case _ID( "ivec4" ):              // 16 bytes
                    case _ID( "uvec4" ):              // 16 bytes
                    case _ID( "vec4" )   : return 7;  // 16 bytes
                    case _ID( "bvec3" ):              // 12 bytes
                    case _ID( "ivec3" ):              // 12 bytes
                    case _ID( "uvec3" ):              // 12 bytes
                    case _ID( "vec3" )   : return 8;  // 12 bytes
                    case _ID( "double" ):             //  8 bytes
                    case _ID( "bvec2" ):              //  8 bytes
                    case _ID( "ivec2" ):              //  8 bytes
                    case _ID( "uvec2" ):              //  8 bytes
                    case _ID( "vec2" )   : return 9;  //  8 bytes
                    case _ID( "int" ):                //  4 bytes
                    case _ID( "uint" ):               //  4 bytes
                    case _ID( "float" )  : return 10; //  4 bytes
                    case _ID( "bool" )   : return 11; //  4 bytes (converted to uint)
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }

                return 999;
            };

            const I32 lhsPriority = g_TypePriority( lhs._typeHash );
            const I32 rhsPriority = g_TypePriority( rhs._typeHash );
            if ( lhsPriority != rhsPriority )
            {
                return lhsPriority < rhsPriority;
            }

            return lhs._name < rhs._name;
        };

        bool SaveReflectionData( const ResourcePath& path, const ResourcePath& file, const Reflection::Data& reflectionDataIn, const eastl::set<U64>& atomIDsIn )
        {
            const auto saveDataEntry = []( ByteBuffer& buffer, const DataEntry& entry )
            {
                buffer << entry._bindingSet;
                buffer << entry._bindingSlot;
                buffer << entry._stageVisibility;
                buffer << entry._name;
            };

            const auto saveImage = [&]( ByteBuffer& buffer, const ImageEntry& entry )
            {
                saveDataEntry( buffer, entry );
                buffer << entry._combinedImageSampler;
                buffer << entry._isWriteTarget;
                buffer << entry._isMultiSampled;
                buffer << entry._isArray;
            };

            std::function<void( ByteBuffer&, const BufferMember& )> saveBufferMember;
            saveBufferMember = [&saveBufferMember]( ByteBuffer& buffer, const BufferMember& entry )
            {
                buffer << to_base( entry._type );
                buffer << entry._offset;
                buffer << entry._absoluteOffset;
                buffer << entry._size;
                buffer << entry._paddedSize;
                buffer << entry._arrayInnerSize;
                buffer << entry._arrayOuterSize;
                buffer << entry._arrayOuterSize;
                buffer << entry._vectorDimensions;
                buffer << entry._matrixDimensions;
                buffer << entry._name;
                buffer << entry._memberCount;
                for ( size_t i = 0u; i < entry._memberCount; ++i )
                {
                    saveBufferMember( buffer, entry._members[i] );
                }
            };

            const auto saveBuffer = [&]( ByteBuffer& buffer, const BufferEntry& entry )
            {
                saveDataEntry( buffer, entry );
                buffer << entry._offset;
                buffer << entry._absoluteOffset;
                buffer << entry._size;
                buffer << entry._paddedSize;
                buffer << entry._memberCount;
                buffer << entry._uniformBuffer;
                buffer << entry._dynamic;

                for ( size_t i = 0u; i < entry._memberCount; ++i )
                {
                    saveBufferMember( buffer, entry._members[i] );
                }
            };

            ByteBuffer buffer;
            buffer << BYTE_BUFFER_VERSION;
            buffer << reflectionDataIn._uniformBlockBindingIndex;
            buffer << reflectionDataIn._fragmentOutputs;
            buffer << reflectionDataIn._images.size();
            for ( const auto& image : reflectionDataIn._images )
            {
                saveImage( buffer, image );
            }
            buffer << reflectionDataIn._buffers.size();
            for ( const auto& bufferEntry : reflectionDataIn._buffers )
            {
                saveBuffer( buffer, bufferEntry );
            }
            buffer << atomIDsIn.size();
            for ( const U64 id : atomIDsIn )
            {
                buffer << id;
            }

            return buffer.dumpToFile( path, file.string() );
        }

        bool LoadReflectionData( const ResourcePath& path, const ResourcePath& file, Reflection::Data& reflectionDataOut, eastl::set<U64>& atomIDsOut )
        {
            size_t sizeTemp;

            const auto loadDataEntry = [&]( ByteBuffer& buffer, DataEntry& entry )
            {
                buffer >> entry._bindingSet;
                buffer >> entry._bindingSlot;
                buffer >> entry._stageVisibility;
                buffer >> entry._name;
            };

            const auto loadImage = [&]( ByteBuffer& buffer, ImageEntry& entry )
            {
                loadDataEntry( buffer, entry );
                buffer >> entry._combinedImageSampler;
                buffer >> entry._isWriteTarget;
                buffer >> entry._isMultiSampled;
                buffer >> entry._isArray;
            };

            std::function<void( ByteBuffer&, BufferMember& )> loadBufferMember;
            loadBufferMember = [&loadBufferMember]( ByteBuffer& buffer, BufferMember& entry )
            {
                BaseType<PushConstantType> tempType{ 0u };

                buffer >> tempType; entry._type = static_cast<PushConstantType>(tempType);
                buffer >> entry._offset;
                buffer >> entry._absoluteOffset;
                buffer >> entry._size;
                buffer >> entry._paddedSize;
                buffer >> entry._arrayInnerSize;
                buffer >> entry._arrayOuterSize;
                buffer >> entry._arrayOuterSize;
                buffer >> entry._vectorDimensions;
                buffer >> entry._matrixDimensions;
                buffer >> entry._name;
                buffer >> entry._memberCount;
                entry._members.resize( entry._memberCount );
                for ( size_t i = 0u; i < entry._memberCount; ++i )
                {
                    loadBufferMember( buffer, entry._members[i] );
                }
            };

            const auto loadBuffer = [&]( ByteBuffer& buffer, BufferEntry& entry )
            {
                loadDataEntry( buffer, entry );
                buffer >> entry._offset;
                buffer >> entry._absoluteOffset;
                buffer >> entry._size;
                buffer >> entry._paddedSize;
                buffer >> entry._memberCount;
                buffer >> entry._uniformBuffer;
                buffer >> entry._dynamic;

                entry._members.resize( entry._memberCount );
                for ( size_t i = 0u; i < entry._memberCount; ++i )
                {
                    loadBufferMember( buffer, entry._members[i] );
                }
            };

            ByteBuffer buffer;
            if ( buffer.loadFromFile( path, file.string() ) )
            {
                auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
                buffer >> tempVer;
                if ( tempVer == BYTE_BUFFER_VERSION )
                {
                    buffer >> reflectionDataOut._uniformBlockBindingIndex;
                    buffer >> reflectionDataOut._fragmentOutputs;
                    buffer >> sizeTemp;
                    reflectionDataOut._images.resize( sizeTemp );
                    for ( size_t i = 0u; i < sizeTemp; ++i )
                    {
                        loadImage( buffer, reflectionDataOut._images[i] );
                    }
                    buffer >> sizeTemp;
                    reflectionDataOut._buffers.resize( sizeTemp );
                    for ( size_t i = 0u; i < sizeTemp; ++i )
                    {
                        loadBuffer( buffer, reflectionDataOut._buffers[i] );
                    }
                    buffer >> sizeTemp;
                    U64 tempID = 0u;
                    for ( size_t i = 0u; i < sizeTemp; ++i )
                    {
                        buffer >> tempID;
                        atomIDsOut.insert( tempID );
                    }

                    return true;
                }
            }

            return false;
        }

        void PreProcessUniforms( string& sourceInOut, Reflection::UniformsSet& foundUniforms )
        {
            string ret;
            ret.reserve( sourceInOut.size());

            string line;
            istringstream input( sourceInOut );
            while ( Util::GetLine( input, line ) )
            {
                bool skip = line.length() < 6u;

                if ( !skip )
                {
                    if ( auto m = ctre::match<Paths::g_uniformPattern>(line) )
                    {
                        Reflection::UniformDeclaration declaration; 
                        declaration._type = Util::Trim( m.get<1>().str() ).c_str();
                        declaration._name = Util::Trim( m.get<2>().str() ).c_str();
                        declaration._typeHash = _ID( declaration._type.c_str() );
                        foundUniforms.emplace( declaration );
                    }
                    else
                    {
                        skip = true;
                    }
                }

                if ( skip )
                {
                    ret.append( line.c_str() );
                    ret.append( "\n" );
                }
            }

            sourceInOut = ret;
        }

        const BufferEntry* FindUniformBlock( const Data& data )
        {
            const BufferEntry* uniformBlock = nullptr;
            if (data._uniformBlockBindingIndex != Reflection::INVALID_BINDING_INDEX )
            {
                for ( const BufferEntry& block : data._buffers )
                {
                    if ( block._bindingSlot == data._uniformBlockBindingIndex &&
                         block._bindingSet == data._uniformBlockBindingSet )
                    {
                        uniformBlock = &block;
                        break;
                    }
                }
            }
            return uniformBlock;
        }

    };

    UniformBlockUploader::UniformBlockUploader( GFXDevice& context, const eastl::string& parentShaderName, const Reflection::BufferEntry& uniformBlock, const U16 shaderStageVisibilityMask )
        : _uniformBlock( uniformBlock )
        , _context( context )
        , _shaderStageVisibilityMask( shaderStageVisibilityMask )
        , _parentShaderName( parentShaderName )
    {
        const auto GetSizeOf = []( const PushConstantType type ) noexcept -> size_t
        {
            switch ( type )
            {
                case PushConstantType::INT: return sizeof( I32 );
                case PushConstantType::UINT: return sizeof( U32 );
                case PushConstantType::FLOAT: return sizeof( F32 );
                case PushConstantType::DOUBLE: return sizeof( D64 );
                default: break;
            };

            DIVIDE_UNEXPECTED_CALL_MSG( "Unexpected push constant type" );
            return 0u;
        };

        if ( uniformBlock._memberCount == 0u )
        {
            return;
        }

        _blockMembers.resize( uniformBlock._memberCount );
        _uniformBlockSizeAligned = Util::GetAlignmentCorrected( uniformBlock._size, ShaderBuffer::AlignmentRequirement( BufferUsageType::CONSTANT_BUFFER ) );
        resizeBlockBuffer( false );
        _localDataCopy.resize( _uniformBlockSizeAligned );

        size_t requiredExtraMembers = 0u;
        for ( size_t member = 0u; member < uniformBlock._memberCount; ++member )
        {
            BlockMember& bMember = _blockMembers[member];
            const Reflection::BufferMember& srcMember = _uniformBlock._members[member];
            DIVIDE_GPU_ASSERT( srcMember._memberCount == 0u, "UniformBlockUploader error: Custom structs in uniform declarations not supported!" );

            bMember._name = srcMember._name;
            bMember._nameHash = _ID( bMember._name.c_str() );
            bMember._offset = srcMember._offset;
            bMember._size = srcMember._size;
            bMember._elementSize = GetSizeOf( srcMember._type );
            bMember._arrayInnerSize = srcMember._arrayInnerSize;
            bMember._arrayOuterSize = srcMember._arrayOuterSize;
            requiredExtraMembers += (srcMember._arrayInnerSize * srcMember._arrayOuterSize);
        }

        vector<BlockMember> arrayMembers;
        arrayMembers.reserve( requiredExtraMembers );

        for ( size_t member = 0; member < uniformBlock._memberCount; ++member )
        {
            const BlockMember& bMember = _blockMembers[member];

            size_t offset = 0u;
            if ( bMember._arrayInnerSize > 0 )
            {
                for ( size_t i = 0u; i < bMember._arrayOuterSize; ++i )
                {
                    for ( size_t j = 0u; j < bMember._arrayInnerSize; ++j )
                    {
                        BlockMember newMember = bMember;
                        Util::StringFormatTo( newMember._name, "{}[{}][{}]", bMember._name.c_str(), i, j );
                        newMember._nameHash = _ID( newMember._name.c_str() );
                        newMember._size -= offset;
                        newMember._offset = offset;
                        newMember._arrayOuterSize -= i;
                        newMember._arrayInnerSize -= j;
                        offset += bMember._elementSize;
                        arrayMembers.push_back( newMember );
                    }
                }
                for ( size_t i = 0u; i < bMember._arrayOuterSize; ++i )
                {
                    BlockMember newMember = bMember;
                    Util::StringFormatTo( newMember._name, "{}[{}]", bMember._name.c_str(), i );
                    newMember._nameHash = _ID( newMember._name.c_str() );
                    newMember._size -= i * (bMember._arrayInnerSize * bMember._elementSize);
                    newMember._offset = i * (bMember._arrayInnerSize * bMember._elementSize);
                    newMember._arrayOuterSize -= i;
                    arrayMembers.push_back( newMember );
                }
            }
            else if ( bMember._arrayOuterSize > 0 )
            {
                for ( size_t i = 0u; i < bMember._arrayOuterSize; ++i )
                {
                    BlockMember newMember = bMember;
                    Util::StringFormatTo( newMember._name, "{}[{}]", bMember._name.c_str(), i );
                    newMember._nameHash = _ID( newMember._name.c_str() );
                    newMember._size -= offset;
                    newMember._offset = offset;
                    newMember._arrayOuterSize -= i;
                    offset += bMember._elementSize;
                    arrayMembers.push_back( newMember );
                }
            }
        }

        if ( !arrayMembers.empty() )
        {
            _blockMembers.insert( end( _blockMembers ), begin( arrayMembers ), end( arrayMembers ) );
        }
    }

    void UniformBlockUploader::resizeBlockBuffer( const bool increaseSize )
    {
        const U16 newSize = _buffer == nullptr ? RingBufferLength : _bufferSizeFactor;

        ShaderBufferDescriptor bufferDescriptor{};
        bufferDescriptor._name = _uniformBlock._name.c_str();
        bufferDescriptor._ringBufferLength = increaseSize ? newSize : RingBufferLength;
        bufferDescriptor._name.append( "_" );
        bufferDescriptor._name.append( _parentShaderName.c_str() );
        bufferDescriptor._elementCount = 1;
        bufferDescriptor._usageType = BufferUsageType::CONSTANT_BUFFER;
        bufferDescriptor._updateFrequency = BufferUpdateFrequency::OCCASIONAL;
        bufferDescriptor._elementSize = _uniformBlockSizeAligned;
        _buffer = _context.newShaderBuffer( bufferDescriptor );
        _uniformBlockDirty = true;
    }

    void UniformBlockUploader::uploadUniformData( const UniformData& uniforms ) noexcept
    {
        if ( _uniformBlock._bindingSlot != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr )
        {
            for ( const UniformData::Entry& uniform : uniforms.entries() )
            {
                if ( uniform._type == PushConstantType::COUNT || uniform._bindingHash == 0u )
                {
                    continue;
                }

                for ( BlockMember& member : _blockMembers )
                {
                    if ( member._nameHash == uniform._bindingHash )
                    {
                        DIVIDE_GPU_ASSERT( uniform._range._length <= member._size );

                        Byte* dst = &_localDataCopy.data()[member._offset];
                        const Byte* src = uniforms.data(uniform._range._startOffset);
                        const size_t numBytes = uniform._range._length;

                        if ( std::memcmp( dst, src, numBytes ) != 0 )
                        {
                            std::memcpy( dst, src, numBytes );
                            _uniformBlockDirty = true;
                        }

                        continue;
                    }
                }
            }
        }
    }

    bool UniformBlockUploader::commit( DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        if ( _uniformBlockDirty )
        {
            DIVIDE_GPU_ASSERT( _uniformBlock._bindingSlot != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr );

            if ( _needsResize )
            {
                resizeBlockBuffer( true );
                _needsResize = false;
                _needsQueueIncrement = false;
                _bufferWritesThisFrame = 0u;
            }

            if ( _needsQueueIncrement )
            {
                _buffer->incQueue();
                _needsQueueIncrement = false;
                ++_bufferWritesThisFrame;
            }

            memCmdInOut._bufferLocks.push_back( _buffer->writeData( _localDataCopy.data() ) );
            _needsQueueIncrement = true;
            _uniformBlockDirty = false;
        }

        return prepare( set );
    }

    bool UniformBlockUploader::prepare( DescriptorSet& set )
    {
        if ( _uniformBlock._bindingSlot != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr )
        {
            const U8 targetBlock = to_U8( _uniformBlock._bindingSlot );
            for ( U8 i = 0u; i < set._bindingCount; ++i )
            {
                DescriptorSetBinding& it = set._bindings[i];
                if ( it._slot == targetBlock )
                {
                    assert( it._data._type == DescriptorSetBindingType::UNIFORM_BUFFER );

                    it._shaderStageVisibility = _shaderStageVisibilityMask;
                    Set( it._data, _buffer.get(), { 0u, _buffer->getPrimitiveCount() } );
                    return true;
                }
            }

            DescriptorSetBinding& binding = AddBinding( set, targetBlock, _shaderStageVisibilityMask );
            Set( binding._data, _buffer.get(), { 0u, _buffer->getPrimitiveCount() } );
            return true;
        }

        return false;
    }

    void UniformBlockUploader::onFrameEnd() noexcept
    {
        if ( _bufferWritesThisFrame >= _buffer->queueLength() )
        {
            _bufferSizeFactor = _bufferWritesThisFrame * 4; /*enough space for 4 frames of data*/
            _needsResize = true;
        }
        _bufferWritesThisFrame = 0u;
    }

    void UniformBlockUploader::toggleStageVisibility( const U16 visibilityMask, bool state )
    {
        state ? _shaderStageVisibilityMask |= visibilityMask : _shaderStageVisibilityMask &= ~visibilityMask;
    }

    void UniformBlockUploader::toggleStageVisibility( const ShaderStageVisibility visibility, const bool state )
    {
        toggleStageVisibility(to_U16(visibility), state);
    }

    size_t UniformBlockUploader::totalBufferSize() const noexcept
    {
        if ( _buffer != nullptr )
        {
            return _buffer->alignedBufferSize() * _buffer->queueLength();
        }

        return 0u;
    }
}//namespace Divide
