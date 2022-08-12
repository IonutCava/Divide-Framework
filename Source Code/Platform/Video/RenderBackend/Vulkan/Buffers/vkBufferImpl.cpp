#include "stdafx.h"

#include "Headers/vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

namespace Divide {
AllocatedBuffer::~AllocatedBuffer()
{
    if (_buffer != VK_NULL_HANDLE) {
        assert(_usageType != BufferUsageType::COUNT);
        vmaDestroyBuffer(*VK_API::GetStateTracker()->_allocator, _buffer, _allocation);
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

}; //namespace Divide