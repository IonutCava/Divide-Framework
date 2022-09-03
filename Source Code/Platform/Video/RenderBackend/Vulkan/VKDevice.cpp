#include "stdafx.h"

#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
    VKDevice::VKDevice(VK_API& context, vkb::Instance& instance, VkSurfaceKHR targetSurface)
        : _context(context)
    {
        vkb::PhysicalDeviceSelector selector{ instance };
        auto physicalDeviceSelection = selector.set_minimum_version(1, Config::MINIMUM_VULKAN_MINOR_VERSION)
                                               .set_surface(targetSurface)
                                               .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                               //.add_required_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
                                               .add_required_extension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)
                                               .add_required_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
                                               .select();
        if (!physicalDeviceSelection) {
            Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), physicalDeviceSelection.error().message().c_str());
        } else {
            _physicalDevice = physicalDeviceSelection.value();
            
            //create the final Vulkan device
            vkb::DeviceBuilder deviceBuilder{ _physicalDevice };

            VkPhysicalDeviceVulkan13Features vk13features{};
            vk13features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            vk13features.synchronization2 = true;
            vk13features.dynamicRendering = true;
            vk13features.maintenance4 = true;
            VkPhysicalDeviceVulkan12Features vk12features{};
            vk12features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            vk12features.samplerMirrorClampToEdge = true;
            vk12features.timelineSemaphore = true;
            vk12features.descriptorBindingPartiallyBound = true;
            vk12features.descriptorBindingUniformBufferUpdateAfterBind = true;
            vk12features.descriptorBindingSampledImageUpdateAfterBind = true;
            vk12features.descriptorBindingStorageImageUpdateAfterBind = true;
            vk12features.descriptorBindingStorageBufferUpdateAfterBind = true;
            vk12features.descriptorBindingUpdateUnusedWhilePending = true;
            vk12features.shaderSampledImageArrayNonUniformIndexing = true;
            vk12features.runtimeDescriptorArray = true;
            vk12features.descriptorBindingVariableDescriptorCount = true;
            VkPhysicalDeviceVulkan11Features vk11features{};
            vk11features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            vk11features.shaderDrawParameters = true;
            VkPhysicalDeviceSynchronization2FeaturesKHR sync_feat{};
            sync_feat.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
            sync_feat.synchronization2 = true;
            deviceBuilder.add_pNext(&vk11features)
                         .add_pNext(&vk12features)
                         .add_pNext(&vk13features);

            auto vkbDevice = deviceBuilder.build();
            if (!vkbDevice) {
                Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), vkbDevice.error().message().c_str());
            } else {
                // Get the VkDevice handle used in the rest of a Vulkan application
                _device = vkbDevice.value();
            }

            _queues[to_base(vkb::QueueType::graphics)] = getQueue(vkb::QueueType::graphics);
            _queues[to_base(vkb::QueueType::compute)] = getQueue(vkb::QueueType::compute);
            _queues[to_base(vkb::QueueType::transfer)] = getQueue(vkb::QueueType::transfer);
            _queues[to_base(vkb::QueueType::present)] = getQueue(vkb::QueueType::present);

            _graphicsCommandPool = createCommandPool(_queues[to_base(vkb::QueueType::graphics)]._queueIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        }
    }

    VKDevice::~VKDevice()
    {
        if (_graphicsCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(getVKDevice(), _graphicsCommandPool, nullptr);
        }
        vkb::destroy_device(_device);
    }

    VKQueue VKDevice::getQueue(vkb::QueueType type) const noexcept {
        VKQueue ret{};

        if (getDevice() != nullptr) {
            // use vkbootstrap to get a Graphics queue
            const auto queue = _device.get_queue(type);
            if (queue) {
                ret._queue = queue.value();
                ret._queueIndex = _device.get_queue_index(type).value();
            } else {
                Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), queue.error().message().c_str());
            }
        }

        return ret;
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

    VkCommandPool VKDevice::createCommandPool(const uint32_t queueFamilyIndex, const VkCommandPoolCreateFlags createFlags) {
        VkCommandPoolCreateInfo cmdPoolInfo = vk::commandPoolCreateInfo();
        cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
        cmdPoolInfo.flags = createFlags;
        VkCommandPool cmdPool;
        VK_CHECK(vkCreateCommandPool(getVKDevice(), &cmdPoolInfo, nullptr, &cmdPool));
        return cmdPool;
    }

    void VKDevice::submitToQueue(const vkb::QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence) const {
        // Submit command buffer to the queue and execute it.
        // "fence" will now block until the graphic commands finish execution
        UniqueLock<Mutex> w_lock(_queueLocks[to_base(queue)]);
        VK_CHECK(vkQueueSubmit(_queues[to_base(queue)]._queue, 1, &submitInfo, fence));
    }

    void VKDevice::submitToQueueAndWait(const vkb::QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence) const {
        submitToQueue(queue, submitInfo, fence);

        vkWaitForFences(getVKDevice(), 1, &fence, true, 9999999999);
        vkResetFences(getVKDevice(), 1, &fence);
    }

    VkResult VKDevice::queuePresent(const vkb::QueueType queue, const VkPresentInfoKHR& presentInfo) const {
        UniqueLock<Mutex> w_lock(_queueLocks[to_base(queue)]);
        return vkQueuePresentKHR(_queues[to_base(queue)]._queue, &presentInfo);
    }

    U32 VKDevice::getQueueIndex(const vkb::QueueType queue) const {
        return _queues[to_base(queue)]._queueIndex;
    }
}; //namespace Divide
