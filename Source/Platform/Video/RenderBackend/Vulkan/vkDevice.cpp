// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/vkDevice.h"
#include "Headers/VKWrapper.h"

#include "Core/Headers/RenderSmokeTest.h"
#include "Utility/Headers/Localization.h"

#include <cinttypes>

namespace Divide
{
    thread_local VKDevice::SmokeFailureState VKDevice::s_smokeFailureState{};

    namespace
    {
        [[noreturn]] void FinishVulkanDeviceLost( std::FILE* output, const char* operation, const VKDevice* device ) noexcept
        {
            VKDevice::SetPendingSmokeDeviceLossFailure( output );
            if ( std::fprintf( output, "detail=%s\n", operation != nullptr ? operation : "<unknown>" ) < 0 ||
                 std::fflush( output ) != 0 )
            {
                RenderSmokeTest::EndFailure( output, RenderSmokeTest::IO_FAILURE_EXIT );
            }

            if ( device == nullptr )
            {
                device = VK_API::GetStateTracker()._device;
            }
            if ( device != nullptr )
            {
                device->writeDeviceFaultInfo( output );
            }
            else
            {
                std::fprintf( output, "device_fault=unavailable_before_device_registration\n" );
            }
            RenderSmokeTest::EndFailure( output, RenderSmokeTest::DEVICE_LOST_EXIT );
        }
    }

    [[noreturn]] void ReportVulkanDeviceLost( const char* operation, const VKDevice* device ) noexcept
    {
        FinishVulkanDeviceLost( RenderSmokeTest::BeginFailure( "device_lost" ), operation, device );
    }

    bool VKDevice::SmokeQueueSubmitActive() noexcept
    {
        return s_smokeFailureState._queueSubmitActive;
    }

    std::FILE* VKDevice::PendingSmokeDeviceLossFailure() noexcept
    {
        return s_smokeFailureState._deviceLossFailure;
    }

    void VKDevice::SetPendingSmokeDeviceLossFailure( std::FILE* output ) noexcept
    {
        s_smokeFailureState._deviceLossFailure = output;
    }

