

#include "Headers/vkDevice.h"

#include "Utility/Headers/Localization.h"

namespace Divide
{
    VKDevice::VKDevice( vkb::Instance& instance, VkSurfaceKHR targetSurface )
    {
        supportsDescriptorBuffers(true);

        VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties{};
        pushDescriptorProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
        pushDescriptorProperties.maxPushDescriptors = 32u;
            
        VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures{};
        descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
        descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
        descriptorBufferFeatures.descriptorBufferPushDescriptors = VK_TRUE;
        // descriptorBufferFeatures.descriptorBufferCaptureReplay = VK_TRUE;

        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
        bufferDeviceAddressFeatures.pNext = &descriptorBufferFeatures;

        VkPhysicalDeviceVulkan14Features vk14features{};
        vk14features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        vk14features.maintenance5 = VK_TRUE;
        vk14features.maintenance6 = VK_TRUE;
        vk14features.pushDescriptor = VK_TRUE;

        VkPhysicalDeviceVulkan13Features vk13features{};
        vk13features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vk13features.synchronization2 = VK_TRUE;
        vk13features.dynamicRendering = VK_TRUE;
        vk13features.inlineUniformBlock = VK_TRUE;
        vk13features.maintenance4 = VK_TRUE;

        VkPhysicalDeviceVulkan12Features vk12features{};
        vk12features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12features.bufferDeviceAddress = VK_TRUE;
        vk12features.samplerMirrorClampToEdge = VK_TRUE;
        vk12features.timelineSemaphore = VK_TRUE;
        vk12features.descriptorBindingPartiallyBound = VK_TRUE;
        vk12features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
        vk12features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vk12features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        vk12features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        vk12features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        vk12features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vk12features.runtimeDescriptorArray = VK_TRUE;
        vk12features.descriptorBindingVariableDescriptorCount = VK_TRUE;

        VkPhysicalDeviceVulkan11Features vk11features{};
        vk11features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vk11features.shaderDrawParameters = VK_TRUE;

        VkPhysicalDeviceFeatures vk10features{};
        vk10features.independentBlend = VK_TRUE;
        vk10features.imageCubeArray = VK_TRUE;
        vk10features.geometryShader = VK_TRUE;
        vk10features.tessellationShader = VK_TRUE;
        vk10features.multiDrawIndirect = VK_TRUE;
        vk10features.drawIndirectFirstInstance = VK_TRUE; //???
        vk10features.depthClamp = VK_TRUE;
        vk10features.depthBiasClamp = VK_TRUE;
        vk10features.fillModeNonSolid = VK_TRUE;
        vk10features.depthBounds = VK_TRUE;
        vk10features.samplerAnisotropy = VK_TRUE;
        vk10features.sampleRateShading = VK_TRUE;
        //vk10features.textureCompressionETC2 = VK_TRUE;
        vk10features.textureCompressionBC = VK_TRUE;
        vk10features.shaderClipDistance = VK_TRUE;
        vk10features.shaderCullDistance = VK_TRUE;

        const auto selectDevice = [&](const uint32_t minor)
        {
            auto selector = 
                vkb::PhysicalDeviceSelector(instance)
                        .set_minimum_version( 1, minor)
                        .set_surface( targetSurface )
                        .prefer_gpu_device_type( vkb::PreferredDeviceType::discrete )
                        .add_required_extension( VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME )
                        .add_required_extension( VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME )
                        .set_required_features( vk10features )
                        .set_required_features( vk10features )
                        .set_required_features( vk10features )
                        .set_required_features_11( vk11features )
                        .set_required_features_12( vk12features );
            if (minor >= 4)
            {
                vk14features.pNext = &bufferDeviceAddressFeatures;
                selector.set_required_features_14( vk14features );
            }
            else
            {
                pushDescriptorProperties.pNext = &bufferDeviceAddressFeatures;
                vk13features.pNext = &pushDescriptorProperties;
                selector.set_required_features_13(vk13features);
                selector.add_required_extension( VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME );
            }

            vulkanMinorVersion(minor);
            return selector.select();
        };

        vkb::Result<vkb::PhysicalDevice> physicalDeviceSelection = selectDevice(Config::DESIRED_VULKAN_MINOR_VERSION);
        if ( !physicalDeviceSelection )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), physicalDeviceSelection.error().message().c_str() );
            physicalDeviceSelection = selectDevice(Config::MINIMUM_VULKAN_MINOR_VERSION);
            if (!physicalDeviceSelection)
            {
                Console::errorfn(LOCALE_STR("ERROR_VK_INIT"), physicalDeviceSelection.error().message().c_str());
                return;
            }
        }

        _physicalDevice = physicalDeviceSelection.value();
        _physicalDevice.enable_extensions_if_present(
            {
                VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
                VK_EXT_MESH_SHADER_EXTENSION_NAME
            }
        );

        //create the final Vulkan device
        vkb::DeviceBuilder deviceBuilder{ _physicalDevice };

        for ( const auto& extension : _physicalDevice.get_extensions() )
        {
            if ( extension == VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME )
            {
                supportsDynamicExtension3(true);


                VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{};
                extendedDynamicState3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
                extendedDynamicState3Features.extendedDynamicState3ColorBlendEnable = VK_TRUE;
                extendedDynamicState3Features.extendedDynamicState3ColorBlendEquation = VK_TRUE;
                extendedDynamicState3Features.extendedDynamicState3ColorWriteMask = VK_TRUE;
                deviceBuilder.add_pNext(&extendedDynamicState3Features);
            }
            else if ( extension == VK_EXT_MESH_SHADER_EXTENSION_NAME )
            {
                suportsMeshShaders(true);

                VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
                meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
                meshShaderFeatures.meshShader = VK_TRUE;
                meshShaderFeatures.taskShader = VK_TRUE;
                meshShaderFeatures.multiviewMeshShader = VK_FALSE;
                meshShaderFeatures.primitiveFragmentShadingRateMeshShader = VK_FALSE;
                meshShaderFeatures.meshShaderQueries = VK_TRUE;

                deviceBuilder.add_pNext(&meshShaderFeatures);
            }
        }

        auto vkbDevice = deviceBuilder.build();
        if (!vkbDevice)
        {
            Console::errorfn(LOCALE_STR("ERROR_VK_INIT"), vkbDevice.error().message().c_str());
            return;
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
