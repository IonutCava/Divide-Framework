// Copyright (c) 2026 DIVIDE-Studio

#include <vulkan/vulkan.h>

#include "../Source/Core/Headers/VulkanRecoveryPolicy.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace
{
    [[noreturn]] void Fail( const char* stage, const VkResult result )
    {
        std::fprintf( stderr, "PROBE_FAILED stage=%s result=%d\n", stage, static_cast<int>( result ) );
        std::fflush( stderr );
        std::_Exit( 1 );
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                       void* )
    {
        if ( ( severity & ( VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) ) != 0u )
        {
            std::fprintf( stderr, "PROBE_VALIDATION id=%s message=%s\n",
                          data != nullptr && data->pMessageIdName != nullptr ? data->pMessageIdName : "UNKNOWN",
                          data != nullptr && data->pMessage != nullptr ? data->pMessage : "<no message>" );
            std::fflush( stderr );
            std::_Exit( 2 );
        }
        return VK_FALSE;
    }

    [[nodiscard]] int HexDigit( const char value ) noexcept
    {
        if ( value >= '0' && value <= '9' )
        {
            return value - '0';
        }
        if ( value >= 'a' && value <= 'f' )
        {
            return value - 'a' + 10;
        }
        if ( value >= 'A' && value <= 'F' )
        {
            return value - 'A' + 10;
        }
        return -1;
    }

    [[nodiscard]] bool ParseUUID( const std::string_view text, std::array<std::uint8_t, VK_UUID_SIZE>& uuid ) noexcept
    {
        if ( text.size() != uuid.size() * 2u )
        {
            return false;
        }
        for ( std::size_t i = 0; i < uuid.size(); ++i )
        {
            const int high = HexDigit( text[i * 2u] );
            const int low = HexDigit( text[i * 2u + 1u] );
            if ( high < 0 || low < 0 )
            {
                return false;
            }
            uuid[i] = static_cast<std::uint8_t>( high * 16 + low );
        }
        return true;
    }
}

