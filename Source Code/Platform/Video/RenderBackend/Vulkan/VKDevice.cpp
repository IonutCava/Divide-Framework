#include "stdafx.h"

#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
    VKDevice::VKDevice(vkb::Instance& instance, VkSurfaceKHR targetSurface)
    {
        vkb::PhysicalDeviceSelector selector{ instance };
        auto physicalDeviceSelection = selector.set_minimum_version(1, 2)
                                               .set_desired_version(1, 3)
                                               .set_surface(targetSurface)
                                               .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                               .select();
        if (!physicalDeviceSelection) {
            Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), physicalDeviceSelection.error().message().c_str());
        } else {
            _physicalDevice = physicalDeviceSelection.value();

            //create the final Vulkan device
            vkb::DeviceBuilder deviceBuilder{ _physicalDevice };
            //deviceBuilder.add_pNext(extra_features)

            auto vkbDevice = deviceBuilder.build();
            if (!vkbDevice) {
                Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), vkbDevice.error().message().c_str());
            } else {
                // Get the VkDevice handle used in the rest of a Vulkan application
                _device = vkbDevice.value();
            }
        }
    }

    VKDevice::~VKDevice()
    {
        vkb::destroy_device(_device);
    }

    void VKDevice::waitIdle() const {
        vkDeviceWaitIdle(getDevice());
    }

    VKDevice::Queue VKDevice::getQueue(vkb::QueueType type) const noexcept {
        if (getDevice() != nullptr) {
            // use vkbootstrap to get a Graphics queue
            auto queue = _device.get_queue(type);
            if (!queue) {
                Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), queue.error().message().c_str());
            } else {
                return {
                    queue.value(),
                    _device.get_queue_index(type).value()
                };
            }
        }

        return {};
    }

    VkDevice VKDevice::getVKDevice() const noexcept {
        return _device.device;
    }

    VkPhysicalDevice VKDevice::getVKPhysicalDevice() const noexcept {
        return _physicalDevice.physical_device;
    }

    const vkb::Device& VKDevice::getDevice() const noexcept {
        return _device;
    }

    const vkb::PhysicalDevice& VKDevice::getPhysicalDevice() const noexcept {
        return _physicalDevice;
    }
}; //namespace Divide
