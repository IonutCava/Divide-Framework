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
    struct PoolSize
    {
        VkDescriptorType type;
        float multiplier;
    };

    struct PoolSizes
    {
        Divide::vector_fast<PoolSize> sizes =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1.f },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1.f },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2.f },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2.f },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1.f }
        };
    };

    struct DescriptorAllocator
    {
        VkDescriptorPool pool{ VK_NULL_HANDLE };
    };

    struct PoolStorage
    {
        Divide::vector_fast<DescriptorAllocator> _usableAllocators;
        Divide::vector_fast<DescriptorAllocator> _fullAllocators;
    };

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
        ~DescriptorAllocatorPool();

        static [[nodiscard]] DescriptorAllocatorPool* Create(const VkDevice& device, int32_t nFrames = 3);

        /// Not thread safe! Switches default allocators to the next frame. When frames loop it will reset the descriptors of that frame.
        void Flip();

        /// Not thread safe! Override the pool size for a specific descriptor type. This will be used new pools are allocated.
        void SetPoolSizeMultiplier(VkDescriptorType type, float multiplier);

        // Thread safe, uses lock! Get handle to use when allocating descriptors.
        [[nodiscard]] DescriptorAllocatorHandle GetAllocator();

    protected:
        friend struct DescriptorAllocatorHandle;
        void ReturnAllocator( DescriptorAllocatorHandle& handle, bool bIsFull );
        VkDescriptorPool createPool( int count, VkDescriptorPoolCreateFlags flags );

    private:
        VkDevice _device{ 0 };
        PoolSizes _poolSizes;
        int32_t _frameIndex{ 0 };
        int32_t _maxFrames{ 3 };

        Divide::Mutex _poolMutex;
        Divide::vector_fast<eastl::unique_ptr<PoolStorage>> _descriptorPools;
        Divide::vector_fast<DescriptorAllocator> _clearAllocators;
    };
}

#endif //_DESCRIPTOR_ALLOCATOR_H_
