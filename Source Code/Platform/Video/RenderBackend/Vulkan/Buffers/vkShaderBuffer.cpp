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
                "ERROR: glShaderBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit");
        } else {
            DIVIDE_ASSERT(_params._elementSize == targetElementSize,
                "ERROR: glShaderBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit");
        }

        _alignedBufferSize = _params._elementCount * _params._elementSize;
        _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset(_alignedBufferSize, AlignmentRequirement(_usage)));

        const VkBufferUsageFlagBits usageFlags = (_usage == Usage::UNBOUND_BUFFER || _usage == Usage::COMMAND_BUFFER) 
                                                        ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                        : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        const size_t dataSize = _alignedBufferSize * queueLength();
        _bufferImpl = eastl::make_unique<AllocatedBuffer>();
        _bufferImpl->_params = _params;
        _bufferImpl->_usageType = BufferUsageType::SHADER_BUFFER;

        // allocate vertex buffer
        VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(usageFlags, dataSize);

        //let the VMA library know that this data should be writeable by CPU, but also readable by GPU
        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        //allocate the buffer
        VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocator,
                                 &bufferInfo,
                                 &vmaallocInfo,
                                 &_bufferImpl->_buffer,
                                 &_bufferImpl->_allocation,
                                 &_bufferImpl->_allocInfo));

        VkMemoryPropertyFlags memPropFlags;
        vmaGetAllocationMemoryProperties(*VK_API::GetStateTracker()->_allocator,
                                         _bufferImpl->_allocation,
                                         &memPropFlags);

        const string bufferName = _name.empty() ? Util::StringFormat("DVD_GENERAL_SHADER_BUFFER_%zu", getGUID()) : (_name + "_SHADER_BUFFER");
        vmaSetAllocationName(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation, bufferName.c_str());

        void* mappedData;
        vmaMapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation, &mappedData);
        Byte* mappedRange = (Byte*)mappedData;

        for (U32 i = 0u; i < descriptor._ringBufferLength; ++i) {
            if (descriptor._initialData.first == nullptr) {
                memset(&mappedRange[i * _alignedBufferSize], 0, _alignedBufferSize);
            } else {
                memcpy(&mappedRange[i * _alignedBufferSize], descriptor._initialData.first, descriptor._initialData.second > 0 ? descriptor._initialData.second : _alignedBufferSize);
            }
        }
        vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation);
    }

    BufferLock vkShaderBuffer::clearBytes(BufferRange range) noexcept {
        DIVIDE_ASSERT(range._length > 0);
        OPTICK_EVENT();

        DIVIDE_ASSERT(range._startOffset == Util::GetAlignmentCorrected((range._startOffset), AlignmentRequirement(_usage)));
        assert(range._startOffset + range._length <= _alignedBufferSize && "glShaderBuffer::UpdateData error: was called with an invalid range (buffer overflow)!");

        range._startOffset += queueWriteIndex() * _alignedBufferSize;

        void* mappedData;
        vmaMapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation, &mappedData);
        Byte* mappedRange = (Byte*)mappedData;
        memset(&mappedRange[range._startOffset], 0u, range._length);
        vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation);

        return { this, range };
    }

    BufferLock vkShaderBuffer::writeBytes(BufferRange range, bufferPtr data) noexcept {
        DIVIDE_ASSERT(range._length > 0);
        OPTICK_EVENT();

        DIVIDE_ASSERT(range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));
        range._startOffset += queueWriteIndex() * _alignedBufferSize;

        void* mappedData;
        vmaMapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation, &mappedData);
        Byte* mappedRange = (Byte*)mappedData;
        memcpy(&mappedRange[range._startOffset], data, range._length);
        vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation);

        return { this, range };
    }

    void vkShaderBuffer::readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData) const noexcept {
        if (range._length > 0) {
            OPTICK_EVENT();

            DIVIDE_ASSERT(range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));
            range._startOffset += queueReadIndex() * _alignedBufferSize;

            void* mappedData;
            vmaMapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation, &mappedData);
            Byte* mappedRange = (Byte*)mappedData;
            memcpy(outData.first, &mappedRange[range._startOffset], std::min(std::min(range._length, outData.second), _alignedBufferSize));
            vmaUnmapMemory(*VK_API::GetStateTracker()->_allocator, _bufferImpl->_allocation);
        }
    }

    bool vkShaderBuffer::bindByteRange([[maybe_unused]] U8 bindIndex, [[maybe_unused]] BufferRange range) noexcept {
        return true;
    }

    bool vkShaderBuffer::lockByteRange([[maybe_unused]] BufferRange range, [[maybe_unused]] SyncObject* sync) const {
        return true;
    }
}; //namespace Divide
