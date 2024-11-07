

#include "Headers/glGenericVertexData.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    glGenericVertexData::glGenericVertexData( GFXDevice& context, const U16 ringBufferLength, const std::string_view name )
        : GenericVertexData( context, ringBufferLength, name )
    {
    }

    void glGenericVertexData::reset()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _bufferObjects.clear();

        LockGuard<SharedMutex> w_lock( _idxBufferLock );
        if (_idxBuffer._handle != GL_NULL_HANDLE )
        {
            GLUtil::freeBuffer(_idxBuffer._handle );
        }
        _idxBuffer = {};
    }

    /// Submit a draw command to the GPU using this object and the specified command
    void glGenericVertexData::draw( const GenericDrawCommand& command, [[maybe_unused]] VDIUserData* userData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Update buffer bindings
        for ( const auto& buffer : _bufferObjects )
        {
            bindBufferInternal( buffer._bindConfig );
        }

        {
            SharedLock<SharedMutex> w_lock( _idxBufferLock );

            gl46core::GLenum indexFormat = gl46core::GL_NONE;

            GenericDrawCommand submitCommand = command;
            if ( _idxBuffer._bufferSize > 0u )
            {

                if (_idxBuffer._idxBufferSync != nullptr )
                {
                    gl46core::glWaitSync(_idxBuffer._idxBufferSync, gl46core::UnusedMask::GL_UNUSED_BIT, gl46core::GL_TIMEOUT_IGNORED );
                    GL_API::DestroyFenceSync(_idxBuffer._idxBufferSync );
                }
                if ( GL_API::GetStateTracker().setActiveBuffer( gl46core::GL_ELEMENT_ARRAY_BUFFER, _idxBuffer._handle ) == GLStateTracker::BindResult::FAILED ) [[unlikely]]
                {
                    DIVIDE_UNEXPECTED_CALL();
                }

                indexFormat = _idxBuffer._data.count > 0u ? (_idxBuffer._data.smallIndices ? gl46core::GL_UNSIGNED_SHORT : gl46core::GL_UNSIGNED_INT) : gl46core::GL_NONE;
                if (_idxBuffer._buffer != nullptr )
                {
                    submitCommand._cmd.firstIndex += _idxBuffer._buffer->getDataOffset() / (_idxBuffer._data.smallIndices ? sizeof(U16) : sizeof(U32));
                }
            }

            // Submit the draw command
            GLUtil::SubmitRenderCommand(submitCommand, indexFormat );
        }
    }

    BufferLock glGenericVertexData::setIndexBuffer( const IndexBuffer& indices )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        LockGuard<SharedMutex> w_lock( _idxBufferLock );

        if ( _idxBuffer._handle != GL_NULL_HANDLE )
        {
            if ( indices.count == 0u || // We don't need indices anymore
                _idxBuffer._data.dynamic != indices.dynamic || // Buffer usage mode changed
                _idxBuffer._data.count < indices.count) // Buffer not big enough
            {
                GLUtil::freeBuffer(_idxBuffer._handle );
                _idxBuffer._bufferSize = 0u;
            }
        }

        if ( indices.count == 0u )
        {
            // That's it. We have a buffer entry but no GL buffer associated with it, so it won't be used
            return {};
        }

        const size_t elementSize = indices.smallIndices ? sizeof( gl46core::GLushort ) : sizeof( gl46core::GLuint );
        const size_t range = indices.count * elementSize;
        bufferPtr data = indices.data;

        const gl46core::GLenum usage = indices.dynamic ? gl46core::GL_STREAM_DRAW : gl46core::GL_STATIC_DRAW;

        if (indices.indicesNeedCast)
        {
            _idxBuffer._data._smallIndicesTemp.resize(indices.count);
            const U32* const dataIn = reinterpret_cast<U32*>(data);
            for (size_t i = 0u; i < indices.count; ++i)
            {
                _idxBuffer._data._smallIndicesTemp[i] = to_U16(dataIn[i]);
            }
            data = _idxBuffer._data._smallIndicesTemp.data();

        }

        if (_idxBuffer._handle == GL_NULL_HANDLE )
        {
            _idxBuffer._data = indices;
            // At this point, we need an actual index buffer
            _idxBuffer._bufferSize = range;
            GLUtil::createAndAllocateBuffer( _idxBuffer._handle,
                                             _name.empty() ? nullptr : Util::StringFormat( "{}_index", _name.c_str() ).c_str(),
                                             gl46core::GL_DYNAMIC_STORAGE_BIT,
                                             _idxBuffer._bufferSize,
                                             { data, range});
        }
        else
        {
            DIVIDE_ASSERT( range <= _idxBuffer._bufferSize );

            gl46core::glInvalidateBufferSubData(_idxBuffer._handle, 0u, range );
            gl46core::glNamedBufferSubData(_idxBuffer._handle, 0u, range, data );
        }

        if ( !Runtime::isMainThread() )
        {
            if (_idxBuffer._idxBufferSync != nullptr )
            {
                GL_API::DestroyFenceSync(_idxBuffer._idxBufferSync );
            }

            _idxBuffer._idxBufferSync = GL_API::CreateFenceSync();
            gl46core::glFlush();
        }

        _idxBuffer._data._smallIndicesTemp.clear();

        return {};
    }

    /// Specify the structure and data of the given buffer
    BufferLock glGenericVertexData::setBuffer( const SetBufferParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._usageType != BufferUsageType::COUNT );
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
        implParams._target = gl46core::GL_ARRAY_BUFFER;

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

        impl->_buffer = std::make_unique<glBufferImpl>( _context, implParams, params._initialData, _name.empty() ? nullptr : _name.c_str() );

        BufferLock ret = {};
        for ( U32 i = 1u; i < ringSizeFactor; ++i )
        {
            ret = impl->_buffer->writeOrClearBytes(i * bufferSizeInBytes,
                                                   params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes,
                                                   params._initialData.first );
        }

        ret._range = {0u, implParams._dataSize};
        return ret;
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

        if ( impl != nullptr ) [[likely]]
        {
            const BufferParams& bufferParams = impl->_buffer->params()._bufferParams;
            size_t offsetInBytes = impl->_buffer->getDataOffset();

            if ( impl->_ringSizeFactor > 1 ) [[likely]]
            {
                offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
            }

            if (GL_API::GetStateTracker().bindActiveBuffer( bindConfig._bindIdx,
                                                            impl->_buffer->getBufferHandle(),
                                                            offsetInBytes,
                                                            impl->_elementStride ) == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

};
