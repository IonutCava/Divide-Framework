#include "stdafx.h"

#include "Headers/vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Core/Headers/StringHelper.h"

namespace Divide {
AllocatedBuffer::~AllocatedBuffer()
{
    if (_buffer != VK_NULL_HANDLE) {
        assert(_usageType != BufferUsageType::COUNT);
        VK_API::RegisterCustomAPIDelete([buf = _buffer, alloc = _allocation]([[maybe_unused]] VkDevice device) {
            UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
            vmaDestroyBuffer(*VK_API::GetStateTracker()->_allocatorInstance._allocator, buf, alloc);
        }, true);
    }
}

VertexInputDescription getVertexDescription(const AttributeMap& vertexFormat) {
    VertexInputDescription description;

    for (U8 idx = 0u; idx < to_base(AttribLocation::COUNT); ++idx) {
        const AttributeDescriptor& descriptor = vertexFormat[idx];
        if (descriptor._dataType == GFXDataFormat::COUNT) {
            continue;
        }

        VkVertexInputBindingDescription mainBinding = {};
        mainBinding.binding = descriptor._bindingIndex;
        mainBinding.stride = to_U32(descriptor._strideInBytes);
        mainBinding.inputRate = descriptor._perVertexInputRate ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
        description.bindings.push_back(mainBinding);

        VkVertexInputAttributeDescription attribute = {};
        attribute.binding = descriptor._bindingIndex;
        attribute.location = idx;
        attribute.format = VKUtil::internalFormat(descriptor._dataType, descriptor._componentsPerElement, descriptor._normalized);
        attribute.offset = to_U32(descriptor._strideInBytes);

        description.attributes.push_back(attribute);
    }

    return description;
}
namespace VKUtil {

AllocatedBuffer_uptr createStagingBuffer(const size_t size, std::string_view bufferName) {
    AllocatedBuffer_uptr ret = eastl::make_unique<AllocatedBuffer>(BufferUsageType::STAGING_BUFFER);

    VmaAllocationCreateInfo vmaallocInfo = {};
    // Let the VMA library know that this data should be writable by CPU only
    vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    vmaallocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Allocate staging buffer
    const VkBufferCreateInfo bufferInfo = vk::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size);
    // Allocate the buffer
    UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
    VK_CHECK(vmaCreateBuffer(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                             &bufferInfo,
                             &vmaallocInfo,
                             &ret->_buffer,
                             &ret->_allocation,
                             &ret->_allocInfo));

    Debug::SetObjectName(VK_API::GetStateTracker()->_device->getVKDevice(), (uint64_t)ret->_buffer, VK_OBJECT_TYPE_BUFFER, Util::StringFormat("%s_staging_buffer", bufferName.data()).c_str());

    return ret;
}
} //namespace VKUtil
}; //namespace Divide