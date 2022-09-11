#include "stdafx.h"

#include "Headers/vkShaderBuffer.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Core/Headers/StringHelper.h"

namespace Divide {
    vkShaderBuffer::vkShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
        : ShaderBuffer(context, descriptor)
    {
        const size_t targetElementSize = Util::GetAlignmentCorrected(_params._elementSize, AlignmentRequirement(_usage));
        if (targetElementSize > _params._elementSize) {
            DIVIDE_ASSERT((_params._elementSize * _params._elementCount) % AlignmentRequirement(_usage) == 0u,
                "ERROR: vkShaderBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit");
        } else {
            DIVIDE_ASSERT(_params._elementSize == targetElementSize,
                "ERROR: vkShaderBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit");
        }

        _alignedBufferSize = _params._elementCount * _params._elementSize;
        _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset(_alignedBufferSize, AlignmentRequirement(_usage)));

        const VkBufferUsageFlagBits usageFlags = (_usage == Usage::UNBOUND_BUFFER || _usage == Usage::COMMAND_BUFFER) 
                                                    ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                    : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        const size_t dataSize = _alignedBufferSize * queueLength();
        _bufferImpl = eastl::make_unique<AllocatedBuffer>(BufferUsageType::SHADER_BUFFER);
        _bufferImpl->_params = _params;

        _stagingBuffer = VKUtil::createStagingBuffer(dataSize);
        Byte* mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;

        for (U32 i = 0u; i < queueLength(); ++i) {
            if (descriptor._initialData.first == nullptr) {
                memset(&mappedRange[i * _alignedBufferSize], 0, _alignedBufferSize);
            } else {
                memcpy(&mappedRange[i * _alignedBufferSize], descriptor._initialData.first, descriptor._initialData.second > 0 ? descriptor._initialData.second : _alignedBufferSize);
            }
        }

        // Let the VMA library know that this buffer should be readable by the GPU only
        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocInfo.flags = 0;
        if (_params._updateFrequency == BufferUpdateFrequency::OFTEN) {
            // If we write to this buffer often (e.g. GPU uniform blocks), we might as well persistently map it and use
            // a lock manager to protect writes (same as GL_API's lockManager system)
            // A staging buffer is just way too slow for multiple writes per frame.
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            vmaallocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        } else if (_params._hostVisible) {
            vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }

        // Allocate the shader buffer
        {
            const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dataSize);

            UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
            VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                     &bufferInfo,
                                     &vmaallocInfo,
                                     &_bufferImpl->_buffer,
                                     &_bufferImpl->_allocation,
                                     &_bufferImpl->_allocInfo));

            VkMemoryPropertyFlags memPropFlags;
            vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                             _bufferImpl->_allocation,
                                             &memPropFlags);

            const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_SHADER_BUFFER_%zu", getGUID()) : (_name + "_SHADER_BUFFER");
            vmaSetAllocationName(*VK_API::GetStateTracker()->_allocatorInstance._allocator, _bufferImpl->_allocation, bufferName.c_str());
        }

        // Queue a command to copy from the staging buffer to the vertex buffer
        VKTransferQueue::TransferRequest request{};
        request.srcOffset = 0u;
        request.dstOffset = 0u;
        request.size = dataSize;
        request.srcBuffer = _stagingBuffer->_buffer;
        request.dstBuffer = _bufferImpl->_buffer;
        request.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        request._immediate = true;
        VK_API::RegisterTransferRequest(request);

        if (_params._updateFrequency == BufferUpdateFrequency::ONCE ||
            _params._updateFrequency == BufferUpdateFrequency::OFTEN)
        {
            _stagingBuffer.reset();
        }
    }

    BufferLock vkShaderBuffer::clearBytes(BufferRange range) noexcept {
        return writeBytes(range, nullptr);
    }

    BufferLock vkShaderBuffer::writeBytes(BufferRange range, bufferPtr data) noexcept {
        DIVIDE_ASSERT(range._length > 0 && _params._updateFrequency != BufferUpdateFrequency::ONCE);
        OPTICK_EVENT();

        UniqueLock<Mutex> w_lock(_stagingBufferLock);

        DIVIDE_ASSERT(range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));
        range._startOffset += getStartOffset(false);

        Byte* mappedRange = nullptr;
        if (_params._updateFrequency != BufferUpdateFrequency::OFTEN) {
            mappedRange = (Byte*)_stagingBuffer->_allocInfo.pMappedData;
        } else {
            if (!_lockManager.waitForLockedRange(range._startOffset, range._length)) {
                DIVIDE_UNEXPECTED_CALL();
            }
            mappedRange = (Byte*)_bufferImpl->_allocInfo.pMappedData;
        }

        if (data == nullptr) {
            memcpy(&mappedRange[range._startOffset], data, range._length);
        } else {
            memset(&mappedRange[range._startOffset], 0, range._length);
        }

        if (_params._updateFrequency == BufferUpdateFrequency::OFTEN) {
            if (!_lockManager.lockRange(range._startOffset, range._length, _lockManager.createSyncObject())) {
                DIVIDE_UNEXPECTED_CALL();
            }
        } else {
            // Queue a command to copy from the staging buffer to the vertex buffer
            VKTransferQueue::TransferRequest request{};
            request.srcOffset = range._startOffset;
            request.dstOffset = range._startOffset;
            request.size = range._length;
            request.srcBuffer = _stagingBuffer->_buffer;
            request.dstBuffer = _bufferImpl->_buffer;
            request.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            request.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            VK_API::RegisterTransferRequest(request);
        }

        return { this, range };
    }

    void vkShaderBuffer::readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData) const noexcept {
        OPTICK_EVENT();

        DIVIDE_ASSERT(_usage == ShaderBuffer::Usage::UNBOUND_BUFFER &&
                      _params._hostVisible &&
                       range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));

        if (range._length > 0) {
            range._startOffset += getStartOffset(true);

            UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);

            if (_params._updateFrequency == BufferUpdateFrequency::OFTEN) {
                Byte* mappedRange = (Byte*)_bufferImpl->_allocInfo.pMappedData;
                memcpy(outData.first, &mappedRange[range._startOffset], std::min(std::min(range._length, outData.second), _alignedBufferSize));
            } else {
                void* mappedData;
                vmaMapMemory(*VK_API::GetStateTracker()->_allocatorInstance._allocator, _bufferImpl->_allocation, &mappedData);
                Byte* mappedRange = (Byte*)mappedData;
                memcpy(outData.first, &mappedRange[range._startOffset], std::min(std::min(range._length, outData.second), _alignedBufferSize));
                vmaUnmapMemory(*VK_API::GetStateTracker()->_allocatorInstance._allocator, _bufferImpl->_allocation);
            }
        }
    }

}; //namespace Divide
