// Copyright (c) 2018-2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/ShaderDataUploader.h"

#include "Core/Headers/ByteBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
namespace Divide
{

    namespace Reflection
    {
        constexpr U16 BYTE_BUFFER_VERSION = 7u; // v7: generated loose-uniform declarations

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
                    default: DIVIDE_UNEXPECTED_CALL();
                }
            };

            const I32 lhsPriority = g_TypePriority( lhs._typeHash );
            const I32 rhsPriority = g_TypePriority( rhs._typeHash );
            if ( lhsPriority != rhsPriority )
            {
                return lhsPriority < rhsPriority;
            }

            return lhs._name < rhs._name;
        };

        bool SaveReflectionData( const ResourcePath& path, const ResourcePath& file, const Reflection::Data& reflectionDataIn,
                                 const UniformsSet& uniformsIn, const eastl::set<U64>& atomIDsIn )
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
                buffer << to_base( entry._textureType );
                buffer << to_base( entry._sampledDataType );
                buffer << entry._isDepth;
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
            buffer << uniformsIn.size();
            for ( const UniformDeclaration& uniform : uniformsIn )
            {
                buffer << uniform._name;
                buffer << uniform._type;
            }
            buffer << reflectionDataIn._uniformBlockBindingIndex;
            buffer << to_base(reflectionDataIn._pushConstantsState);
            buffer << reflectionDataIn._fragmentOutputs;
            buffer << reflectionDataIn._vertexInputs;
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

        bool LoadReflectionData( const ResourcePath& path, const ResourcePath& file, Reflection::Data& reflectionDataOut,
                                 UniformsSet& uniformsOut, eastl::set<U64>& atomIDsOut )
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
                BaseType<TextureType> textureType{};
                BaseType<GFXDataFormat> dataType{};
                buffer >> textureType;
                buffer >> dataType;
                entry._textureType = static_cast<TextureType>(textureType);
                entry._sampledDataType = static_cast<GFXDataFormat>(dataType);
                buffer >> entry._isDepth;
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
                    buffer >> sizeTemp;
                    uniformsOut.clear();
                    for ( size_t i = 0u; i < sizeTemp; ++i )
                    {
                        UniformDeclaration uniform{};
                        buffer >> uniform._name;
                        buffer >> uniform._type;
                        uniform._typeHash = _ID( uniform._type.c_str() );
                        uniformsOut.emplace( uniform );
                    }
                    buffer >> reflectionDataOut._uniformBlockBindingIndex;
                    buffer >> reflectionDataOut._pushConstantsState;
                    buffer >> reflectionDataOut._fragmentOutputs;
                    buffer >> reflectionDataOut._vertexInputs;
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

    UniformUploadArena::SizeClass::SizeClass( const size_t stride ) noexcept
        : _stride( stride )
    {
    }

    UniformUploadArena::SizeClass::~SizeClass() = default;
    UniformUploadArena::SizeClass::SizeClass( SizeClass&& ) noexcept = default;
    UniformUploadArena::SizeClass& UniformUploadArena::SizeClass::operator=( SizeClass&& ) noexcept = default;

    UniformUploadArena::UniformUploadArena( GFXDevice& context ) noexcept
        : _context( context )
        , _frameNumber( GFXDevice::FrameCount() )
    {
    }

    UniformUploadArena::~UniformUploadArena() = default;

    bool UniformUploadArena::CalculateSlotStride( const size_t byteCount, const size_t maxRange,
                                                  const size_t offsetAlignment, const size_t nonCoherentAtomSize,
                                                  size_t& strideOut ) noexcept
    {
        strideOut = 0u;
        if ( byteCount == 0u || byteCount > maxRange || offsetAlignment == 0u || nonCoherentAtomSize == 0u )
        {
            return false;
        }

        size_t a = offsetAlignment;
        size_t b = nonCoherentAtomSize;
        while ( b != 0u )
        {
            const size_t remainder = a % b;
            a = b;
            b = remainder;
        }

        const size_t multiplier = offsetAlignment / a;
        if ( multiplier > SIZE_MAX / nonCoherentAtomSize )
        {
            return false;
        }
        const size_t alignment = multiplier * nonCoherentAtomSize;
        if ( byteCount > SIZE_MAX - (alignment - 1u) )
        {
            return false;
        }

        const size_t stride = Util::GetAlignmentCorrected( byteCount, alignment );
        if ( stride > U32_MAX / (UniformUploadPageCursor::SlotsPerPage - 1u) ||
             stride > SIZE_MAX / UniformUploadPageCursor::SlotsPerPage )
        {
            return false;
        }
        strideOut = stride;
        return true;
    }

    bool UniformUploadArena::beginFrame( const U64 frameNumber )
    {
        LockGuard<Mutex> lock( _mutex );
        if ( frameNumber < _frameNumber )
        {
            Console::errorfn( "Renderer: uniform upload arena frame number moved backwards" );
            return false;
        }

        for ( SizeClass& sizeClass : _sizeClasses )
        {
            if ( !sizeClass._cursor.beginFrame( frameNumber ) )
            {
                Console::errorfn( "Renderer: failed to recycle uniform upload slots" );
                return false;
            }
        }
        _frameNumber = frameNumber;
        return true;
    }

    bool UniformUploadArena::upload( Byte* data, const size_t byteCount, Allocation& allocationOut )
    {
        const DeviceInformation& device = GFXDevice::GetDeviceInformation();
        size_t stride = 0u;
        if ( data == nullptr ||
             !CalculateSlotStride( byteCount, device._maxSizeBytesUBO, device._offsetAlignmentBytesUBO,
                                   device._nonCoherentAtomSize, stride ) )
        {
            Console::errorfn( "Renderer: invalid uniform upload size or device alignment ({} bytes)", byteCount );
            return false;
        }

        LockGuard<Mutex> lock( _mutex );
        SizeClass* sizeClass = nullptr;
        size_t sizeClassIndex = 0u;
        for ( ; sizeClassIndex < _sizeClasses.size(); ++sizeClassIndex )
        {
            SizeClass& candidate = _sizeClasses[sizeClassIndex];
            if ( candidate._stride == stride )
            {
                sizeClass = &candidate;
                break;
            }
        }
        if ( sizeClass == nullptr )
        {
            _sizeClasses.emplace_back( stride );
            sizeClass = &_sizeClasses.back();
            if ( !sizeClass->_cursor.beginFrame( _frameNumber ) )
            {
                Console::errorfn( "Renderer: failed to initialize uniform upload slots" );
                return false;
            }
        }

        const UniformUploadPageCursor::Allocation slot = sizeClass->_cursor.reserve();
        auto& pages = sizeClass->_pages;
        if ( slot._page == pages.size() )
        {
            const size_t pageSize = stride * UniformUploadPageCursor::SlotsPerPage;
            if ( _totalBufferSize > SIZE_MAX - pageSize )
            {
                Console::errorfn( "Renderer: uniform upload arena size overflow" );
                return false;
            }

            ShaderBufferDescriptor descriptor{};
            descriptor._name = Util::StringFormat( "UniformUpload_{}_{}", stride, slot._page );
            descriptor._ringBufferLength = UniformUploadPageCursor::SlotsPerPage;
            descriptor._elementCount = to_U32( stride );
            descriptor._elementSize = 1u;
            descriptor._usageType = BufferUsageType::CONSTANT_BUFFER;
            descriptor._updateFrequency = BufferUpdateFrequency::OFTEN;

            ShaderBuffer_uptr page = _context.newShaderBuffer( descriptor );
            if ( page == nullptr || !page->supportsPersistentWrites() || page->alignedBufferSize() != stride )
            {
                Console::errorfn( "Renderer: uniform upload page is not persistently mapped with the required stride" );
                return false;
            }

            pages.push_back( MOV( page ) );
            _totalBufferSize += pageSize;
        }
        else if ( slot._page > pages.size() )
        {
            Console::errorfn( "Renderer: uniform upload page cursor skipped a page" );
            return false;
        }

        ShaderBuffer* page = pages[slot._page].get();
        // Keep the buffer's range-wait checks; final-consumer lifetime is tracked by frame completion, not this write.
        const BufferLock write = page->writeBytes( { 0u, byteCount }, data, slot._slot );
        if ( write._buffer == nullptr || write._type != BufferSyncUsage::CPU_WRITE_TO_GPU_READ ||
             write._range._startOffset != to_size( slot._slot ) * stride || write._range._length != byteCount )
        {
            Console::errorfn( "Renderer: uniform upload page write did not complete as a direct mapped write" );
            return false;
        }

        allocationOut = { page, sizeClassIndex, slot._page, slot._slot, slot._generation };
        return true;
    }

    bool UniformUploadArena::release( const Allocation allocation, const UniformUploadPageCursor::Usage& usage )
    {
        LockGuard<Mutex> lock( _mutex );
        if ( allocation._buffer == nullptr || allocation._sizeClassIndex >= _sizeClasses.size() )
        {
            Console::errorfn( "Renderer: invalid uniform upload allocation release" );
            return false;
        }

        SizeClass& sizeClass = _sizeClasses[allocation._sizeClassIndex];
        if ( allocation._page >= sizeClass._pages.size() ||
             sizeClass._pages[allocation._page].get() != allocation._buffer ||
             !sizeClass._cursor.release( { allocation._page, allocation._slot, allocation._generation }, usage ) )
        {
            Console::errorfn( "Renderer: uniform upload allocation release failed" );
            return false;
        }
        return true;
    }

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
        };

        if ( uniformBlock._memberCount == 0u )
        {
            return;
        }

        _blockMembers.resize( uniformBlock._memberCount );
        _localDataCopy.resize( std::max( uniformBlock._size, uniformBlock._paddedSize ) );

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
        eastl::stable_sort( begin( _blockMembers ), end( _blockMembers ),
                            []( const BlockMember& lhs, const BlockMember& rhs )
                            {
                                return lhs._nameHash < rhs._nameHash;
                            } );
    }

    UniformBlockUploader::UniformBlockUploader( UniformBlockUploader&& other ) noexcept
        : _uniformBlock( MOV( other._uniformBlock ) )
        , _context( other._context )
        , _shaderStageVisibilityMask( other._shaderStageVisibilityMask )
        , _localDataCopy( MOV( other._localDataCopy ) )
        , _blockMembers( MOV( other._blockMembers ) )
        , _parentShaderName( MOV( other._parentShaderName ) )
        , _allocation( other._allocation )
        , _snapshot( other._snapshot )
    {
        other._allocation = {};
    }

    UniformBlockUploader::~UniformBlockUploader()
    {
        if ( _allocation._buffer != nullptr &&
             !_context.uniformUploadArena().release( _allocation, _snapshot.usage() ) )
        {
            Console::errorfn( "Renderer: failed to retire uniform block '{}' for shader '{}'",
                              _uniformBlock._name.c_str(), _parentShaderName.c_str() );
        }
    }

    void UniformBlockUploader::uploadUniformData( const UniformData& uniforms ) noexcept
    {
        if ( _uniformBlock._bindingSlot != Reflection::INVALID_BINDING_INDEX && !_localDataCopy.empty() )
        {
            ApplyUniformData( uniforms, _blockMembers, _localDataCopy, _snapshot );
        }
    }

    void UniformBlockUploader::ApplyUniformData( const UniformData& uniforms, const std::span<const BlockMember> members,
                                                const std::span<Byte> destination, UniformUploadSnapshotState& snapshot ) noexcept
    {
        if ( members.empty() )
        {
            return;
        }
        const BlockMember* last = members.data() + members.size();
        for ( const UniformData::Entry& uniform : uniforms.entries() )
        {
            if ( uniform._type == PushConstantType::COUNT || uniform._bindingHash == 0u )
            {
                continue;
            }

            auto member = eastl::lower_bound( members.data(), last, uniform._bindingHash,
                                               []( const BlockMember& entry, const U64 hash )
                                               {
                                                   return entry._nameHash < hash;
                                               } );
            for ( ; member != last && member->_nameHash == uniform._bindingHash; ++member )
            {
                DIVIDE_GPU_ASSERT( uniform._range._length <= member->_size );
                snapshot.updateBytes( destination.data() + member->_offset,
                                      uniforms.data( uniform._range._startOffset ), uniform._range._length );
            }
        }
    }

    DescriptorSetBinding* UniformBlockUploader::FindBinding( DescriptorSet& set, const U8 targetSlot, U8& indexHint ) noexcept
    {
        if ( indexHint >= set._bindingCount || set._bindings[indexHint]._slot != targetSlot )
        {
            indexHint = U8_MAX;
            for ( U8 i = 0u; i < set._bindingCount; ++i )
            {
                if ( set._bindings[i]._slot == targetSlot )
                {
                    indexHint = i;
                    break;
                }
            }
        }
        return indexHint < set._bindingCount ? &set._bindings[indexHint] : nullptr;
    }

    bool UniformBlockUploader::commit( DescriptorSet& set )
    {
        if ( _uniformBlock._bindingSlot == Reflection::INVALID_BINDING_INDEX || _localDataCopy.empty() ||
             _context.uniformUploadFailed() )
        {
            return false;
        }

        const U64 frameNumber = GFXDevice::FrameCount();
        if ( _snapshot.needsUpload() )
        {
            UniformUploadArena::Allocation nextAllocation{};
            if ( !_context.uniformUploadArena().upload( _localDataCopy.data(), _localDataCopy.size(), nextAllocation ) )
            {
                _context.reportUniformUploadFailure( _parentShaderName.c_str(), _uniformBlock._name.c_str() );
                return false;
            }
            if ( _allocation._buffer != nullptr &&
                 !_context.uniformUploadArena().release( _allocation, _snapshot.usage() ) )
            {
                DIVIDE_UNUSED( _context.uniformUploadArena().release( nextAllocation, {} ) );
                _context.reportUniformUploadFailure( _parentShaderName.c_str(), _uniformBlock._name.c_str() );
                return false;
            }
            _allocation = nextAllocation;
            _snapshot.markUploaded();
        }

        _snapshot.markUsed( frameNumber );
        return prepare( set );
    }

    bool UniformBlockUploader::prepare( DescriptorSet& set )
    {
        if ( _uniformBlock._bindingSlot != Reflection::INVALID_BINDING_INDEX && _allocation._buffer != nullptr )
        {
            const U8 targetBlock = to_U8( _uniformBlock._bindingSlot );
            if ( DescriptorSetBinding* binding = FindBinding( set, targetBlock, _bindingIndexHint ) )
            {
                DescriptorSetBinding& it = *binding;
                DIVIDE_GPU_ASSERT(it._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC);

                if ( it._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC &&
                     it._shaderStageVisibility == _shaderStageVisibilityMask &&
                     it._data._buffer._buffer == _allocation._buffer &&
                     it._data._buffer._range._startOffset == 0u &&
                     it._data._buffer._range._length == _localDataCopy.size() &&
                     it._data._buffer._queueReadIndex == _allocation._slot )
                {
                    return false;
                }

                it._shaderStageVisibility = _shaderStageVisibilityMask;
                Set( it._data, _allocation._buffer );
                it._data._buffer._range = { 0u, _localDataCopy.size() };
                it._data._buffer._queueReadIndex = _allocation._slot;
                return true;
            }

            DescriptorSetBinding& binding = AddBinding( set, targetBlock, _shaderStageVisibilityMask );
            _bindingIndexHint = set._bindingCount - 1u;
            Set( binding._data, _allocation._buffer );
            binding._data._buffer._range = { 0u, _localDataCopy.size() };
            binding._data._buffer._queueReadIndex = _allocation._slot;
            return true;
        }

        return false;
    }

    void UniformBlockUploader::toggleStageVisibility( const U16 visibilityMask, bool state )
    {
        state ? _shaderStageVisibilityMask |= visibilityMask : _shaderStageVisibilityMask &= ~visibilityMask;
    }

    void UniformBlockUploader::toggleStageVisibility( const ShaderStageVisibility visibility, const bool state )
    {
        toggleStageVisibility(to_U16(visibility), state);
    }

}//namespace Divide
