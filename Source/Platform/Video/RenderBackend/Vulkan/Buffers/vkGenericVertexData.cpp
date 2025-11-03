

#include "Headers/vkGenericVertexData.h"
#include "Headers/vkBufferImpl.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
    vkGPUBuffer::vkGPUBuffer(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
        : GPUBuffer(context, ringBufferLength, name)
    {
    }

    vkGPUBuffer::~vkGPUBuffer()
    {
        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();
    }

    BufferLock vkGPUBuffer::setBuffer(const SetBufferParams& params) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_GPU_ASSERT( params._usageType != BufferUsageType::COUNT );

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

        firstIndexOffsetCount(0u);

        if (params._usageType == BufferUsageType::INDEX_BUFFER &&
            params._elementCount == 0u)
        {
            return ret;
        }

        const size_t ringSizeFactor = queueLength();
        const size_t bufferSizeInBytes = params._elementCount * params._elementSize;
        const bufferPtr data = params._initialData.first;

        if (_internalBuffer != nullptr &&
            _internalBuffer->_params._elementSize == params._elementSize &&
            _internalBuffer->_params._hostVisible == params._hostVisible &&
            _internalBuffer->_params._updateFrequency == params._updateFrequency &&
            _internalBuffer->_params._elementCount >= params._elementCount)
        {
            return updateBuffer(0, params._elementCount, params._initialData.first);
        }

        const bool isIndexBuffer = params._usageType == BufferUsageType::INDEX_BUFFER;

        _internalBuffer = std::make_unique<vkBufferImpl>(params,
                                                         bufferSizeInBytes,
                                                         ringSizeFactor,
                                                         std::make_pair(data, data == nullptr ? 0u : bufferSizeInBytes),
                                                         _name.empty() ? Util::StringFormat("Generic_VK_{}_buffer_{}", isIndexBuffer ? "IDX" : "VB", getGUID()).c_str() : _name.c_str());

        for (U32 i = 1u; i < ringSizeFactor; ++i)
        {
            const BufferRange<> range = { i * bufferSizeInBytes , params._initialData.second > 0 ? params._initialData.second : bufferSizeInBytes };
            _internalBuffer->writeBytes(range,
                                        isIndexBuffer ? VK_ACCESS_INDEX_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                        data);
        }

        _context.getPerformanceMetrics()._gpuBufferCount = TotalBufferCount();

        ret._range = {0u, bufferSizeInBytes * ringSizeFactor};
        ret._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ;
        ret._buffer = _internalBuffer.get();
        return ret;
    }

    BufferLock vkGPUBuffer::updateBuffer( const U32 elementCountOffset,
                                          const U32 elementCountRange,
                                          bufferPtr data) noexcept
    {
        DIVIDE_GPU_ASSERT(_internalBuffer != nullptr, "vkGenericVertexData error: set buffer called for invalid buffer index!");

        const BufferParams& bufferParams = _internalBuffer->_params;
        const bool isIndexBuffer = bufferParams._usageType == BufferUsageType::INDEX_BUFFER;
        const VkAccessFlags2 dstAccessMask = isIndexBuffer ? VK_ACCESS_INDEX_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

        // Calculate the size of the data that needs updating
        const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
        // Calculate the offset in the buffer in bytes from which to start writing
        size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;
        const size_t bufferSizeInBytes = bufferParams._elementCount * bufferParams._elementSize;
        DIVIDE_GPU_ASSERT(offsetInBytes + dataCurrentSizeInBytes <= bufferSizeInBytes);

        if (queueLength() > 1u)
        {
            offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
        }

        DIVIDE_EXPECTED_CALL(_internalBuffer->waitForLockedRange({ offsetInBytes, dataCurrentSizeInBytes }));

        const BufferRange<> range = { offsetInBytes , dataCurrentSizeInBytes };

        return _internalBuffer->writeBytes(range, dstAccessMask, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, data);
    }


}; //namespace Divide
