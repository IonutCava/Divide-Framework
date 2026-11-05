#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#include "vkResources.h"

namespace Divide
{
    constexpr U32 MAX_VULKAN_TOOL_ENUMERATION_ATTEMPTS = 4u;

    template<typename Query>
    [[nodiscard]] VkResult EnumeratePhysicalDeviceTools( Query&& query, std::vector<VkPhysicalDeviceToolProperties>& tools )
    {
        tools.clear();
        for ( U32 attempt = 0u; attempt < MAX_VULKAN_TOOL_ENUMERATION_ATTEMPTS; ++attempt )
        {
            U32 capacity = 0u;
            VkResult result = query( &capacity, nullptr );
            if ( result != VK_SUCCESS || capacity == 0u )
            {
                return result;
            }

            const VkPhysicalDeviceToolProperties toolTemplate{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES };
            tools.assign( capacity, toolTemplate );
            U32 written = capacity;
            result = query( &written, tools.data() );
            if ( written > capacity )
            {
                tools.clear();
                return VK_ERROR_UNKNOWN;
            }
            if ( result == VK_SUCCESS )
            {
                tools.resize( written );
                return VK_SUCCESS;
            }

            tools.clear();
            if ( result != VK_INCOMPLETE )
            {
                return result;
            }
        }
        return VK_INCOMPLETE;
    }
}
