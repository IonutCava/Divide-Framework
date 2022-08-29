/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef VK_BUFFER_IMPL_H_
#define VK_BUFFER_IMPL_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VMAInclude.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h"

#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace Divide {
    struct VertexInputDescription {

        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;

        VkPipelineVertexInputStateCreateFlags flags = 0u;
    };

    VertexInputDescription getVertexDescription(const AttributeMap& vertexFormat);

    struct AllocatedBuffer {
        AllocatedBuffer() = default;
        ~AllocatedBuffer();

        VkBuffer _buffer{VK_NULL_HANDLE};
        VmaAllocation _allocation{ VK_NULL_HANDLE };
        VmaAllocationInfo _allocInfo{};
        BufferUsageType _usageType{ BufferUsageType::COUNT };
        BufferParams _params{};
    };
    FWD_DECLARE_MANAGED_STRUCT(AllocatedBuffer);
} //namespace Divide

#endif //VK_BUFFER_IMPL_H_