    VKDevice::VKDevice( vkb::Instance& instance, VkSurfaceKHR targetSurface )
    {
        supportsDescriptorBuffers(true);

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
        vk13features.shaderDemoteToHelperInvocation = VK_TRUE;
        vk13features.pNext = &bufferDeviceAddressFeatures;

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

        auto selector =
                vkb::PhysicalDeviceSelector(instance)
                        .set_minimum_version( 1, Config::MINIMUM_VULKAN_MINOR_VERSION )
                        .set_surface( targetSurface )
                        .prefer_gpu_device_type( vkb::PreferredDeviceType::discrete )
                        .add_required_extension( VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME )
                        .add_required_extension( VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME )
                        .add_required_extension( VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME ) //< Core in VK 1.4 but throws validation errors if missing
                        .set_required_features( vk10features )
                        .set_required_features_11( vk11features )
                        .set_required_features_12( vk12features )
                        .set_required_features_13( vk13features )
                        .set_required_features_14( vk14features );

        vulkanMinorVersion( Config::MINIMUM_VULKAN_MINOR_VERSION );
        vkb::Result<vkb::PhysicalDevice> physicalDeviceSelection = selector.select();
        if ( !physicalDeviceSelection )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), physicalDeviceSelection.error().message().c_str() );
            return;
        }

        _physicalDevice = physicalDeviceSelection.value();

        if ( RenderSmokeTest::Enabled() )
        {
            VkPhysicalDeviceIDProperties identity{};
            identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &identity;
            vkGetPhysicalDeviceProperties2( _physicalDevice.physical_device, &properties );

            std::array<char, VK_UUID_SIZE * 2u + 1u> uuid{};
            for ( size_t i = 0u; i < VK_UUID_SIZE; ++i )
            {
                std::snprintf( uuid.data() + i * 2u, 3u, "%02x", to_U32( identity.deviceUUID[i] ) );
            }
            RenderSmokeTest::ReportGPU( uuid.data(), properties.properties.deviceName );
        }

        {
            // VK_EXT_descriptor_heap is not enabled/used yet (planned future descriptor model). Only log availability.
            U32 extensionCount = 0u;
            vkEnumerateDeviceExtensionProperties( _physicalDevice.physical_device, nullptr, &extensionCount, nullptr );
            vector<VkExtensionProperties> availableExtensions( extensionCount );
            vkEnumerateDeviceExtensionProperties( _physicalDevice.physical_device, nullptr, &extensionCount, availableExtensions.data() );

            bool descriptorHeapSupported = false;
            for ( const VkExtensionProperties& extension : availableExtensions )
            {
                if ( strcmp( extension.extensionName, "VK_EXT_descriptor_heap" ) == 0 )
                {
                    descriptorHeapSupported = true;
                    break;
                }
            }
            Console::printfn( LOCALE_STR( "VK_DESCRIPTOR_HEAP_INFO" ), descriptorHeapSupported ? "YES" : "NO" );
        }
        _physicalDevice.enable_extensions_if_present(
            {
                VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
                VK_KHR_MAINTENANCE_7_EXTENSION_NAME,
                VK_EXT_MESH_SHADER_EXTENSION_NAME,
            }
        );
        if ( RenderSmokeTest::Enabled() )
        {
            _physicalDevice.enable_extensions_if_present( { VK_EXT_DEVICE_FAULT_EXTENSION_NAME } );
        }

        //create the final Vulkan device
        vkb::DeviceBuilder deviceBuilder{ _physicalDevice };

        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{};
        extendedDynamicState3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;

        VkPhysicalDeviceMaintenance7FeaturesKHR maintenance7Features{};
        maintenance7Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR;

        VkPhysicalDeviceFaultFeaturesEXT faultFeatures{};
        faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;

        for ( const auto& extension : _physicalDevice.get_extensions() )
        {
            if ( extension == VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME )
            {
                VkPhysicalDeviceFeatures2 features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features.pNext = &extendedDynamicState3Features;
                vkGetPhysicalDeviceFeatures2( _physicalDevice.physical_device, &features );
                if ( HasRequiredDynamicState3Features( extendedDynamicState3Features ) )
                {
                    extendedDynamicState3Features = {};
                    extendedDynamicState3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
                    extendedDynamicState3Features.extendedDynamicState3ColorBlendEnable = VK_TRUE;
                    extendedDynamicState3Features.extendedDynamicState3ColorBlendEquation = VK_TRUE;
                    extendedDynamicState3Features.extendedDynamicState3ColorWriteMask = VK_TRUE;
                    supportsDynamicExtension3(true);
                    deviceBuilder.add_pNext(&extendedDynamicState3Features);
                }
            }
            else if ( extension == VK_KHR_MAINTENANCE_7_EXTENSION_NAME )
            {
                VkPhysicalDeviceFeatures2 features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features.pNext = &maintenance7Features;
                vkGetPhysicalDeviceFeatures2( _physicalDevice.physical_device, &features );
                if ( maintenance7Features.maintenance7 )
                {
                    supportsMaintenance7(true);
                    deviceBuilder.add_pNext(&maintenance7Features);
                }
            }
            else if ( extension == VK_EXT_MESH_SHADER_EXTENSION_NAME )
            {
                /*supportsMeshShaders(true);

                VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
                meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
                meshShaderFeatures.meshShader = VK_TRUE;
                meshShaderFeatures.taskShader = VK_TRUE;
                meshShaderFeatures.multiviewMeshShader = VK_FALSE;
                meshShaderFeatures.primitiveFragmentShadingRateMeshShader = VK_FALSE;
                meshShaderFeatures.meshShaderQueries = VK_TRUE;

                deviceBuilder.add_pNext(&meshShaderFeatures);*/
            }
            else if ( extension == VK_EXT_DEVICE_FAULT_EXTENSION_NAME && RenderSmokeTest::Enabled() )
            {
                VkPhysicalDeviceFeatures2 features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                features.pNext = &faultFeatures;
                vkGetPhysicalDeviceFeatures2( _physicalDevice.physical_device, &features );
                if ( CanEnableDeviceFault( true, true, faultFeatures.deviceFault == VK_TRUE ) )
                {
                    _deviceFaultEnabled = true;
                    _deviceFaultVendorBinary = faultFeatures.deviceFaultVendorBinary == VK_TRUE;
                    deviceBuilder.add_pNext( &faultFeatures );
                }
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
        RenderSmokeTest::ReportDeviceFaultSupport( _deviceFaultEnabled, _deviceFaultVendorBinary );

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

        if ( getDevice() == nullptr )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), "VKDevice::getQueueInternal error: no valid device found!");
            return {};
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
                return getQueueInternal(type, false);
            }
            
            Console::errorfn( LOCALE_STR( "ERROR_VK_DEDICATED_QUEUE" ), QueueName[to_base( type )], index.error().message().c_str() );
            return {};
        }

        const auto indexVal = index.value();
        VKQueue ret
        {
            ._queue = VK_NULL_HANDLE,
            ._pool = createCommandPool(indexVal, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT),
            ._index = indexVal,
            ._type = type
        };
        
        vkGetDeviceQueue( _device.device, indexVal, 0u, &ret._queue );

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

    VkResult VKDevice::submitToQueue( const QueueType queue, const VkSubmitInfo& submitInfo, VkFence& fence ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Submit command buffer to the queue and execute it.
        // "fence" will now block until the graphic commands finish execution
        VkResult submitResult = VK_SUCCESS;
        {
            LockGuard<Mutex> w_lock( queueLock( queue ) );
            s_smokeFailureState._queueSubmitActive = RenderSmokeTest::Enabled();
            submitResult = vkQueueSubmit( _queues[to_base( queue )]._queue, 1, &submitInfo, fence );
            s_smokeFailureState._queueSubmitActive = false;
        }
        std::FILE* deferredFailure = s_smokeFailureState._deviceLossFailure;
        if ( deferredFailure != nullptr )
        {
            if ( submitResult == VK_ERROR_DEVICE_LOST )
            {
                FinishVulkanDeviceLost( deferredFailure, "vkQueueSubmit", this );
            }
            std::fprintf( deferredFailure, "deferred_submit_result=%d\n", to_I32( submitResult ) );
            RenderSmokeTest::EndFailure( deferredFailure, RenderSmokeTest::VALIDATION_FAILURE_EXIT );
        }
        if ( submitResult == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
        {
            ReportVulkanDeviceLost( "vkQueueSubmit", this );
        }
        if ( submitResult != VK_SUCCESS )
        {
            Console::errorfn( "vkQueueSubmit failed [queue={}, fence={}, waitSemaphores={}, commandBuffers={}, signalSemaphores={}]: {} ({})",
                              to_base( queue ),
                              static_cast<const void*>( fence ),
                              submitInfo.waitSemaphoreCount,
                              submitInfo.commandBufferCount,
                              submitInfo.signalSemaphoreCount,
                              VKErrorString( submitResult ).c_str(),
                              static_cast<int>( submitResult ) );
        }

        return submitResult;
    }

    VkResult VKDevice::queuePresent( const QueueType queue, const VkPresentInfoKHR& presentInfo ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VkResult result = VK_SUCCESS;
        {
            LockGuard<Mutex> w_lock( queueLock( queue ) );
            result = vkQueuePresentKHR( _queues[to_base( queue )]._queue, &presentInfo );
        }
        if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
        {
            ReportVulkanDeviceLost( "vkQueuePresentKHR", this );
        }
        return result;
    }

    Mutex& VKDevice::queueLock( const QueueType type ) const noexcept
    {
        const U8 index = to_base( type );
        for ( U8 i = 0u; i < index; ++i )
        {
            if ( _queues[i]._queue == _queues[index]._queue )
            {
                return _queueLocks[i];
            }
        }

        return _queueLocks[index];
    }

    VkResult VKDevice::waitIdle() const
    {
        VkResult result = VK_SUCCESS;
        {
            ScopedLock<Mutex, Mutex, Mutex> lock( _queueLocks[to_base(QueueType::GRAPHICS)],
                                                 _queueLocks[to_base(QueueType::COMPUTE)],
                                                 _queueLocks[to_base(QueueType::TRANSFER)] );
            result = vkDeviceWaitIdle( getVKDevice() );
        }
        if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
        {
            ReportVulkanDeviceLost( "vkDeviceWaitIdle", this );
        }
        return result;
    }

    void VKDevice::writeDeviceFaultInfo( std::FILE* output ) const noexcept
    {
        if ( !_deviceFaultEnabled || getVKDevice() == VK_NULL_HANDLE )
        {
            std::fprintf( output, "device_fault=unavailable\n" );
            return;
        }

        const auto getFault = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(
            vkGetDeviceProcAddr( getVKDevice(), "vkGetDeviceFaultInfoEXT" ) );
        if ( getFault == nullptr )
        {
            std::fprintf( output, "device_fault=entry_point_unavailable\n" );
            return;
        }

        VkDeviceFaultCountsEXT counts{};
        counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
        VkResult result = getFault( getVKDevice(), &counts, nullptr );
        if ( result != VK_SUCCESS )
        {
            std::fprintf( output, "device_fault=query_failed result=%d\n", to_I32( result ) );
            return;
        }

        constexpr U32 MAX_FAULT_ENTRIES = 256u;
        constexpr U64 MAX_FAULT_BINARY_BYTES = 16u * 1024u * 1024u;
        const U32 availableAddresses = counts.addressInfoCount;
        const U32 availableVendors = counts.vendorInfoCount;
        const U64 availableBinaryBytes = counts.vendorBinarySize;
        counts.addressInfoCount = std::min( counts.addressInfoCount, MAX_FAULT_ENTRIES );
        counts.vendorInfoCount = std::min( counts.vendorInfoCount, MAX_FAULT_ENTRIES );
        counts.vendorBinarySize = _deviceFaultVendorBinary ? std::min( counts.vendorBinarySize, MAX_FAULT_BINARY_BYTES ) : 0u;

        try
        {
            std::vector<VkDeviceFaultAddressInfoEXT> addresses( counts.addressInfoCount );
            std::vector<VkDeviceFaultVendorInfoEXT> vendors( counts.vendorInfoCount );
            std::vector<U8> binary( to_size( counts.vendorBinarySize ) );
            VkDeviceFaultInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
            info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
            info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
            info.pVendorBinaryData = binary.empty() ? nullptr : binary.data();
            result = getFault( getVKDevice(), &counts, &info );

            std::fprintf( output, "device_fault=result_%d addresses=%u/%u vendors=%u/%u binary_bytes=%" PRIu64 "/%" PRIu64 "\n",
                          to_I32( result ), counts.addressInfoCount, availableAddresses, counts.vendorInfoCount, availableVendors,
                          to_U64( counts.vendorBinarySize ), availableBinaryBytes );
            if ( result != VK_SUCCESS && result != VK_INCOMPLETE )
            {
                return;
            }

            std::fprintf( output, "device_fault_description=%.*s\n", VK_MAX_DESCRIPTION_SIZE, info.description );
            for ( size_t i = 0u; i < std::min( addresses.size(), to_size( counts.addressInfoCount ) ); ++i )
            {
                const VkDeviceFaultAddressInfoEXT& address = addresses[i];
                std::fprintf( output, "fault_address type=%d value=0x%" PRIx64 " precision=%" PRIu64 "\n",
                              to_I32( address.addressType ), to_U64( address.reportedAddress ), to_U64( address.addressPrecision ) );
            }
            for ( size_t i = 0u; i < std::min( vendors.size(), to_size( counts.vendorInfoCount ) ); ++i )
            {
                const VkDeviceFaultVendorInfoEXT& vendor = vendors[i];
                std::fprintf( output, "fault_vendor description=%.*s code=%" PRIu64 " data=%" PRIu64 "\n",
                              VK_MAX_DESCRIPTION_SIZE, vendor.description, to_U64( vendor.vendorFaultCode ), to_U64( vendor.vendorFaultData ) );
            }
            if ( !binary.empty() )
            {
                const size_t writtenSize = std::min( binary.size(), to_size( counts.vendorBinarySize ) );
                const bool saved = writtenSize > 0u && RenderSmokeTest::WriteDeviceFaultBinary( std::span{ binary.data(), writtenSize } );
                std::fprintf( output, "device_fault_binary=%s\n", saved ? "device-fault.bin" : "write_failed" );
            }
        }
        catch ( const std::bad_alloc& )
        {
            std::fprintf( output, "device_fault=allocation_failed\n" );
        }
    }

}; //namespace Divide