int main( const int argc, char** argv )
{
    Divide::RenderSmokeTest::RecoveryProgress progress{};
    std::array<std::uint8_t, VK_UUID_SIZE> requestedUUID{};
    bool hasUUID = false;
    bool hasTimeout = false;
    std::uint32_t timeoutMS = 3000u;

    for ( int i = 1; i < argc; ++i )
    {
        const std::string_view argument{ argv[i] };
        if ( argument.starts_with( "--uuid=" ) && !hasUUID )
        {
            hasUUID = ParseUUID( argument.substr( 7u ), requestedUUID );
            if ( !hasUUID )
            {
                std::fprintf( stderr, "A 32-digit Vulkan device UUID is required.\n" );
                return 2;
            }
        }
        else if ( argument.starts_with( "--timeout-ms=" ) && !hasTimeout )
        {
            hasTimeout = true;
            const std::string_view value = argument.substr( 13u );
            const auto result = std::from_chars( value.data(), value.data() + value.size(), timeoutMS );
            if ( result.ec != std::errc{} || result.ptr != value.data() + value.size() || timeoutMS == 0u || timeoutMS > 10000u )
            {
                std::fprintf( stderr, "The fence timeout must be between 1 and 10000 ms.\n" );
                return 2;
            }
        }
        else
        {
            std::fprintf( stderr, "Unknown or duplicate probe option.\n" );
            return 2;
        }
    }
    if ( !hasUUID )
    {
        std::fprintf( stderr, "A 32-digit Vulkan device UUID is required.\n" );
        return 2;
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Divide Vulkan recovery probe";
    application.apiVersion = VK_API_VERSION_1_1;

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = ValidationCallback;

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = &debugInfo;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledLayerCount = 1u;
    instanceInfo.ppEnabledLayerNames = &validationLayer;
    instanceInfo.enabledExtensionCount = 1u;
    instanceInfo.ppEnabledExtensionNames = &debugExtension;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance( &instanceInfo, nullptr, &instance );
    if ( result != VK_SUCCESS )
    {
        Fail( "create_instance", result );
    }

    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );
    const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );
    if ( createMessenger == nullptr || destroyMessenger == nullptr )
    {
        Fail( "debug_messenger_unavailable", VK_ERROR_EXTENSION_NOT_PRESENT );
    }
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    result = createMessenger( instance, &debugInfo, nullptr, &messenger );
    if ( result != VK_SUCCESS )
    {
        Fail( "create_debug_messenger", result );
    }

    std::uint32_t deviceCount = 0u;
    result = vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
    if ( result != VK_SUCCESS || deviceCount == 0u )
    {
        Fail( "enumerate_devices", result );
    }
    std::vector<VkPhysicalDevice> devices( deviceCount );
    result = vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );
    if ( result != VK_SUCCESS )
    {
        Fail( "enumerate_devices", result );
    }

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    for ( const VkPhysicalDevice candidate : devices )
    {
        VkPhysicalDeviceIDProperties identity{};
        identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &identity;
        vkGetPhysicalDeviceProperties2( candidate, &properties );
        if ( std::memcmp( identity.deviceUUID, requestedUUID.data(), requestedUUID.size() ) == 0 )
        {
            if ( physicalDevice != VK_NULL_HANDLE )
            {
                Fail( "duplicate_device_uuid", VK_ERROR_INITIALIZATION_FAILED );
            }
            physicalDevice = candidate;
        }
    }
    if ( !progress.matchDevice( physicalDevice != VK_NULL_HANDLE ) )
    {
        Fail( "matching_device_unavailable", VK_ERROR_INITIALIZATION_FAILED );
    }

    std::uint32_t familyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &familyCount, nullptr );
    std::vector<VkQueueFamilyProperties> families( familyCount );
    vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &familyCount, families.data() );
    std::uint32_t graphicsFamily = familyCount;
    for ( std::uint32_t i = 0u; i < familyCount; ++i )
    {
        if ( families[i].queueCount > 0u && ( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) != 0u )
        {
            graphicsFamily = i;
            break;
        }
    }
    if ( graphicsFamily == familyCount )
    {
        Fail( "graphics_queue_unavailable", VK_ERROR_INITIALIZATION_FAILED );
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsFamily;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1u;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice( physicalDevice, &deviceInfo, nullptr, &device );
    if ( !progress.createDevice( result == VK_SUCCESS ) )
    {
        Fail( "create_device", result );
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue( device, graphicsFamily, 0u, &queue );
    if ( queue == VK_NULL_HANDLE )
    {
        Fail( "get_queue", VK_ERROR_INITIALIZATION_FAILED );
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamily;

    VkCommandPool pool = VK_NULL_HANDLE;
    result = vkCreateCommandPool( device, &poolInfo, nullptr, &pool );
    if ( result != VK_SUCCESS )
    {
        Fail( "create_command_pool", result );
    }

    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = pool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1u;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers( device, &allocationInfo, &commandBuffer );
    if ( result != VK_SUCCESS )
    {
        Fail( "allocate_command_buffer", result );
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer( commandBuffer, &beginInfo );
    if ( result != VK_SUCCESS )
    {
        Fail( "begin_command_buffer", result );
    }
    result = vkEndCommandBuffer( commandBuffer );
    if ( result != VK_SUCCESS )
    {
        Fail( "end_command_buffer", result );
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    result = vkCreateFence( device, &fenceInfo, nullptr, &fence );
    if ( result != VK_SUCCESS )
    {
        Fail( "create_fence", result );
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &commandBuffer;
    result = vkQueueSubmit( queue, 1u, &submit, fence );
    if ( !progress.submit( result == VK_SUCCESS ) )
    {
        Fail( "queue_submit", result );
    }
    result = vkWaitForFences( device, 1u, &fence, VK_TRUE, static_cast<std::uint64_t>( timeoutMS ) * 1000000u );
    if ( !progress.completeFence( result == VK_SUCCESS ) || !progress.recovered() )
    {
        Fail( "wait_fence", result );
    }

    vkDestroyFence( device, fence, nullptr );
    vkDestroyCommandPool( device, pool, nullptr );
    vkDestroyDevice( device, nullptr );
    destroyMessenger( instance, messenger, nullptr );
    vkDestroyInstance( instance, nullptr );
    std::printf( "PROBE_RECOVERED\n" );
    std::fflush( stdout );
    return 0;
}
