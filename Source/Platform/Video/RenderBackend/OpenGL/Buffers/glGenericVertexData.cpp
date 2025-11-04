

#include "Headers/glGenericVertexData.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{
    glGPUBuffer::glGPUBuffer( GFXDevice& context, const U16 ringBufferLength, const std::string_view name )
        : GPUBuffer( context, ringBufferLength, name )
    {
        firstIndexOffsetCount(INVALID_INDEX_OFFSET);
    }

    /// Specify the structure and data of the given buffer
    BufferLock glGPUBuffer::setBuffer( const SetBufferParams& params )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( params._usageType != BufferUsageType::COUNT );

        BufferLock ret = GPUBuffer::setBuffer( params );

        LockGuard<SharedMutex> w_lock(_bufferLock);
        if (_internalBuffer != nullptr)
        {
            const auto& existingParams = _internalBuffer->_params;
            if (params._elementCount == 0u || // We don't need indices anymore
                existingParams._elementCount < params._elementCount || // Buffer not big enough
                existingParams._updateFrequency != params._updateFrequency || // Buffer update frequency changed
                existingParams._elementSize != params._elementSize)  //Different element size
            {
                _internalBuffer.reset();
            }
        }

        if ( params._usageType == BufferUsageType::INDEX_BUFFER &&
             params._elementCount == 0u)
        {
            firstIndexOffsetCount(0u);
            return ret;
        }
   
        const size_t ringSizeFactor = queueLength();
        const size_t bufferSizeInBytes = params._elementCount * params._elementSize;
        const bufferPtr data = params._initialData.first;

        const bool isIndexBuffer = params._usageType == BufferUsageType::INDEX_BUFFER;

        BufferImplParams implParams;
        implParams._target = isIndexBuffer ? gl::GL_ELEMENT_ARRAY_BUFFER : gl46core::GL_ARRAY_BUFFER;
        implParams._usageType = params._usageType;
        implParams._hostVisible = params._hostVisible;
        implParams._updateFrequency = params._updateFrequency;
        implParams._elementCount = params._elementCount;
        implParams._elementSize = params._elementSize;
        implParams._dataSize = bufferSizeInBytes * ringSizeFactor;


        if (_internalBuffer != nullptr &&
            _internalBuffer->_params._elementSize == implParams._elementSize &&
            _internalBuffer->_params._hostVisible == implParams._hostVisible &&
            _internalBuffer->_params._updateFrequency == implParams._updateFrequency &&
            _internalBuffer->_params._dataSize >= implParams._dataSize)
        {
            return updateBuffer( 0, params._elementCount, params._initialData.first );
        }

        _internalBuffer = std::make_unique<glBufferImpl>(_context,
                                                         implParams,
                                                         std::make_pair(data, data == nullptr ? 0u : bufferSizeInBytes),
                                                         _name.empty() ? Util::StringFormat("Generic_GL_{}_buffer_{}", isIndexBuffer ? "IDX" : "VB", getGUID()).c_str() : _name.c_str() );

        for ( U32 i = 1u; i < ringSizeFactor; ++i )
        {
            _internalBuffer->writeOrClearBytes(i * bufferSizeInBytes,
                                               params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes,
                                               data );
        }

        firstIndexOffsetCount(_internalBuffer->getDataOffset() / implParams._elementSize);

        ret._range = {0u, bufferSizeInBytes * ringSizeFactor};
        ret._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ;
        ret._buffer = _internalBuffer.get();
        return ret;
    }

    /// Update the elementCount worth of data contained in the buffer starting from elementCountOffset size offset
    BufferLock glGPUBuffer::updateBuffer( const U32 elementCountOffset,
                                          const U32 elementCountRange,
                                          const bufferPtr data )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * _internalBuffer->_params._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * _internalBuffer->_params._elementSize;

        DIVIDE_GPU_ASSERT(offsetInBytes + dataCurrentSizeInBytes <= _internalBuffer->_params._elementCount * _internalBuffer->_params._elementSize);

        if (queueLength() > 1u)
        {
            offsetInBytes += _internalBuffer->_params._elementCount * _internalBuffer->_params._elementSize * queueIndex();
        }

        return _internalBuffer->writeOrClearBytes(offsetInBytes, dataCurrentSizeInBytes, data);
    }

};
