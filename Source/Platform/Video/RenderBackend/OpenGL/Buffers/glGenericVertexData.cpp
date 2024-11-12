

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
        firstIndexOffsetCount(INVALID_INDEX_OFFSET);
    }

    void glGenericVertexData::reset()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _bufferObjects.clear();
        _indexBuffer = {};
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

        SharedLock<SharedMutex> r_lock( _idxBufferLock );

        if (_indexBuffer._buffer != nullptr &&
            _indexBuffer._data.count > 0u)
        {
            if (GL_API::GetStateTracker().setActiveBuffer(gl46core::GL_ELEMENT_ARRAY_BUFFER, _indexBuffer._buffer->getBufferHandle()) == GLStateTracker::BindResult::FAILED) [[unlikely]]
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            if (firstIndexOffsetCount() == INVALID_INDEX_OFFSET)
            {
                // Skip this draw for now
                return;
            }
            const BufferParams& bufferParams = _indexBuffer._buffer->params()._bufferParams;
            
            size_t offsetInBytes = 0u;

            const size_t indexSizeInBytes = bufferParams._elementSize;
            DIVIDE_ASSERT(indexSizeInBytes == (_indexBuffer._data.smallIndices ? sizeof(gl::GLushort) : sizeof(gl::GLuint)));

            if (_indexBuffer._ringSizeFactor > 1u) [[likely]]
            {
                offsetInBytes += (bufferParams._elementCount * indexSizeInBytes) * queueIndex();
            }

            GenericDrawCommand submitCommand = command;
            submitCommand._cmd.firstIndex += to_U32(offsetInBytes / indexSizeInBytes);
            submitCommand._cmd.firstIndex += firstIndexOffsetCount();
            // Submit the draw command
            const gl46core::GLenum indexFormat = _indexBuffer._data.smallIndices ? gl46core::GL_UNSIGNED_SHORT : gl46core::GL_UNSIGNED_INT;
            GLUtil::SubmitRenderCommand(submitCommand, indexFormat);
        }
        else
        {
            GLUtil::SubmitRenderCommand(command);
        }
    }

    BufferLock glGenericVertexData::setIndexBuffer( const IndexBuffer& indices )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        BufferLock ret = {};

        LockGuard<SharedMutex> w_lock( _idxBufferLock );

        if ( _indexBuffer._buffer != nullptr )
        {
            if (indices.count == 0u || // We don't need indices anymore
                _indexBuffer._data.count < indices.count || // Buffer not big enough
                _indexBuffer._data.dynamic != indices.dynamic || // Buffer usage mode changed
                _indexBuffer._data.smallIndices != indices.smallIndices)  //Different element size
            {
                _indexBuffer = {};
            }
        }

        _indexBuffer._data = indices;
        if (_indexBuffer._data.count == 0u )
        {
            // That's it. We have a buffer entry but no GL buffer associated with it, so it won't be used
            firstIndexOffsetCount(0u);
            return ret;
        }

        SCOPE_EXIT
        {
            efficient_clear(_indexBuffer._data.smallIndicesTemp);
        };

        bufferPtr data = _indexBuffer._data.data;

        if (data != nullptr && _indexBuffer._data.indicesNeedCast)
        {
            _indexBuffer._data.smallIndicesTemp.resize(_indexBuffer._data.count);
            const U32* const dataIn = reinterpret_cast<U32*>(data);
            for (size_t i = 0u; i < _indexBuffer._data.count; ++i)
            {
                _indexBuffer._data.smallIndicesTemp[i] = to_U16(dataIn[i]);
            }
            data = _indexBuffer._data.smallIndicesTemp.data();
        }

        const size_t elementSize = _indexBuffer._data.smallIndices ? sizeof(gl::GLushort) : sizeof(gl::GLuint);
        const size_t ringSizeFactor = _indexBuffer._data.useRingBuffer ? queueLength() : 1u;
        const size_t bufferSizeInBytes = _indexBuffer._data.count * elementSize;

        BufferImplParams implParams;
        implParams._target = gl::GL_ELEMENT_ARRAY_BUFFER;
        implParams._bufferParams._usageType = BufferUsageType::INDEX_BUFFER;
        implParams._bufferParams._hostVisible = false;
        implParams._bufferParams._updateFrequency = _indexBuffer._data.dynamic ? BufferUpdateFrequency::OFTEN : BufferUpdateFrequency::OCASSIONAL;
        implParams._bufferParams._elementCount = to_U32(_indexBuffer._data.count);
        implParams._bufferParams._elementSize = elementSize;
        implParams._dataSize = bufferSizeInBytes * ringSizeFactor;

        _indexBuffer._ringSizeFactor = ringSizeFactor;
        _indexBuffer._elementStride = elementSize;

        if (_indexBuffer._buffer != nullptr &&
            _indexBuffer._buffer->params()._bufferParams._elementSize == implParams._bufferParams._elementSize &&
            _indexBuffer._buffer->params()._bufferParams._hostVisible == implParams._bufferParams._hostVisible &&
            _indexBuffer._buffer->params()._bufferParams._updateFrequency == implParams._bufferParams._updateFrequency &&
            _indexBuffer._buffer->params()._dataSize >= implParams._dataSize)
        {
            return updateIndexBuffer(0u, implParams._bufferParams._elementCount, data);
        }

        _indexBuffer._buffer = std::make_unique<glBufferImpl>(_context, implParams, std::make_pair(data, data == nullptr ? 0u : bufferSizeInBytes), _name.empty() ? nullptr : Util::StringFormat("{}_index", _name.c_str()).c_str());

        for (U32 i = 1u; i < ringSizeFactor; ++i)
        {
            _indexBuffer._buffer->writeOrClearBytes(i * bufferSizeInBytes, bufferSizeInBytes, data);
        }

        firstIndexOffsetCount(_indexBuffer._buffer->getDataOffset() / elementSize);

        return BufferLock
        {
            ._range = { 0u, bufferSizeInBytes * ringSizeFactor },
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = _indexBuffer._buffer.get()
        };
    }

    /// Specify the structure and data of the given buffer
    BufferLock glGenericVertexData::setBuffer( const SetBufferParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._bufferParams._usageType != BufferUsageType::COUNT );
        // Make sure we specify buffers in order.
        GenericBindableBufferImpl* impl = nullptr;
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
        implParams._target = gl46core::GL_ARRAY_BUFFER;
        implParams._bufferParams = params._bufferParams;
        implParams._dataSize = bufferSizeInBytes * ringSizeFactor;

        const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                            ? params._bufferParams._elementSize
                                                            : params._elementStride;
        impl->_ringSizeFactor = ringSizeFactor;
        impl->_bindConfig = params._bindConfig;
        impl->_elementStride = elementStride;

        if (_indexBuffer._buffer != nullptr &&
            _indexBuffer._buffer->params()._bufferParams._elementSize == implParams._bufferParams._elementSize &&
            _indexBuffer._buffer->params()._bufferParams._hostVisible == implParams._bufferParams._hostVisible &&
            _indexBuffer._buffer->params()._bufferParams._updateFrequency == implParams._bufferParams._updateFrequency &&
            _indexBuffer._buffer->params()._dataSize >= implParams._dataSize)
        {
            return updateBuffer( params._bindConfig._bufferIdx, 0, params._bufferParams._elementCount, params._initialData.first );
        }

        impl->_buffer = std::make_unique<glBufferImpl>( _context, implParams, params._initialData, _name.empty() ? nullptr : _name.c_str() );

        for ( U32 i = 1u; i < ringSizeFactor; ++i )
        {
            impl->_buffer->writeOrClearBytes(i * bufferSizeInBytes,
                                             params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes,
                                             params._initialData.first );
        }
        
        return BufferLock
        {
            ._range = {0u, implParams._dataSize},
            ._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ,
            ._buffer = impl->_buffer.get()
        };
    }

    BufferLock glGenericVertexData::updateBuffer(GenericBufferImpl& buffer, const U32 elementCountOffset, const U32 elementCountRange, bufferPtr data)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        const BufferParams& bufferParams = buffer._buffer->params()._bufferParams;

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;

        DIVIDE_ASSERT(offsetInBytes + dataCurrentSizeInBytes <= bufferParams._elementCount * bufferParams._elementSize);

        if (buffer._ringSizeFactor > 1u)
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        return buffer._buffer->writeOrClearBytes(offsetInBytes, dataCurrentSizeInBytes, data);
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
        return updateBuffer(*impl, elementCountOffset, elementCountRange, data);
    }

    BufferLock glGenericVertexData::updateIndexBuffer(U32 elementCountOffset, U32 elementCountRange, bufferPtr data)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        return updateBuffer(_indexBuffer, elementCountOffset, elementCountRange, data);
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
