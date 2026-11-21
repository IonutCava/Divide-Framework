/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_VK_DEVICE_H_
#define DVD_VK_DEVICE_H_

#include "vkResources.h"

namespace Divide {

class VKDevice final : NonCopyable, NonMovable
{
    public:
        VKDevice( vkb::Instance& instance, VkSurfaceKHR targetSurface);
        ~VKDevice();

        [[nodiscard]] static constexpr bool HasRequiredDynamicState3Features( const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT& features ) noexcept
        {
            return features.extendedDynamicState3ColorBlendEnable &&
                   features.extendedDynamicState3ColorBlendEquation &&
                   features.extendedDynamicState3ColorWriteMask;
        }

        [[nodiscard]] static constexpr bool CanEnableDeviceFault( const bool smokeMode,
                                                                   const bool extensionPresent,
                                                                   const bool featureSupported ) noexcept
        {
            return smokeMode && extensionPresent && featureSupported;
        }

        [[nodiscard]] VkDevice getVKDevice() const noexcept;
        [[nodiscard]] VkPhysicalDevice getVKPhysicalDevice() const noexcept;

        [[nodiscard]] const vkb::Device& getDevice() const noexcept;
        [[nodiscard]] const vkb::PhysicalDevice& getPhysicalDevice() const noexcept;

        [[nodiscard]] VkCommandPool   createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT) const;
        [[nodiscard]] VkResult        submitToQueue(QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence) const;
        [[nodiscard]] VkResult        queuePresent(QueueType queue, const VkPresentInfoKHR& presentInfo) const;
        [[nodiscard]] VkResult        waitIdle() const;
        void writeDeviceFaultInfo( std::FILE* output ) const noexcept;

        [[nodiscard]] static bool SmokeQueueSubmitActive() noexcept;
        [[nodiscard]] static std::FILE* PendingSmokeDeviceLossFailure() noexcept;
        static void SetPendingSmokeDeviceLossFailure( std::FILE* output ) noexcept;

        [[nodiscard]] U32     getPresentQueueIndex() const noexcept;
        [[nodiscard]] VKQueue getQueue( QueueType type ) const noexcept;

        PROPERTY_R_IW(size_t, vulkanMinorVersion, 0u);
        PROPERTY_R_IW(bool, supportsMaintenance7, false);
        PROPERTY_R_IW(bool, supportsDynamicExtension3, false);
        PROPERTY_R_IW(bool, supportsDescriptorBuffers, false);
        PROPERTY_R_IW(bool, supportsMeshShaders, false);

    private:
        [[nodiscard]] VKQueue getQueueInternal( QueueType type, bool dedicated) const noexcept;
        [[nodiscard]] Mutex& queueLock( QueueType type ) const noexcept;

    private:
        struct SmokeFailureState
        {
            bool _queueSubmitActive{ false };
            std::FILE* _deviceLossFailure{ nullptr };
        };

        static thread_local SmokeFailureState s_smokeFailureState;

        vkb::Device _device{}; // Vulkan device for commands
        vkb::PhysicalDevice _physicalDevice{}; // GPU chosen as the default device
        U32 _presentQueueIndex{ INVALID_VK_QUEUE_INDEX };
        bool _deviceFaultEnabled{ false };
        bool _deviceFaultVendorBinary{ false };

        std::array<VKQueue, to_base( QueueType::COUNT ) > _queues;
        mutable std::array<Mutex, to_base( QueueType::COUNT )> _queueLocks;
};

FWD_DECLARE_MANAGED_CLASS(VKDevice);

}; //namespace Divide

#endif //DVD_VK_DEVICE_H_
