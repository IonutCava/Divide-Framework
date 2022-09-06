/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

/*
* MIT License

Copyright (c) 2020 vblanco20-1

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#ifndef _VK_DESCRIPTORS_H_
#define _VK_DESCRIPTORS_H_

#include "vkResources.h"

namespace Divide {

//ref: https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/
class DescriptorAllocator {
public:
    struct PoolSizes {
        vector<std::pair<VkDescriptorType, float>> sizes =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
        };
    };

    explicit DescriptorAllocator(VkDevice device);
    ~DescriptorAllocator();

    void resetPools();
    bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);

    PROPERTY_R_IW(VkDevice, device, VK_NULL_HANDLE);
private:
    VkDescriptorPool grabPool();

private:
    VkDescriptorPool _currentPool{ VK_NULL_HANDLE };
    PoolSizes _descriptorSizes;
    vector<VkDescriptorPool> _usedPools;
    vector<VkDescriptorPool> _freePools;
};

FWD_DECLARE_MANAGED_CLASS(DescriptorAllocator);

class DescriptorLayoutCache {
public:
    explicit DescriptorLayoutCache(VkDevice device);
    ~DescriptorLayoutCache();

    VkDescriptorSetLayout createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info);

    struct DescriptorLayoutInfo {
        vector<VkDescriptorSetLayoutBinding> bindings;
        size_t GetHash() const;
        bool operator==(const DescriptorLayoutInfo& other) const;
    };

private:
    struct DescriptorLayoutHash {
        std::size_t operator()(const DescriptorLayoutInfo& k) const {
            return k.GetHash();
        }
    };

    hashMap<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> _layoutCache;
    VkDevice _device{ VK_NULL_HANDLE };
};

FWD_DECLARE_MANAGED_CLASS(DescriptorLayoutCache);

class DescriptorBuilder {
public:

    static DescriptorBuilder Begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator);

    DescriptorBuilder& bindBuffer(U32 binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);
    DescriptorBuilder& bindImage(U32 binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

    bool build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
    bool build(VkDescriptorSet& set);
private:

    vector<VkWriteDescriptorSet> writes;
    vector<VkDescriptorSetLayoutBinding> bindings;

    DescriptorLayoutCache* cache{ nullptr };
    DescriptorAllocator* alloc{ nullptr };
};
} //namespace Divide

#endif //_VK_DESCRIPTORS_H_
