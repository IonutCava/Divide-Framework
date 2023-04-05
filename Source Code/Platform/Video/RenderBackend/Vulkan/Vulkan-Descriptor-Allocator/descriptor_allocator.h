/*
  MIT License
  
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
#ifndef _DESCRIPTOR_ALLOCATOR_H_
#define _DESCRIPTOR_ALLOCATOR_H_

#include <stdint.h>
#include <vulkan/vulkan_core.h>

namespace vke {

    class DescriptorAllocatorPool;

    struct DescriptorAllocatorHandle
    {
        friend class DescriptorAllocatorPool;
        DescriptorAllocatorHandle() = default;
        DescriptorAllocatorHandle& operator=(const DescriptorAllocatorHandle&) = delete;

        ~DescriptorAllocatorHandle();
        DescriptorAllocatorHandle(DescriptorAllocatorHandle&& other);
        DescriptorAllocatorHandle& operator=(DescriptorAllocatorHandle&& other);

        /// Return this handle to the pool. Will make this handle orphaned.
        void Return();

        /// Allocate new descriptor. handle has to be valid returns true if allocation succeeded, and false if it didn't will mutate the handle if it requires a new vkDescriptorPool.
        [[nodiscard]] bool Allocate(const VkDescriptorSetLayout& layout, VkDescriptorSet& builtSet, int8_t retryCount = 0);

        DescriptorAllocatorPool* ownerPool{nullptr};
        VkDescriptorPool vkPool{VK_NULL_HANDLE};
        int32_t poolIdx{-1};
    };

    class DescriptorAllocatorPool
    {
    public:
        virtual ~DescriptorAllocatorPool(){};

        static [[nodiscard]] DescriptorAllocatorPool* Create(const VkDevice& device, int32_t nFrames = 3);

        /// Not thread safe! Switches default allocators to the next frame. When frames loop it will reset the descriptors of that frame.
        virtual void Flip() = 0;

        /// Not thread safe! Override the pool size for a specific descriptor type. This will be used new pools are allocated.
        virtual void SetPoolSizeMultiplier(VkDescriptorType type, float multiplier) = 0;

        // Thread safe, uses lock! Get handle to use when allocating descriptors.
        virtual [[nodiscard]] DescriptorAllocatorHandle GetAllocator() = 0;
    };
}

#endif //_DESCRIPTOR_ALLOCATOR_H_
