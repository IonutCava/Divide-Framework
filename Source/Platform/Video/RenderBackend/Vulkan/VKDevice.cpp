

#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{
    VKDevice::VKDevice( VK_API& context, vkb::Instance& instance, VkSurfaceKHR targetSurface )
        : _context( context )
    {
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT vk13EXTfeatures{};
        vk13EXTfeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        vk13EXTfeatures.extendedDynamicState3ColorBlendEnable = true;
        vk13EXTfeatures.extendedDynamicState3ColorBlendEquation = true;
        vk13EXTfeatures.extendedDynamicState3ColorWriteMask = true;

        VkPhysicalDeviceVulkan13Features vk13features{};
        vk13features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vk13features.pNext = &vk13EXTfeatures;

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
        VkPhysicalDeviceFeatures vk10features{};
        vk10features.independentBlend = true;
        vk10features.imageCubeArray = true;
        vk10features.geometryShader = true;
        vk10features.tessellationShader = true;
        vk10features.multiDrawIndirect = true;
        vk10features.drawIndirectFirstInstance = true; //???
        vk10features.depthClamp = true;
        vk10features.depthBiasClamp = true;
        vk10features.fillModeNonSolid = true;
        vk10features.depthBounds = true;
        vk10features.samplerAnisotropy = true;
        vk10features.sampleRateShading = true;
        //vk10features.textureCompressionETC2 = true;
        vk10features.textureCompressionBC = true;
        vk10features.shaderClipDistance = true;
        vk10features.shaderCullDistance = true;

        vkb::PhysicalDeviceSelector selector{ instance };
        auto physicalDeviceSelection = selector.set_minimum_version( 1, Config::MINIMUM_VULKAN_MINOR_VERSION )
            //.set_desired_version(1, Config::DESIRED_VULKAN_MINOR_VERSION)
            .set_surface( targetSurface )
            .prefer_gpu_device_type( vkb::PreferredDeviceType::discrete )
            .add_required_extension( VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME )
            .set_required_features( vk10features )
            .set_required_features_11( vk11features )
            .set_required_features_12( vk12features )
            .set_required_features_13( vk13features )
            .select();

        if ( !physicalDeviceSelection )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), physicalDeviceSelection.error().message().c_str() );
        }
        else
        {
            _physicalDevice = physicalDeviceSelection.value();
            _physicalDevice.enable_extensions_if_present(
   {
                 VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
                 VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                 VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME
            }
            );

            //create the final Vulkan device
            vkb::DeviceBuilder deviceBuilder{ _physicalDevice };
            auto vkbDevice = deviceBuilder.build();
            if ( !vkbDevice )
            {
                Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), vkbDevice.error().message().c_str() );
                return;
            }

            for ( const auto& extension : _physicalDevice.get_extensions() )
            {
                if ( extension == VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME )
                {
                    supportsDynamicExtension3(true);
                }
                else if ( extension == VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME )
                {
                    supportsPushDescriptors(true);
                }
                else if ( extension == VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME )
                {
                    supportsDescriptorBuffers(true);
                }
            }

            // Get the VkDevice handle used in the rest of a Vulkan application
            _device = vkbDevice.value();

            const auto presentIndex = _device.get_queue_index(vkb::QueueType::present);
            if (presentIndex )
            {
                _presentQueueIndex = presentIndex.value();

                for ( U8 t = 0u; t < to_base( QueueType::COUNT ); ++t )
                {
                    _queues[t] = getQueueInternal( static_cast<QueueType>(t), true);
                }
            }
            else
            {
                Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), presentIndex.error().message().c_str() );
            }

        }
    }

    VKDevice::~VKDevice()
    {
        for ( VKQueue& queue : _queues )
        {
            if ( queue._pool != VK_NULL_HANDLE )
            {
                vkDestroyCommandPool( getVKDevice(), queue._pool, nullptr );
            }
        }
        
        if ( _device.device != VK_NULL_HANDLE )
        {
            vkb::destroy_device( _device );
        }
    }

    U32 VKDevice::getPresentQueueIndex() const noexcept
    {
        return _presentQueueIndex;
    }

    VKQueue VKDevice::getQueue( QueueType type ) const noexcept
    {
        return _queues[to_base(type)];
    }

    VKQueue VKDevice::getQueueInternal( const QueueType type, bool dedicated ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        constexpr const char* QueueName[] = { "Graphics", "Compute", "Transfer" };
        constexpr vkb::QueueType VKBQueueType[] = {vkb::QueueType::graphics, vkb::QueueType::compute, vkb::QueueType::transfer};

        VKQueue ret{};

        if ( getDevice() == nullptr )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), "VKDevice::getQueueInternal error: no valid device found!");
            return ret;
        }

        if ( type != QueueType::COMPUTE && type != QueueType::TRANSFER )
        {
            dedicated = false;
        }

        const vkb::QueueType vkbType = VKBQueueType[to_base(type)];

        // Dumb way of doing this, but VkBootstrap's Result& operator=(Result&& result) is missing a return statement and causes a compilation error
        const auto index = dedicated ? _device.get_dedicated_queue_index( vkbType ) : _device.get_queue_index( vkbType );
        if ( !index )
        {
            if ( dedicated )
            {
                Console::warnfn( LOCALE_STR( "WARN_VK_DEDICATED_QUEUE" ), QueueName[to_base(type)], index.error().message().c_str() );
            }
            else
            {
                Console::errorfn( LOCALE_STR( "ERROR_VK_DEDICATED_QUEUE" ), QueueName[to_base( type )], index.error().message().c_str() );
            }

            return dedicated ? getQueueInternal(type, false) : ret;
        }

        ret._index = index.value();
        ret._type = type;
        ret._pool = createCommandPool( ret._index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT );
        vkGetDeviceQueue( _device.device, ret._index, 0u, &ret._queue );

        return ret;
    }

    VkDevice VKDevice::getVKDevice() const noexcept
    {
        return _device.device;
    }

    VkPhysicalDevice VKDevice::getVKPhysicalDevice() const noexcept
    {
        return _physicalDevice.physical_device;
    }

    const vkb::Device& VKDevice::getDevice() const noexcept
    {
        return _device;
    }

    const vkb::PhysicalDevice& VKDevice::getPhysicalDevice() const noexcept
    {
        return _physicalDevice;
    }

    VkCommandPool VKDevice::createCommandPool( const uint32_t queueFamilyIndex, const VkCommandPoolCreateFlags createFlags ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VkCommandPoolCreateInfo cmdPoolInfo = vk::commandPoolCreateInfo();
        cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
        cmdPoolInfo.flags = createFlags;
        VkCommandPool cmdPool;
        VK_CHECK( vkCreateCommandPool( getVKDevice(), &cmdPoolInfo, nullptr, &cmdPool ) );
        return cmdPool;
    }

    void VKDevice::submitToQueue( const QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Submit command buffer to the queue and execute it.
        // "fence" will now block until the graphic commands finish execution
        LockGuard<Mutex> w_lock( _queueLocks[to_base( queue )] );
        VK_CHECK( vkQueueSubmit( _queues[to_base( queue )]._queue, 1, &submitInfo, fence ) );
    }

    VkResult VKDevice::queuePresent( const QueueType queue, const VkPresentInfoKHR& presentInfo ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        LockGuard<Mutex> w_lock( _queueLocks[to_base( queue )] );
        return vkQueuePresentKHR( _queues[to_base( queue )]._queue, &presentInfo );
    }

}; //namespace Divide
