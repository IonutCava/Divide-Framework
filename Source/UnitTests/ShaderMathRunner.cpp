/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include <vulkan/vulkan.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Check( const VkResult result, const char* operation )
{
    if ( result != VK_SUCCESS )
    {
        throw std::runtime_error( std::string( operation ) + " failed: " + std::to_string( result ) );
    }
}
}

int main( const int argc, char** argv )
{
    try
    {
        if ( argc != 3 )
        {
            throw std::runtime_error( "Expected a SPIR-V filename and a result count" );
        }

        const unsigned long resultCount = std::stoul( argv[2] );
        if ( resultCount == 0 || resultCount > 4096 )
        {
            throw std::runtime_error( "Invalid result count" );
        }

        std::ifstream input( argv[1], std::ios::binary | std::ios::ate );
        if ( !input || input.tellg() <= 0 || input.tellg() % sizeof( std::uint32_t ) != 0 )
        {
            throw std::runtime_error( "Invalid SPIR-V input" );
        }
        std::vector<std::uint32_t> code( static_cast<std::size_t>( input.tellg() ) / sizeof( std::uint32_t ) );
        input.seekg( 0 );
        input.read( reinterpret_cast<char*>( code.data() ), static_cast<std::streamsize>( code.size() * sizeof( std::uint32_t ) ) );
        if ( !input )
        {
            throw std::runtime_error( "Cannot read SPIR-V input" );
        }

        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instanceInfo.pApplicationInfo = &app;
        VkInstance instance{};
        const VkResult instanceResult = vkCreateInstance( &instanceInfo, nullptr, &instance );
        if ( instanceResult == VK_ERROR_INCOMPATIBLE_DRIVER || instanceResult == VK_ERROR_INITIALIZATION_FAILED )
        {
            std::cerr << "No Vulkan 1.2 driver available\n";
            return 77;
        }
        Check( instanceResult, "vkCreateInstance" );

        std::uint32_t physicalCount = 0;
        Check( vkEnumeratePhysicalDevices( instance, &physicalCount, nullptr ), "vkEnumeratePhysicalDevices" );
        std::vector<VkPhysicalDevice> physicals( physicalCount );
        if ( physicalCount > 0 )
        {
            Check( vkEnumeratePhysicalDevices( instance, &physicalCount, physicals.data() ), "vkEnumeratePhysicalDevices" );
        }
        VkPhysicalDevice physical{};
        std::uint32_t queueFamily = 0;
        for ( const VkPhysicalDevice candidate : physicals )
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties( candidate, &properties );
            if ( properties.apiVersion < VK_API_VERSION_1_2 )
            {
                continue;
            }
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties( candidate, &count, nullptr );
            std::vector<VkQueueFamilyProperties> families( count );
            vkGetPhysicalDeviceQueueFamilyProperties( candidate, &count, families.data() );
            for ( std::uint32_t i = 0; i < count; ++i )
            {
                if ( families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) )
                {
                    physical = candidate;
                    queueFamily = i;
                    break;
                }
            }
            if ( physical != VK_NULL_HANDLE )
            {
                break;
            }
        }
        if ( physical == VK_NULL_HANDLE )
        {
            std::cerr << "No Vulkan 1.2 compute device available\n";
            vkDestroyInstance( instance, nullptr );
            return 77;
        }

        const float priority = 1.f;
        VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        VkDevice device{};
        Check( vkCreateDevice( physical, &deviceInfo, nullptr, &device ), "vkCreateDevice" );
        VkQueue queue{};
        vkGetDeviceQueue( device, queueFamily, 0, &queue );

        const VkDeviceSize resultSize = resultCount * sizeof( float );
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = resultSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer{};
        Check( vkCreateBuffer( device, &bufferInfo, nullptr, &buffer ), "vkCreateBuffer" );
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements( device, buffer, &requirements );
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties( physical, &memoryProperties );
        std::uint32_t memoryIndex = memoryProperties.memoryTypeCount;
        for ( std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i )
        {
            const auto flags = memoryProperties.memoryTypes[i].propertyFlags;
            if ( (requirements.memoryTypeBits & (1u << i)) &&
                 (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                     (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) )
            {
                memoryIndex = i;
                break;
            }
        }
        if ( memoryIndex == memoryProperties.memoryTypeCount )
        {
            throw std::runtime_error( "No host-visible coherent memory" );
        }
        VkMemoryAllocateInfo memoryInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        memoryInfo.allocationSize = requirements.size;
        memoryInfo.memoryTypeIndex = memoryIndex;
        VkDeviceMemory memory{};
        Check( vkAllocateMemory( device, &memoryInfo, nullptr, &memory ), "vkAllocateMemory" );
        Check( vkBindBufferMemory( device, buffer, memory, 0 ), "vkBindBufferMemory" );

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        setLayoutInfo.bindingCount = 1;
        setLayoutInfo.pBindings = &binding;
        VkDescriptorSetLayout setLayout{};
        Check( vkCreateDescriptorSetLayout( device, &setLayoutInfo, nullptr, &setLayout ), "vkCreateDescriptorSetLayout" );
        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        VkPipelineLayout pipelineLayout{};
        Check( vkCreatePipelineLayout( device, &layoutInfo, nullptr, &pipelineLayout ), "vkCreatePipelineLayout" );

        VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = code.size() * sizeof( std::uint32_t );
        moduleInfo.pCode = code.data();
        VkShaderModule module{};
        Check( vkCreateShaderModule( device, &moduleInfo, nullptr, &module ), "vkCreateShaderModule" );
        VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";
        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.stage = stage;
        pipelineInfo.layout = pipelineLayout;
        VkPipeline pipeline{};
        Check( vkCreateComputePipelines( device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline ), "vkCreateComputePipelines" );

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VkDescriptorPool pool{};
        Check( vkCreateDescriptorPool( device, &poolInfo, nullptr, &pool ), "vkCreateDescriptorPool" );
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout;
        VkDescriptorSet set{};
        Check( vkAllocateDescriptorSets( device, &allocInfo, &set ), "vkAllocateDescriptorSets" );
        VkDescriptorBufferInfo descriptor{ buffer, 0, resultSize };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &descriptor;
        vkUpdateDescriptorSets( device, 1, &write, 0, nullptr );

        VkCommandPoolCreateInfo commandPoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        commandPoolInfo.queueFamilyIndex = queueFamily;
        VkCommandPool commandPool{};
        Check( vkCreateCommandPool( device, &commandPoolInfo, nullptr, &commandPool ), "vkCreateCommandPool" );
        VkCommandBufferAllocateInfo commandAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        VkCommandBuffer command{};
        Check( vkAllocateCommandBuffers( device, &commandAlloc, &command ), "vkAllocateCommandBuffers" );
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        Check( vkBeginCommandBuffer( command, &begin ), "vkBeginCommandBuffer" );
        vkCmdBindPipeline( command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline );
        vkCmdBindDescriptorSets( command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr );
        vkCmdDispatch( command, 1, 1, 1 );
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                              1, &barrier, 0, nullptr, 0, nullptr );
        Check( vkEndCommandBuffer( command ), "vkEndCommandBuffer" );
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        Check( vkQueueSubmit( queue, 1, &submit, VK_NULL_HANDLE ), "vkQueueSubmit" );
        Check( vkQueueWaitIdle( queue ), "vkQueueWaitIdle" );

        void* mapped{};
        Check( vkMapMemory( device, memory, 0, resultSize, 0, &mapped ), "vkMapMemory" );
        const float* results = static_cast<const float*>( mapped );
        std::cout << std::setprecision( 9 );
        for ( unsigned long i = 0; i < resultCount; ++i )
        {
            std::cout << results[i] << '\n';
        }
        vkUnmapMemory( device, memory );

        vkDestroyCommandPool( device, commandPool, nullptr );
        vkDestroyDescriptorPool( device, pool, nullptr );
        vkDestroyPipeline( device, pipeline, nullptr );
        vkDestroyShaderModule( device, module, nullptr );
        vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
        vkDestroyDescriptorSetLayout( device, setLayout, nullptr );
        vkDestroyBuffer( device, buffer, nullptr );
        vkFreeMemory( device, memory, nullptr );
        vkDestroyDevice( device, nullptr );
        vkDestroyInstance( instance, nullptr );
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
