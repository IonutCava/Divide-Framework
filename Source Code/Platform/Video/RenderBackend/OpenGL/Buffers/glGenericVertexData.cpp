#include "stdafx.h"

#include "Headers/glGenericVertexData.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    glGenericVertexData::glGenericVertexData( GFXDevice& context, const U32 ringBufferLength, const char* name )
        : GenericVertexData( context, ringBufferLength, name )
    {
    }

    void glGenericVertexData::reset()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _bufferObjects.clear();

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        for ( auto& idx : _idxBuffers )
        {
            if ( idx._handle != GLUtil::k_invalidObjectID )
            {
                GLUtil::freeBuffer( idx._handle );
            }
        }
        _idxBuffers.clear();
    }

    /// Submit a draw command to the GPU using this object and the specified command
    void glGenericVertexData::draw( const GenericDrawCommand& command, [[maybe_unused]] VDIUserData* userData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( GL_API::GetStateTracker()._primitiveRestartEnabled == primitiveRestartRequired() );
        DIVIDE_ASSERT( _idxBuffers.size() > command._bufferFlag );

        // Update buffer bindings
        for ( const auto& buffer : _bufferObjects )
        {
            bindBufferInternal( buffer._bindConfig );
        }

        {
            SharedLock<SharedMutex> w_lock( _idxBufferLock );
            auto& idxBuffer = _idxBuffers[command._bufferFlag];
            if ( idxBuffer._idxBufferSync != nullptr )
            {
                glWaitSync( idxBuffer._idxBufferSync, 0u, GL_TIMEOUT_IGNORED );
                GL_API::DestroyFenceSync( idxBuffer._idxBufferSync );
            }
            if ( GL_API::GetStateTracker().setActiveBuffer( GL_ELEMENT_ARRAY_BUFFER, idxBuffer._handle ) == GLStateTracker::BindResult::FAILED ) [[unlikely]]
            {
                DIVIDE_UNEXPECTED_CALL();
            }

            if ( !renderIndirect() &&
                 command._cmd.instanceCount == 1u &&
                 command._drawCount > 1u )
            {
                rebuildCountAndIndexData( command._drawCount, static_cast<GLsizei>(command._cmd.indexCount), command._cmd.firstIndex, idxBuffer._data.count );
            }

            // Submit the draw command
            GLUtil::SubmitRenderCommand( command,
                                         renderIndirect(),
                                         idxBuffer._data.count > 0u ? idxBuffer._data.smallIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT : GL_NONE,
                                         _indexInfo._countData.data(),
                                         (bufferPtr)_indexInfo._indexOffsetData.data() );
        }
    }

    BufferLock glGenericVertexData::setIndexBuffer( const IndexBuffer& indices )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        IndexBufferEntry* oldIdxBufferEntry = nullptr;

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        bool found = false;
        for ( auto& idxBuffer : _idxBuffers )
        {
            if ( idxBuffer._data.id == indices.id )
            {
                oldIdxBufferEntry = &idxBuffer;
                found = true;
                break;
            }
        }

        if ( !found )
        {
            oldIdxBufferEntry = &_idxBuffers.emplace_back();
            oldIdxBufferEntry->_data = indices;
        }
        else if ( oldIdxBufferEntry->_handle != GLUtil::k_invalidObjectID &&
                  (!AreCompatible( oldIdxBufferEntry->_data, indices ) || indices.count == 0u) )
        {
            GLUtil::freeBuffer( oldIdxBufferEntry->_handle );
        }

        IndexBuffer& oldIdxBuffer = oldIdxBufferEntry->_data;

        oldIdxBuffer.count = std::max( oldIdxBuffer.count, indices.count );

        if ( indices.count > 0u )
        {
            const size_t elementSize = indices.smallIndices ? sizeof( GLushort ) : sizeof( GLuint );

            vector_fast<U16> smallIndicesTemp;
            bufferPtr data = indices.data;

            if ( indices.indicesNeedCast )
            {
                smallIndicesTemp.resize( indices.count );
                const U32* const dataIn = reinterpret_cast<U32*>(data);
                for ( size_t i = 0u; i < indices.count; ++i )
                {
                    smallIndicesTemp[i] = to_U16( dataIn[i] );
                }
                data = smallIndicesTemp.data();
            }

            if ( oldIdxBufferEntry->_handle == GLUtil::k_invalidObjectID )
            {
                const size_t newDataSize = indices.count * elementSize;
                _indexBufferSize = std::max( newDataSize, _indexBufferSize );

                GLUtil::createBuffer( _indexBufferSize,
                                      oldIdxBufferEntry->_handle,
                                      _name.empty() ? nullptr : Util::StringFormat( "%s_index_%d", _name.c_str(), _idxBuffers.size() - 1u ).c_str() );
            }

            const size_t range = indices.count * elementSize;
            DIVIDE_ASSERT( range <= _indexBufferSize );

            if ( range == _indexBufferSize )
            {
                glNamedBufferData( oldIdxBufferEntry->_handle, range, data, indices.dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW );
            }
            else
            {
                glInvalidateBufferSubData( oldIdxBufferEntry->_handle, 0u, range );
                glNamedBufferSubData( oldIdxBufferEntry->_handle, 0u, range, data );
            }
            if ( !Runtime::isMainThread() )
            {
                if ( oldIdxBufferEntry->_idxBufferSync != nullptr )
                {
                    GL_API::DestroyFenceSync( oldIdxBufferEntry->_idxBufferSync );
                }

                oldIdxBufferEntry->_idxBufferSync = GL_API::CreateFenceSync();
                glFlush();
            }
        }

        return {};
    }

    /// Specify the structure and data of the given buffer
    BufferLock glGenericVertexData::setBuffer( const SetBufferParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._flags._usageType != BufferUsageType::COUNT );
        // Make sure we specify buffers in order.
        GenericBufferImpl* impl = nullptr;
        for ( auto& buffer : _bufferObjects )
        {
            if ( buffer._bindConfig._bufferIdx == params._bindConfig._bufferIdx )
            {
                impl = &buffer;
                break;
            }
        }
        if ( impl == nullptr )
        {
            impl = &_bufferObjects.emplace_back();
        }

        const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
        const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;

        BufferImplParams implParams;
        implParams._bufferParams = params._bufferParams;
        implParams._dataSize = bufferSizeInBytes * ringSizeFactor;
        implParams._target = GL_ARRAY_BUFFER;
        implParams._useChunkAllocation = true;

        const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;
        impl->_ringSizeFactor = ringSizeFactor;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = elementStride;

        if ( impl->_buffer != nullptr && impl->_buffer->params() == implParams )
        {
            return updateBuffer( params._bindConfig._bufferIdx, 0, params._bufferParams._elementCount, params._initialData.first );
        }

        impl->_buffer = eastl::make_unique<glBufferImpl>( _context, implParams, params._initialData, _name.empty() ? nullptr : _name.c_str() );

        for ( U32 i = 1u; i < ringSizeFactor; ++i )
        {
            impl->_buffer->writeOrClearBytes(i * bufferSizeInBytes,
                                             params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes,
                                             params._initialData.first,
                                             true );
        }

        return BufferLock
        {
            ._range = {0u, implParams._dataSize},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    /// Update the elementCount worth of data contained in the buffer starting from elementCountOffset size offset
    BufferLock glGenericVertexData::updateBuffer( const U32 buffer,
                                                  const U32 elementCountOffset,
                                                  const U32 elementCountRange,
                                                  const bufferPtr data )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GenericBufferImpl* impl = nullptr;
        for ( auto& bufferImpl : _bufferObjects )
        {
            if ( bufferImpl._bindConfig._bufferIdx == buffer )
            {
                impl = &bufferImpl;
                break;
            }
        }

        DIVIDE_ASSERT( impl != nullptr && "glGenericVertexData error: set buffer called for invalid buffer index!" );

        const BufferParams& bufferParams = impl->_buffer->params()._bufferParams;

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;

        DIVIDE_ASSERT( offsetInBytes + dataCurrentSizeInBytes <= bufferParams._elementCount * bufferParams._elementSize );

        if ( impl->_ringSizeFactor > 1u )
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        impl->_buffer->writeOrClearBytes( offsetInBytes, dataCurrentSizeInBytes, data );

        return BufferLock
        {
            ._range = { offsetInBytes, dataCurrentSizeInBytes },
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    void glGenericVertexData::bindBufferInternal( const SetBufferParams::BufferBindConfig& bindConfig )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GenericBufferImpl* impl = nullptr;
        for ( auto& bufferImpl : _bufferObjects )
        {
            if ( bufferImpl._bindConfig._bufferIdx == bindConfig._bufferIdx )
            {
                impl = &bufferImpl;
                break;
            }
        }

        if ( impl == nullptr ) [[unlikely]]
        {
            return;
        }

        const BufferParams& bufferParams = impl->_buffer->params()._bufferParams;
        size_t offsetInBytes = impl->_buffer->memoryBlock()._offset;

        if ( impl->_ringSizeFactor > 1 ) [[likely]]
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        const GLStateTracker::BindResult ret =
            GL_API::GetStateTracker().bindActiveBuffer( bindConfig._bindIdx,
                                                        impl->_buffer->memoryBlock()._bufferHandle,
                                                        offsetInBytes,
                                                        impl->_elementStride );

        DIVIDE_ASSERT(ret != GLStateTracker::BindResult::FAILED );
    }

    void glGenericVertexData::rebuildCountAndIndexData( const U32 drawCount, const GLsizei indexCount, const U32 firstIndex, const size_t indexBufferSize )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _lastDrawCount == drawCount && _lastIndexCount == indexCount && _lastFirstIndex == firstIndex )
        {
            return;
        }

        const size_t idxCountInternal = drawCount * indexBufferSize;
        if ( idxCountInternal >= _indexInfo._indexOffsetData.size() )
        {
            _indexInfo._indexOffsetData.resize( idxCountInternal, _lastFirstIndex );
        }

        if ( _lastDrawCount != drawCount )
        {
            if ( drawCount >= _indexInfo._countData.size() )
            {
                // No need to resize down. Cheap to keep in memory.
                _indexInfo._countData.resize( drawCount, _lastIndexCount );
            }
            _lastDrawCount = drawCount;
        }

        if ( _lastIndexCount != indexCount )
        {
            eastl::fill( begin( _indexInfo._countData ), begin( _indexInfo._countData ) + drawCount, indexCount );
            _lastIndexCount = indexCount;
        }

        if ( _lastFirstIndex != firstIndex )
        {
            if ( idxCountInternal > 0u )
            {
                eastl::fill( begin( _indexInfo._indexOffsetData ), begin( _indexInfo._indexOffsetData ) + idxCountInternal, firstIndex );
            }
            _lastFirstIndex = firstIndex;
        }
    }

};
