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

#pragma once
#ifndef _VK_DEVICE_H_
#define _VK_DEVICE_H_

#include "vkResources.h"

namespace Divide {

class VK_API;

class VKDevice final : NonCopyable, NonMovable
{
    public:
        VKDevice(VK_API& context, vkb::Instance& instance, VkSurfaceKHR targetSurface);
        ~VKDevice();

        [[nodiscard]] VkDevice getVKDevice() const noexcept;
        [[nodiscard]] VkPhysicalDevice getVKPhysicalDevice() const noexcept;

        [[nodiscard]] const vkb::Device& getDevice() const noexcept;
        [[nodiscard]] const vkb::PhysicalDevice& getPhysicalDevice() const noexcept;

        [[nodiscard]] VkCommandPool   createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) const;
                      void            submitToQueue(QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence) const;
        [[nodiscard]] VkResult        queuePresent(QueueType queue, const VkPresentInfoKHR& presentInfo) const;

        [[nodiscard]] U32     getPresentQueueIndex() const noexcept;
        [[nodiscard]] VKQueue getQueue( QueueType type ) const noexcept;

    private:
        [[nodiscard]] VKQueue getQueueInternal( QueueType type, bool dedicated) const noexcept;

    private:

        VK_API& _context;
        vkb::Device _device{}; // Vulkan device for commands
        vkb::PhysicalDevice _physicalDevice{}; // GPU chosen as the default device
        U32 _presentQueueIndex{ INVALID_VK_QUEUE_INDEX };

        std::array<VKQueue, to_base( QueueType::COUNT ) > _queues;
        mutable std::array<Mutex, to_base( QueueType::COUNT )> _queueLocks;
};

FWD_DECLARE_MANAGED_CLASS(VKDevice);

}; //namespace Divide
#endif //_VK_DEVICE_H_
