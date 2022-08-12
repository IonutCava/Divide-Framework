#include "stdafx.h"

#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide {
    VKDevice::VKDevice(vkb::Instance& instance, VkSurfaceKHR targetSurface)
    {
        vkb::PhysicalDeviceSelector selector{ instance };
        auto physicalDeviceSelection = selector.set_minimum_version(1, Config::MINIMUM_VULKAN_MINOR_VERSION)
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

        _graphicsQueue = getQueue(vkb::QueueType::graphics);
        _computeQueue = getQueue(vkb::QueueType::compute);
        _transferQueue = getQueue(vkb::QueueType::transfer);

        _graphicsCommandPool = createCommandPool(_graphicsQueue._queueIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    }

    VKDevice::~VKDevice()
    {
        if (_graphicsCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(getVKDevice(), _graphicsCommandPool, nullptr);
        }
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

    VkCommandPool VKDevice::createCommandPool(const uint32_t queueFamilyIndex, const VkCommandPoolCreateFlags createFlags) {
        VkCommandPoolCreateInfo cmdPoolInfo = vk::commandPoolCreateInfo();
        cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
        cmdPoolInfo.flags = createFlags;
        VkCommandPool cmdPool;
        VK_CHECK(vkCreateCommandPool(getVKDevice(), &cmdPoolInfo, nullptr, &cmdPool));
        return cmdPool;
    }

    /**
    * Allocate a command buffer from the command pool
    *
    * @param level Level of the new command buffer (primary or secondary)
    * @param pool Command pool from which the command buffer will be allocated
    * @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
    *
    * @return A handle to the allocated command buffer
    */
    VkCommandBuffer VKDevice::createCommandBuffer(const VkCommandBufferLevel level, const VkCommandPool pool, const bool begin) {
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = vk::commandBufferAllocateInfo(pool, level, 1);

        VkCommandBuffer cmdBuffer;
        VK_CHECK(vkAllocateCommandBuffers(getVKDevice(), &cmdBufAllocateInfo, &cmdBuffer));
        // If requested, also start recording for the new command buffer
        if (begin) {
            VkCommandBufferBeginInfo cmdBufferBeginInfo = vk::commandBufferBeginInfo();
            VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));
        }
        return cmdBuffer;
    }

    VkCommandBuffer VKDevice::createCommandBuffer(const VkCommandBufferLevel level, const bool begin) {
        return createCommandBuffer(level, _graphicsCommandPool, begin);
    }

    /**
    * Finish command buffer recording and submit it to a queue
    *
    * @param commandBuffer Command buffer to flush
    * @param queue Queue to submit the command buffer to
    * @param pool Command pool on which the command buffer has been created
    * @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
    *
    * @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
    * @note Uses a fence to ensure command buffer has finished executing
    */
    void VKDevice::flushCommandBuffer(const VkCommandBuffer commandBuffer, const VkQueue queue, const VkCommandPool pool, const bool free) {
        if (commandBuffer == VK_NULL_HANDLE) {
            return;
        }

        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo = vk::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo = vk::fenceCreateInfo();

        VkFence fence;
        VK_CHECK(vkCreateFence(getVKDevice(), &fenceInfo, nullptr, &fence));
        // Submit to the queue
        VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK(vkWaitForFences(getVKDevice(), 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(getVKDevice(), fence, nullptr);
        if (free) {
            vkFreeCommandBuffers(getVKDevice(), pool, 1, &commandBuffer);
        }
    }

    void VKDevice::flushCommandBuffer(const VkCommandBuffer commandBuffer, const VkQueue queue, const bool free) {
        return flushCommandBuffer(commandBuffer, queue, _graphicsCommandPool, free);
    }

}; //namespace Divide
