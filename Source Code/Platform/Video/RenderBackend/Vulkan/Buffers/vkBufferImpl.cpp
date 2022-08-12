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
}; //namespace Divide