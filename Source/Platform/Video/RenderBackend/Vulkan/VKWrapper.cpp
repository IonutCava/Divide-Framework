// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/VKWrapper.h"
#include "Headers/vkDescriptorState.h"
#include "Headers/vkToolProperties.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"
#include "Utility/Headers/ImageToolsFwd.h"

#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/SampledImageResolver.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Buffers/Headers/vkRenderTarget.h"
#include "Buffers/Headers/vkBufferImpl.h"
#include "Buffers/Headers/vkShaderBuffer.h"
#include "Buffers/Headers/vkGenericVertexData.h"

#include "Shaders/Headers/vkShaderProgram.h"

#include "Textures/Headers/vkTexture.h"
#include "Textures/Headers/vkSamplerObject.h"

#include <SDL3/SDL_vulkan.h>

#define VMA_IMPLEMENTATION
#include "Headers/vkMemAllocatorInclude.h"

namespace
{
    template <typename Handle>
    [[nodiscard]] Divide::U64 ShaderModuleHandleBits( const Handle handle ) noexcept
    {
        if constexpr ( std::is_pointer_v<Handle> )
        {
            return Divide::to_U64( reinterpret_cast<std::uintptr_t>( handle ) );
        }
        else
        {
            return Divide::to_U64( handle );
        }
    }

    inline VKAPI_ATTR VkBool32 VKAPI_CALL divide_debug_callback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void* )
    {
        using namespace Divide;

        const bool warning = ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) != 0u;
        const bool error = ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) != 0u;
        const char* messageID = pCallbackData != nullptr && pCallbackData->pMessageIdName != nullptr ? pCallbackData->pMessageIdName : "UNKNOWN";
        const char* message = pCallbackData != nullptr && pCallbackData->pMessage != nullptr ? pCallbackData->pMessage : "<no message>";
        if ( RenderSmokeTest::Enabled() && ( warning || error ) )
        {
            if ( std::FILE* pendingFailure = VKDevice::PendingSmokeDeviceLossFailure(); pendingFailure != nullptr )
            {
                if ( std::fprintf( pendingFailure, "subsequent_validation_id=%s\nsubsequent_validation_message=%s\n", messageID, message ) < 0 ||
                     std::fflush( pendingFailure ) != 0 )
                {
                    RenderSmokeTest::EndFailure( pendingFailure, RenderSmokeTest::IO_FAILURE_EXIT );
                }
                return VK_FALSE;
            }
        }
        if ( RenderSmokeTest::Enabled() && RenderSmokeTest::IsAllowedValidationAdvisory( warning, error, messageID, message ) )
        {
            RenderSmokeTest::ReportValidationAdvisory( messageID, message );
        }
        if ( RenderSmokeTest::Enabled() && RenderSmokeTest::IsFatalValidationSeverity( warning, error, messageID, message ) )
        {
            const bool deferQueueDeviceLoss = RenderSmokeTest::IsQueueSubmitDeviceLossResult( VKDevice::SmokeQueueSubmitActive(),
                                                                                             messageID,
                                                                                             message );
            std::FILE* output = RenderSmokeTest::BeginFailure( deferQueueDeviceLoss ? "device_lost" : "validation" );
            std::fprintf( output, "severity=%s\ntype=%u\nthread=%zu\nmessage_id=%s\n",
                          error ? "error" : "warning",
                          to_U32( messageType ),
                          std::hash<std::thread::id>{}( std::this_thread::get_id() ),
                          messageID );
            if ( pCallbackData != nullptr )
            {
                for ( U32 i = 0u; i < pCallbackData->objectCount && pCallbackData->pObjects != nullptr; ++i )
                {
                    const VkDebugUtilsObjectNameInfoEXT& object = pCallbackData->pObjects[i];
                    std::fprintf( output, "object type=%d handle=%s name=%s\n",
                                  to_I32( object.objectType ),
                                  std::to_string( object.objectHandle ).c_str(),
                                  object.pObjectName != nullptr ? object.pObjectName : "" );
                }
                for ( U32 i = 0u; i < pCallbackData->queueLabelCount && pCallbackData->pQueueLabels != nullptr; ++i )
                {
                    const char* name = pCallbackData->pQueueLabels[i].pLabelName;
                    std::fprintf( output, "queue_label=%s\n", name != nullptr ? name : "" );
                }
                for ( U32 i = 0u; i < pCallbackData->cmdBufLabelCount && pCallbackData->pCmdBufLabels != nullptr; ++i )
                {
                    const char* name = pCallbackData->pCmdBufLabels[i].pLabelName;
                    std::fprintf( output, "command_label=%s\n", name != nullptr ? name : "" );
                }
            }
            std::fprintf( output, "message:\n%s\n", message );
            if ( deferQueueDeviceLoss )
            {
                if ( std::ferror( output ) != 0 || std::fflush( output ) != 0 )
                {
                    RenderSmokeTest::EndFailure( output, RenderSmokeTest::IO_FAILURE_EXIT );
                }
                VKDevice::SetPendingSmokeDeviceLossFailure( output );
                return VK_FALSE;
            }
            RenderSmokeTest::EndFailure( output, RenderSmokeTest::VALIDATION_FAILURE_EXIT );
        }

        if ( VK_API::GetStateTracker()._enabledAPIDebugging && !(*VK_API::GetStateTracker()._enabledAPIDebugging) )
        {
            return VK_FALSE;
        }

        const auto to_string_message_severity = []( VkDebugUtilsMessageSeverityFlagBitsEXT s ) -> const char*
        {
            switch ( s )
            {
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "ERROR";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "INFO";
                default:                                              return "UNKNOWN";
            }
        };

        const auto to_string_message_type = []( VkDebugUtilsMessageTypeFlagsEXT s )
        {
            Str<64> ret{};
            if ( s & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT )
            {
                ret.append("[General]");
            }
            if ( s & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT )
            {
                ret.append( "[Validation]" );
            }
            if ( s & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT )
            {
                ret.append( "[Performance]" );
            }
            if ( s & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT )
            {
                ret.append( "[Address Binding]" );
            }
            if ( ret.empty() )
            {
                ret.append("[Unknown]");
            }

            return ret;
        };

        constexpr const char* kSkippedMessages[] = {
            "UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension-debugging",
            "BestPractices-specialuse-extension",
            "UNASSIGNED-BestPractices-vkCreateDevice-specialuse-extension-d3demulation",
            "UNASSIGNED-BestPractices-vkCreateDevice-specialuse-extension-glemulation",
            "UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation",
            "UNASSIGNED-BestPractices-vkAllocateMemory-small-allocation",
            "BestPractices-vkBindImageMemory-small-dedicated-allocation",
            "UNASSIGNED-BestPractices-SpirvDeprecated_WorkgroupSize"
        };

        if ( pCallbackData->pMessageIdName != nullptr )
        {
            if ( strstr( pCallbackData->pMessageIdName, "UNASSIGNED-BestPractices-Error-Result") != nullptr )
            {
                // We don't care about this error since we use VMA for our allocations and this is standard behaviour with that library
                if ( strstr( pCallbackData->pMessage, "vkAllocateMemory()" ) != nullptr )
                {
                    return VK_FALSE;
                }
            }
            else
            {
                for ( const char* msg : kSkippedMessages )
                {
                    if ( strstr( pCallbackData->pMessageIdName, msg ) != nullptr )
                    {
                        return VK_FALSE;
                    }
                }
            }
        }

        const string outputError = Util::StringFormat("[ {} ] {} : {}\n",
                                                      to_string_message_severity( messageSeverity ),
                                                      to_string_message_type( messageType ).c_str(),
                                                      pCallbackData->pMessage );

        const Console::ScopedFlags consoleFlags{
            to_U32(to_base(Console::Flags::PRINT_IMMEDIATE)),
            to_U32(to_base(Console::Flags::DECORATE_SEVERITY))
        };

        if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ||
             messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT )
        {
            Console::printfn( "{}", outputError.c_str() );
        }
        else if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
        {
            Console::warnfn( "{}", outputError.c_str() );
        }
        else
        {
            Console::errorfn( "{}", outputError.c_str() );

            const bool isValidationError = ( messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ) != 0u;
            if ( isValidationError )
            {
                const char* messageId = pCallbackData->pMessageIdName != nullptr ? pCallbackData->pMessageIdName : "UNKNOWN_VALIDATION_MESSAGE";
                Console::errorfn( "[ VK_VALIDATION_BREAKPOINT ] {}", messageId );
                DebugBreak(true);
            }

            DIVIDE_GPU_ASSERT( VK_API::GetStateTracker()._assertOnAPIError && !(*VK_API::GetStateTracker()._assertOnAPIError), outputError.c_str() );
        }

        return VK_FALSE; // Applications must return false here
    }
}

namespace Divide
{
    std::atomic_bool g_enableVMADebugLogging{ false };

    static PFN_vkCmdSetColorBlendEnableEXT   vkCmdSetColorBlendEnableEXT   = VK_NULL_HANDLE;
    static PFN_vkCmdSetColorBlendEquationEXT vkCmdSetColorBlendEquationEXT = VK_NULL_HANDLE;
    static PFN_vkCmdSetColorWriteMaskEXT     vkCmdSetColorWriteMaskEXT     = VK_NULL_HANDLE;
    static PFN_vkCmdPushDescriptorSetKHR     vkCmdPushDescriptorSetKHR     = VK_NULL_HANDLE;

    static PFN_vkGetDescriptorSetLayoutSizeEXT              vkGetDescriptorSetLayoutSizeEXT              = VK_NULL_HANDLE;
    static PFN_vkGetDescriptorSetLayoutBindingOffsetEXT     vkGetDescriptorSetLayoutBindingOffsetEXT     = VK_NULL_HANDLE;
    static PFN_vkGetDescriptorEXT                           vkGetDescriptorEXT                           = VK_NULL_HANDLE;
    static PFN_vkCmdBindDescriptorBuffersEXT                vkCmdBindDescriptorBuffersEXT                = VK_NULL_HANDLE;
    static PFN_vkCmdSetDescriptorBufferOffsetsEXT           vkCmdSetDescriptorBufferOffsetsEXT           = VK_NULL_HANDLE;
    static PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT vkCmdBindDescriptorBufferEmbeddedSamplersEXT = VK_NULL_HANDLE;

    static PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = VK_NULL_HANDLE;

    namespace
    {
        const ResourcePath PipelineCacheFileName{ "pipeline_cache.dvd" };

        FORCE_INLINE ResourcePath PipelineCacheLocation()
        {
            return Paths::Shaders::g_cacheLocation / Paths::g_buildTypeLocation / Paths::Shaders::g_cacheLocationVK;
        }

        /// The swapchain is the only target rendered with a flipped (negative height) viewport. Everything else uses OpenGL's memory layout.
        [[nodiscard]] FORCE_INLINE bool IsSwapChainTargetActive() noexcept
        {
            return VK_API::GetStateTracker()._activeRenderTargetID == SCREEN_TARGET_ID;
        }

        [[nodiscard]] FORCE_INLINE bool IsTriangles( const PrimitiveTopology topology )
        {
            return topology == PrimitiveTopology::TRIANGLES ||
                   topology == PrimitiveTopology::TRIANGLE_STRIP ||
                   topology == PrimitiveTopology::TRIANGLE_FAN ||
                   topology == PrimitiveTopology::TRIANGLES_ADJACENCY ||
                   topology == PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY;
        }

        [[nodiscard]] VkShaderStageFlags GetFlagsForStageVisibility( const BaseType<ShaderStageVisibility> mask ) noexcept
        {
            VkShaderStageFlags ret = 0u;

            if ( mask != to_base( ShaderStageVisibility::NONE ) )
            {
                if ( mask == to_base( ShaderStageVisibility::ALL ) )
                {
                    ret = VK_SHADER_STAGE_ALL;
                }
                else if ( mask == to_base( ShaderStageVisibility::ALL_DRAW ) )
                {
                    ret = VK_SHADER_STAGE_ALL_GRAPHICS;
                }
                else if ( mask == to_base( ShaderStageVisibility::COMPUTE ) )
                {
                    ret = VK_SHADER_STAGE_COMPUTE_BIT;
                }
                else
                {
                    if ( mask & to_base(ShaderStageVisibility::VERTEX ) )
                    {
                        ret |= VK_SHADER_STAGE_VERTEX_BIT;
                    }
                    if ( mask & to_base(ShaderStageVisibility::GEOMETRY ) )
                    {
                        ret |= VK_SHADER_STAGE_GEOMETRY_BIT;
                    }
                    if ( mask & to_base(ShaderStageVisibility::TESS_CONTROL ) )
                    {
                        ret |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                    }
                    if ( mask & to_base(ShaderStageVisibility::TESS_EVAL ) )
                    {
                        ret |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                    }
                    if ( mask & to_base(ShaderStageVisibility::FRAGMENT ) )
                    {
                        ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
                    }
                    if ( mask & to_base(ShaderStageVisibility::COMPUTE ) )
                    {
                        ret |= VK_SHADER_STAGE_COMPUTE_BIT;
                    }
                }
            }

            return ret;
        }

        // One entry per shared DescriptorSetUsage (PER_FRAME/PER_PASS/PER_BATCH). PER_DRAW uses push descriptors instead.
        // Sets are allocated from per-frame pools, so the cache is invalidated every frame (see VK_API::frameStarted).
        struct SharedDescriptorSetCache
        {
            VKDescriptorSlotStates _slots{};
            VkDescriptorSet _set{ VK_NULL_HANDLE };
            bool _pendingUpdate{ true };
        };

        // What is currently bound on the active command buffer for a given bind point (graphics/compute).
        // Indexed by DescriptorSetUsage (which is also the Vulkan set index).
        //
        // These caches compare raw handles. Runtime resource and shader-reload pipeline destruction is deferred,
        // and the caches reset at frame start or on a command-buffer change. Teardown invalidates them before
        // destroying the pipeline cache immediately. Reusing handles during a recording would break this invariant.
        struct BoundDescriptorState
        {
            std::array<VkDescriptorSet, to_base( DescriptorSetUsage::COUNT )> _sets{};
            std::array<std::array<U32, MAX_BINDINGS_PER_DESCRIPTOR_SET>, to_base( DescriptorSetUsage::COUNT )> _dynamicOffsets{};
            VKPushDescriptorState _perDraw{};
        };

        std::array<SharedDescriptorSetCache, to_base( DescriptorSetUsage::COUNT )> s_sharedDescriptorSets{};
        std::array<BoundDescriptorState, 2u> s_boundDescriptorState{}; // 0 = graphics, 1 = compute
        VkCommandBuffer s_boundDescriptorCmdBuffer{ VK_NULL_HANDLE };
        U64 s_perDrawDescriptorVersion{ 0u };

        void ResetBoundDescriptorState() noexcept
        {
            s_boundDescriptorState = {};
            s_boundDescriptorCmdBuffer = VK_NULL_HANDLE;
        }

        void InvalidateSharedDescriptorSets() noexcept
        {
            s_sharedDescriptorSets = {};
            ResetBoundDescriptorState();
        }
    }

    constexpr U32 VK_VENDOR_ID_AMD = 0x1002;
    constexpr U32 VK_VENDOR_ID_IMGTECH = 0x1010;
    constexpr U32 VK_VENDOR_ID_NVIDIA = 0x10DE;
    constexpr U32 VK_VENDOR_ID_ARM = 0x13B5;
    constexpr U32 VK_VENDOR_ID_QUALCOMM = 0x5143;
    constexpr U32 VK_VENDOR_ID_INTEL = 0x8086;

    bool VK_API::s_hasDebugMarkerSupport = false;
    bool VK_API::s_hasDescriptorBufferSupport = false;
    bool VK_API::s_hasDynamicBlendStateSupport = false;
    U32 VK_API::s_maxDescriptorSetStorageBuffersDynamic{0u};
    U32 VK_API::s_maxPushDescriptors{0u};
    U32 VK_API::s_maxDescriptorSetUniformBuffersDynamic{0u};
    VkResolveModeFlags VK_API::s_supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

    VKDeletionQueue VK_API::s_transientDeleteQueue;
    VKDeletionQueue VK_API::s_deviceDeleteQueue;
    VKTransferQueue VK_API::s_transferQueue;
    VKImageBarrierQueue VK_API::s_imageBarrierQueue;
    VKSubmitSempahore VK_API::s_submitSemaphores;

    VKStateTracker VK_API::s_stateTracker;
    Mutex VK_API::s_reloadedShadersLock;
    vector<I64> VK_API::s_reloadedShaders;
    SharedMutex VK_API::s_samplerMapLock;
    VK_API::SamplerObjectMap VK_API::s_samplerMap{};
    VK_API::DepthFormatInformation VK_API::s_depthFormatInformation;

    VkPipeline PipelineBuilder::build_pipeline( VkDevice device, VkPipelineCache pipelineCache, const bool graphics )
    {
        if ( graphics )
        {
            return build_graphics_pipeline( device, pipelineCache );
        }

        return build_compute_pipeline( device, pipelineCache );
    }

    VkPipeline PipelineBuilder::build_compute_pipeline( VkDevice device, VkPipelineCache pipelineCache )
    {
        VkComputePipelineCreateInfo pipelineInfo = vk::computePipelineCreateInfo( _pipelineLayout );
        pipelineInfo.stage = _shaderStages.front();
        pipelineInfo.layout = _pipelineLayout;

        //it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
        VkPipeline newPipeline;
        if ( vkCreateComputePipelines( device, pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_PIPELINE_COMPUTE_FAILED" ) );
            return VK_NULL_HANDLE; // failed to create graphics pipeline
        }

        return newPipeline;
    }

    VkPipeline PipelineBuilder::build_graphics_pipeline( VkDevice device, VkPipelineCache pipelineCache)
    {
        //make viewport state from our stored viewport and scissor.
        //at the moment we won't support multiple viewports or scissors
        VkPipelineViewportStateCreateInfo viewportState = vk::pipelineViewportStateCreateInfo( 1, 1 );
        viewportState.pViewports = &_viewport;
        viewportState.pScissors = &_scissor;

        const VkPipelineColorBlendStateCreateInfo colorBlending = vk::pipelineColorBlendStateCreateInfo(
            to_U32( _colorBlendAttachments.size() ),
            _colorBlendAttachments.data()
        );

        constexpr VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_OP,
            VK_DYNAMIC_STATE_CULL_MODE,
            VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
            VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,

            VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
            VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
            VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,

            /*ToDo:
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE*/
        };

        constexpr U32 stateCount = to_U32( std::size( dynamicStates ) );

        const VkPipelineDynamicStateCreateInfo dynamicState = vk::pipelineDynamicStateCreateInfo( dynamicStates, stateCount - (VK_API::s_hasDynamicBlendStateSupport ? 0u : 3u) );
        const VkPipelineTessellationDomainOriginStateCreateInfo tessellationOrigin
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO,
            .domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT
        };
        VkPipelineTessellationStateCreateInfo tessellationState = _tessellation;
        tessellationState.pNext = &tessellationOrigin;

        //build the actual pipeline
        //we now use all of the info structs we have been writing into into this one to create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = vk::pipelineCreateInfo( _pipelineLayout, VK_NULL_HANDLE );
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.stageCount = to_U32( _shaderStages.size() );
        pipelineInfo.pStages = _shaderStages.data();
        pipelineInfo.pVertexInputState = &_vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &_inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &_rasterizer;
        pipelineInfo.pMultisampleState = &_multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &_depthStencil;
        pipelineInfo.pTessellationState = &tessellationState;
        pipelineInfo.subpass = 0;
        pipelineInfo.pNext = &VK_API::GetStateTracker()._pipelineRenderInfo;

        //it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
        VkPipeline newPipeline;
        if ( vkCreateGraphicsPipelines( device, pipelineCache, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_PIPELINE_GRAPHICS_FAILED" ) );
            return VK_NULL_HANDLE; // failed to create graphics pipeline
        }

        return newPipeline;
    }

    void VKDeletionQueue::push( DELEGATE<void, VkDevice>&& function )
    {
        LockGuard<Mutex> w_lock( _deletionLock );
        _deletionQueue.emplace_back( MOV( function ), (flags() & to_base(Flags::TREAT_AS_TRANSIENT)) ? Config::MAX_FRAMES_IN_FLIGHT + 1u : 0 );
    }

    void VKDeletionQueue::flush( VkDevice device, const bool force )
    {
        LockGuard<Mutex> w_lock( _deletionLock );
        bool needsClean = false;
        for ( const auto& it : _deletionQueue )
        {
            if (it.second == 0u || force)
            {
                (it.first)(device);
                needsClean = true;
            }
        }

        if ( needsClean )
        {
            if ( force )
            {
                _deletionQueue.clear();
            }
            else
            {
                std::erase_if( _deletionQueue, []( const auto it )
                                {
                                    return it.second == 0u;
                                });
            }
        }
    }

    void VKDeletionQueue::onFrameEnd()
    {
        LockGuard<Mutex> w_lock( _deletionLock );
        for ( auto& it : _deletionQueue )
        {
            if ( it.second > 0u )
            {
                it.second -= 1u;
            }
        }
    }

    bool VKDeletionQueue::empty() const
    {
        LockGuard<Mutex> w_lock( _deletionLock );
        return _deletionQueue.empty();
    }

    VKImmediateCmdContext::VKImmediateCmdContext( const Configuration& config, VKDevice& context, const QueueType type )
        : _context( context )
        , _config(config)
        , _type(type)
        , _queueIndex(context.getQueue(type)._index)
    {
        const VkFenceCreateInfo fenceCreateInfo = vk::fenceCreateInfo();
        for (U8 i = 0u; i < BUFFER_COUNT; ++i )
        {
            vkCreateFence( _context.getVKDevice(), &fenceCreateInfo, nullptr, &_bufferFences[i]);
        }

        _commandPool = _context.createCommandPool( _context.getQueue( type )._index, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT );

        const VkCommandBufferAllocateInfo cmdBufAllocateInfo = vk::commandBufferAllocateInfo( _commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, BUFFER_COUNT );

        VK_CHECK( vkAllocateCommandBuffers( _context.getVKDevice(), &cmdBufAllocateInfo, _commandBuffers.data() ) );
    }

    VKImmediateCmdContext::~VKImmediateCmdContext()
    {
        vkDestroyCommandPool( _context.getVKDevice(), _commandPool, nullptr );
        for ( U8 i = 0u; i < BUFFER_COUNT; ++i )
        {
            vkDestroyFence( _context.getVKDevice(), _bufferFences[i], nullptr);
            _bufferFences[i] = VK_NULL_HANDLE;
        }
    }

    void VKImmediateCmdContext::flushCommandBuffer(FlushCallback&& function, const char* scopeName, const bool waitForFinish )
    {
        LockGuard<Mutex> w_lock( _submitLock );

        auto& submitStatus = VK_API::GetStateTracker()._immediateSubmitStatus;
        if ( submitStatus.failed() )
        {
            return;
        }

        const VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo( VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT );

        VkFence fence = _bufferFences[_bufferIndex];

        if ( _wrapCounter > 0u )
        {
            vkWaitForFences( _context.getVKDevice(), 1, &fence, true, 9999999999 );
            vkResetFences( _context.getVKDevice(), 1, &fence );
        }

        VkCommandBuffer cmd = _commandBuffers[_bufferIndex];
        VK_CHECK( vkBeginCommandBuffer( cmd, &cmdBeginInfo ) );
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmd );

        VK_API::PushDebugMessage( _config, cmd, scopeName );

        // Execute the function
        function( cmd, _type, _queueIndex );

        VK_API::PopDebugMessage( _config, cmd ) ;
        VK_CHECK( vkEndCommandBuffer( cmd ) );

        VkSubmitInfo submitInfo = vk::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        thread_local VKSubmitSempahore::Container waitSems;
        thread_local fixed_vector<VkPipelineStageFlags, 8, true > waitStages;

        {
            LockGuard<Mutex> submitLock(VK_API::s_submitSemaphores._lock);
            // Transfer the pending waits to this submission.
            waitSems = VK_API::s_submitSemaphores._pendingSubmitSemaphores;
            VK_API::s_submitSemaphores._pendingSubmitSemaphores.reset_lose_memory();
        }

        for (VkSemaphore semaphore : waitSems)
        {
            VK_API::RegisterCustomAPIDelete([semaphore](VkDevice device)
            {
                vkDestroySemaphore(device, semaphore, nullptr);
            }, true);
        }

        // Create a transient semaphore to signal completion of this immediate submit.
        VkSemaphoreCreateInfo semCreate = vk::semaphoreCreateInfo();
        VkSemaphore submitSemaphore = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(_context.getVKDevice(), &semCreate, nullptr, &submitSemaphore));

        if ( !waitSems.empty() )
        {
            waitStages.resize(waitSems.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            submitInfo.waitSemaphoreCount = to_U32(waitSems.size());
            submitInfo.pWaitSemaphores = waitSems.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
        }

        // Signal our transient semaphore when done so the frame submit can wait on it.
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &submitSemaphore;

        const VkResult submitResult = _context.submitToQueue( _type, submitInfo, fence );
        if ( !submitStatus.record( submitResult ) )
        {
            VK_API::RegisterCustomAPIDelete( [submitSemaphore]( VkDevice device )
            {
                vkDestroySemaphore( device, submitSemaphore, nullptr );
            }, true );
            if ( RenderSmokeTest::Enabled() )
            {
                RenderSmokeTest::Fail( "queue_submit", VKErrorString( submitResult ).c_str(), RenderSmokeTest::VALIDATION_FAILURE_EXIT );
            }
            return;
        }

        VK_API::PushPendingSubmitSemaphore(submitSemaphore);

        _bufferIndex = (_bufferIndex + 1u) % BUFFER_COUNT;

        if ( _bufferIndex == 0u )
        {
            ++_wrapCounter;
        }

        if ( waitForFinish )
        {
            vkWaitForFences(_context.getVKDevice(), 1, &fence, true, UINT64_MAX);
            vkResetFences(_context.getVKDevice(), 1, &fence);
        }
    }

    VKRenderingSignature VKRenderingSignature::From( const VkPipelineRenderingCreateInfo& info, const U8 sampleCount ) noexcept
    {
        DIVIDE_ASSERT( info.colorAttachmentCount <= to_base( RTColourAttachmentSlot::COUNT ) );
        DIVIDE_ASSERT( info.colorAttachmentCount == 0u || info.pColorAttachmentFormats != nullptr );
        VKRenderingSignature result{};
        result._colourAttachmentCount = info.colorAttachmentCount;
        result._viewMask = info.viewMask;
        result._depthFormat = info.depthAttachmentFormat;
        result._stencilFormat = info.stencilAttachmentFormat;
        result._sampleCount = sampleCount;
        for ( U32 i = 0u; i < info.colorAttachmentCount; ++i )
        {
            result._colourFormats[i] = info.pColorAttachmentFormats[i];
        }
        return result;
    }

    VKPipelineCacheKey VKPipelineCacheKey::From( const Pipeline& pipeline, const VKRenderingSignature& rendering, const bool dynamicBlendState ) noexcept
    {
        return
        {
            ._pipelineStateHash = pipeline.compiledPipelineHash(),
            ._blendStateHash = dynamicBlendState ? 0u : pipeline.blendStateHash(),
            ._colourWrite = dynamicBlendState ? 0u : pipeline.descriptor()._stateBlock._colourWrite.i,
            ._rendering = rendering
        };
    }

    size_t VKPipelineCacheKeyHash::operator()( const VKPipelineCacheKey& key ) const noexcept
    {
        size_t hash = key._pipelineStateHash;
        Util::Hash_combine( hash, key._blendStateHash, key._colourWrite, key._rendering._colourAttachmentCount,
                            key._rendering._viewMask, key._rendering._depthFormat,
                            key._rendering._stencilFormat, key._rendering._sampleCount );
        for ( U32 i = 0u; i < key._rendering._colourAttachmentCount; ++i )
        {
            Util::Hash_combine( hash, key._rendering._colourFormats[i] );
        }
        return hash;
    }

    void VKStateTracker::init( const Configuration& config, VKDevice* device, VKPerWindowState* mainWindow )
    {
        DIVIDE_GPU_ASSERT(device != nullptr && mainWindow != nullptr);
        _immediateSubmitStatus.reset();
        setDefaultState();

        _activeWindow = mainWindow;
        for ( U8 t = 0u; t < to_base(QueueType::COUNT); ++t )
        {
            _cmdContexts[t] = std::make_unique<VKImmediateCmdContext>( config, *device, static_cast<QueueType>(t) );
        }

    }

    void VKStateTracker::reset()
    {
        _cmdContexts = {};
        setDefaultState();
    }

    void VKStateTracker::setDefaultState()
    {
        _pipeline = {};
        _logicalPipeline = nullptr;
        _renderingSignature = {};
        _boundRenderingSignature = {};
        _activeWindow = nullptr;
        _activeMSAASamples = 0u;
        _activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        _activeRenderTargetDimensions = { 1u, 1u };
        _activeRenderTargetColourAttachmentCount = { 1u };
        _drawIndirectBuffer = VK_NULL_HANDLE;
        _drawIndirectBufferOffset = 0u;
        _pipelineStageMask = VK_FLAGS_NONE;
        _pushConstantsValid = false;
        _pipelineRenderInfo = {};
        ResetBoundDescriptorState();
        _pipelineRenderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    }

    VKImmediateCmdContext* VKStateTracker::IMCmdContext( const QueueType type ) const
    {
        return _cmdContexts[to_base( type )].get();
    }

    void VK_API::EnqueueImageBarriers( const std::span<VkImageMemoryBarrier2> barriers )
    {
        if (barriers.empty())
        {
            return;
        }

        UniqueLock<Mutex> w_lock(s_imageBarrierQueue._lock);
        for (const VkImageMemoryBarrier2& barrier : barriers)
        {
            s_imageBarrierQueue._imageBarriers.push_back(barrier);
        }
    }

    void VK_API::RecordOrEnqueueImageBarriers(const std::span<VkImageMemoryBarrier2> barriers)
    {
        if ( barriers.empty() )
        {
            return;
        }

        VkCommandBuffer cmdBuffer = VK_API::GetCurrentCommandBuffer();
        if (cmdBuffer != VK_NULL_HANDLE )
        {
            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = to_U32(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkTexture::FlushPipelineBarriers(cmdBuffer, dependencyInfo );
            return;
        }

        EnqueueImageBarriers( barriers );
    }

    void VK_API::RegisterCustomAPIDelete( DELEGATE<void, VkDevice>&& cbk, const bool isResourceTransient )
    {
        if ( isResourceTransient )
        {
            s_transientDeleteQueue.push( MOV( cbk ) );
        }
        else
        {
            s_deviceDeleteQueue.push( MOV( cbk ) );
        }
    }

    void VK_API::RegisterTransferRequest( const VKTransferQueue::TransferRequest& request )
    {
        s_transferQueue._requests.enqueue(request);
        s_transferQueue._dirty.store(true, std::memory_order_release);
    }

    VK_API::VK_API( GFXDevice& context ) noexcept
        : _context( context )
    {
    }

    VkCommandBuffer VK_API::GetCurrentCommandBuffer() noexcept
    {
        if (FrameData* frameData = nullptr;
            s_stateTracker._activeWindow != nullptr &&
            s_stateTracker._activeWindow->_swapChain->getFrameData(frameData))
        {
            return frameData->_commandBuffer;
        }

        return VK_NULL_HANDLE;
    }

    void VK_API::idle( [[maybe_unused]] const bool fast ) noexcept
    {
        NOP();
    }

    bool VK_API::waitForGPUIdle() noexcept
    {
        if ( _device == nullptr || _device->getVKDevice() == VK_NULL_HANDLE )
        {
            return true;
        }

        const VkResult result = _device->waitIdle();
        if ( result != VK_SUCCESS )
        {
            Console::errorfn( "Vulkan device idle wait failed: {} ({})", VKErrorString( result ).c_str(), to_I32( result ) );
            return false;
        }

        return true;
    }

    bool VK_API::queueBufferReadback( BufferReadbackRequest&& request ) noexcept
    {
        VKPerWindowState* activeWindow = GetStateTracker()._activeWindow;
        if ( activeWindow == nullptr ||
             !activeWindow->_frameInProgress ||
             activeWindow->_swapChain == nullptr ||
             request._buffer == nullptr ||
             request._target.first == nullptr )
        {
            return false;
        }

        const VkFence fence = activeWindow->_swapChain->activeFrameFence();
        if ( fence == VK_NULL_HANDLE )
        {
            return false;
        }

        _pendingBufferReadbacks.push_back( { fence, MOV( request ) } );
        return true;
    }

    void VK_API::pollBufferReadbacks( const VkFence fence, const I64 windowGUID )
    {
        for ( auto it = _pendingBufferReadbacks.begin(); it != _pendingBufferReadbacks.end(); )
        {
            if ( ( fence != VK_NULL_HANDLE && it->_fence != fence ) ||
                 ( windowGUID != I64_LOWEST && it->_request._windowGUID != windowGUID ) )
            {
                ++it;
                continue;
            }

            it->_request._buffer->readData(
                { it->_request._offsetElementCount, it->_request._elementCount },
                it->_request._target,
                it->_request._queueIndex );
            if ( it->_request._completion )
            {
                it->_request._completion( true );
            }
            it = _pendingBufferReadbacks.erase( it );
        }
    }

    void VK_API::discardBufferReadbacks( const I64 windowGUID )
    {
        for ( auto it = _pendingBufferReadbacks.begin(); it != _pendingBufferReadbacks.end(); )
        {
            if ( windowGUID != I64_LOWEST && it->_request._windowGUID != windowGUID )
            {
                ++it;
                continue;
            }

            if ( it->_request._completion )
            {
                it->_request._completion( false );
            }
            it = _pendingBufferReadbacks.erase( it );
        }
    }

    bool VK_API::drawToWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( s_stateTracker._immediateSubmitStatus.failed() )
        {
            return false;
        }

        VKPerWindowState& windowState = _perWindowState[window.getGUID()];
        if ( windowState._window == nullptr )
        {
            windowState._window = &window;
            initStatePerWindow(windowState);
        }

        GetStateTracker()._activeWindow = &windowState;

        if ( windowState._frameInProgress )
        {
            return true;
        }
        if ( windowState._skipEndFrame )
        {
            return false;
        }

        const vec2<U16> windowDimensions = window.getDrawableSize();
        VkExtent2D surfaceExtent = windowState._swapChain->surfaceExtent();

        if ( windowState._needsRecreate ||
             windowDimensions.width != surfaceExtent.width || windowDimensions.height != surfaceExtent.height )
        {
            if ( !recreateSwapChain( windowState ) )
            {
                windowState._needsRecreate = true;
                windowState._skipEndFrame = true;
                return false;
            }
            windowState._needsRecreate = false;
            surfaceExtent = windowState._swapChain->surfaceExtent();
        }

        const VkResult result = windowState._swapChain->beginFrame();
        if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR )
        {
            if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
            {
                ReportVulkanDeviceLost( "Vulkan frame startup", _device.get() );
            }
            if ( result == VK_ERROR_OUT_OF_DATE_KHR )
            {
                windowState._needsRecreate = !recreateSwapChain( windowState );
            }
            else
            {
                Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( result ).c_str() );
                if ( result != VK_ERROR_DEVICE_LOST )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }

            windowState._skipEndFrame = true;
            return false;
        }

        windowState._bufferBindings.reset();
        windowState._frameInProgress = true;
        windowState._recreateAfterPresent = result == VK_SUBOPTIMAL_KHR;
        if ( window.getGUID() == _context.context().mainWindow().getGUID() )
        {
            _context.getPerformanceMetrics()._gpuTimeInMS = windowState._swapChain->lastGPUTimeMS();
        }
        pollBufferReadbacks( windowState._swapChain->activeFrameFence(), window.getGUID() );
        return true;
    }

    void VK_API::onRenderThreadLoopStart( )
    {
    }

    void VK_API::onRenderThreadLoopEnd()
    {
    }

    bool VK_API::prepareFlushWindow( DisplayWindow& window )
    {
        const VKPerWindowState& windowState = _perWindowState[window.getGUID()];
        return !s_stateTracker._immediateSubmitStatus.failed() && windowState._frameInProgress && !windowState._skipEndFrame;
    }

    void VK_API::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VKPerWindowState& windowState = _perWindowState[window.getGUID()];
        assert( windowState._window != nullptr );

        SCOPE_EXIT{
            GetStateTracker()._activeWindow = nullptr;
        };

        if ( s_stateTracker._immediateSubmitStatus.failed() )
        {
            windowState._frameInProgress = false;
            return;
        }

        if ( windowState._skipEndFrame )
        {
            windowState._skipEndFrame = false;
            return;
        }

        if ( !DIVIDE_ASSERT( windowState._frameInProgress, "VK_API::flushWindow: no acquired frame" ) )
        {
            return;
        }

        FlushBufferTransferRequests( GetCurrentCommandBuffer() );

        windowState._activeState = {};

        thread_local VKSubmitSempahore::Container s_localPendingSems;
        {
            LockGuard<Mutex> w_lock(s_submitSemaphores._lock);
            s_localPendingSems = s_submitSemaphores._pendingSubmitSemaphores;
            s_submitSemaphores._pendingSubmitSemaphores.reset_lose_memory();
        }

        const bool mainWindow = window.getGUID() == _context.context().mainWindow().getGUID();
        const U32 presentsBeforeCapture = RenderSmokeTest::PresentCount();
        const bool capture = mainWindow && !windowState._recreateAfterPresent &&
                             windowState._swapChain->currentImageWasRenderedThisFrame() &&
                             RenderSmokeTest::TryBeginCapture();
        VMABuffer_uptr editorReadback{};
        VkExtent2D editorExtent{};
        VkFormat editorFormat = VK_FORMAT_UNDEFINED;
        size_t editorByteCount = 0u;
        if ( capture && RenderSmokeTest::WindowCaptureRequested() )
        {
            const vkb::Swapchain& swapchain = windowState._swapChain->getSwapChain();
            editorExtent = swapchain.extent;
            editorFormat = swapchain.image_format;
            if ( !windowState._swapChain->deferPresentTransition() ||
                 ( swapchain.image_usage_flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0u ||
                 !VKSwapChain::SupportsSmokeReadback( editorFormat ) ||
                 editorExtent.width == 0u || editorExtent.height == 0u ||
                 editorExtent.width > U16_MAX || editorExtent.height > U16_MAX )
            {
                RenderSmokeTest::Fail( "window_capture", "Unsupported swapchain image for editor readback",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            editorByteCount = to_size( editorExtent.width ) * to_size( editorExtent.height ) * 4u;
            editorReadback = VKUtil::createStagingBuffer( editorByteCount, "SMOKE_EDITOR_READBACK", true );
            if ( editorReadback == nullptr || editorReadback->_buffer == VK_NULL_HANDLE ||
                 editorReadback->_allocation == VK_NULL_HANDLE || editorReadback->_allocInfo.pMappedData == nullptr )
            {
                RenderSmokeTest::Fail( "window_capture", "Unable to allocate editor readback buffer",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            const VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();
            VkImageMemoryBarrier2 imageBarrier = VKSwapChain::SmokePresentBarrier( windowState._swapChain->getCurrentImage(), true );

            VkDependencyInfo dependency = vk::dependencyInfo();
            dependency.imageMemoryBarrierCount = 1u;
            dependency.pImageMemoryBarriers = &imageBarrier;
            VK_PROFILE( vkCmdPipelineBarrier2, commandBuffer, &dependency );

            VkBufferImageCopy copy{};
            copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
            copy.imageExtent = { editorExtent.width, editorExtent.height, 1u };
            VK_PROFILE( vkCmdCopyImageToBuffer, commandBuffer, imageBarrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        editorReadback->_buffer, 1u, &copy );

            imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_NONE;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkBufferMemoryBarrier2 bufferBarrier = vk::bufferMemoryBarrier2();
            bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            bufferBarrier.buffer = editorReadback->_buffer;
            bufferBarrier.offset = 0u;
            bufferBarrier.size = editorByteCount;
            dependency.bufferMemoryBarrierCount = 1u;
            dependency.pBufferMemoryBarriers = &bufferBarrier;
            VK_PROFILE( vkCmdPipelineBarrier2, commandBuffer, &dependency );
        }
        else if ( windowState._swapChain->deferPresentTransition() &&
                  windowState._swapChain->currentImageWasRenderedThisFrame() )
        {
            const VkImageMemoryBarrier2 imageBarrier = VKSwapChain::SmokePresentBarrier( windowState._swapChain->getCurrentImage(), false );
            VkDependencyInfo dependency = vk::dependencyInfo();
            dependency.imageMemoryBarrierCount = 1u;
            dependency.pImageMemoryBarriers = &imageBarrier;
            VK_PROFILE( vkCmdPipelineBarrier2, GetCurrentCommandBuffer(), &dependency );
        }

        const VkResult result = windowState._swapChain->endFrame( s_localPendingSems );
        windowState._frameInProgress = false;
        const bool recreateAfterPresent = windowState._recreateAfterPresent;
        windowState._recreateAfterPresent = false;

        RenderSmokeTest::RecordPresent( mainWindow, result == VK_SUCCESS, recreateAfterPresent );

        if ( capture )
        {
            if ( result == VK_ERROR_DEVICE_LOST )
            {
                ReportVulkanDeviceLost( "frame finalization, submission, or presentation during capture", _device.get() );
            }
            if ( result != VK_SUCCESS || recreateAfterPresent || RenderSmokeTest::PresentCount() <= presentsBeforeCapture )
            {
                RenderSmokeTest::Fail( "capture", "The captured scene frame was not presented successfully",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            if ( editorReadback != nullptr )
            {
                const VkFence fence = windowState._swapChain->activeFrameFence();
                const VkResult waitResult = vkWaitForFences( _device->getVKDevice(), 1u, &fence, VK_TRUE, 10'000'000'000ull );
                if ( waitResult == VK_ERROR_DEVICE_LOST )
                {
                    ReportVulkanDeviceLost( "vkWaitForFences during editor capture" );
                }
                if ( waitResult == VK_TIMEOUT )
                {
                    RenderSmokeTest::Fail( "gpu_hang", "Timed out waiting for editor readback",
                                           RenderSmokeTest::DEVICE_LOST_EXIT );
                }
                if ( waitResult != VK_SUCCESS )
                {
                    char detail[128]{};
                    std::snprintf( detail, sizeof( detail ), "Editor readback fence failed: %d", to_I32( waitResult ) );
                    RenderSmokeTest::Fail( "window_capture", detail, RenderSmokeTest::CAPTURE_FAILURE_EXIT );
                }

                vector<Byte> pixels( editorByteCount );
                {
                    LockGuard<Mutex> lock( s_stateTracker._allocatorInstance._allocatorLock );
                    const VkResult invalidateResult = vmaInvalidateAllocation( *s_stateTracker._allocatorInstance._allocator,
                                                                               editorReadback->_allocation, 0u, editorByteCount );
                    if ( invalidateResult == VK_ERROR_DEVICE_LOST )
                    {
                        ReportVulkanDeviceLost( "vmaInvalidateAllocation during editor capture" );
                    }
                    if ( invalidateResult != VK_SUCCESS )
                    {
                        char detail[128]{};
                        std::snprintf( detail, sizeof( detail ), "Editor readback invalidation failed: %d", to_I32( invalidateResult ) );
                        RenderSmokeTest::Fail( "window_capture", detail, RenderSmokeTest::CAPTURE_FAILURE_EXIT );
                    }
                    const std::span<const Byte> mapped{ static_cast<const Byte*>( editorReadback->_allocInfo.pMappedData ),
                                                        editorByteCount };
                    const std::span<Byte> destination{ pixels.data(), pixels.size() };
                    if ( !VKSwapChain::ConvertSmokeReadback( editorFormat, editorExtent.width, editorExtent.height, mapped, destination ) )
                    {
                        RenderSmokeTest::Fail( "window_capture", "Could not convert editor readback pixels",
                                               RenderSmokeTest::CAPTURE_FAILURE_EXIT );
                    }
                }
                const ResourcePath editorPath = ResourcePath{ RenderSmokeTest::OutputDirectory() } / "editor.pending.png";
                if ( !ImageTools::SaveImage( editorPath, to_U16( editorExtent.width ), to_U16( editorExtent.height ),
                                             4u, 4u, false, pixels.data(), ImageTools::SaveImageFormat::PNG ) )
                {
                    RenderSmokeTest::Fail( "window_capture", "Unable to save editor image",
                                           RenderSmokeTest::CAPTURE_FAILURE_EXIT );
                }
            }
            const RenderTarget* target = _context.renderTargetPool().getRenderTarget( RenderTargetNames::BACK_BUFFER );
            const RTAttachment* attachment = target != nullptr
                                           ? target->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO )
                                           : nullptr;
            const Texture* texture = attachment != nullptr ? Get( attachment->texture() ) : nullptr;
            if ( texture == nullptr )
            {
                RenderSmokeTest::Fail( "capture", "Scene colour attachment unavailable", RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            const ImageReadbackData data = static_cast<const vkTexture*>( texture )->readDataForSmoke();
            const ResourcePath path = ResourcePath{ RenderSmokeTest::OutputDirectory() } / "scene.png";
            if ( data._data.empty() ||
                 !ImageTools::SaveImage( path, data._width, data._height, data._numComponents, data._bpp,
                                         data._sourceIsBGR, data._data.data(), ImageTools::SaveImageFormat::PNG ) )
            {
                RenderSmokeTest::FinishCapture( false );
                RenderSmokeTest::Fail( "capture", "Unable to save scene image", RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            RenderSmokeTest::FinishCapture( true );
            if ( editorReadback != nullptr )
            {
                RenderSmokeTest::FinishWindowCapture( true );
            }
        }

        for (VkSemaphore semaphore : s_localPendingSems)
        {
            VK_API::RegisterCustomAPIDelete([semaphore](VkDevice device)
            {
                vkDestroySemaphore(device, semaphore, nullptr);
            }, true);
        }

        s_localPendingSems.reset_lose_memory();

        if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
             (result == VK_SUCCESS && recreateAfterPresent) )
        {
            windowState._needsRecreate = !recreateSwapChain( windowState );
        }
        else if ( result != VK_SUCCESS )
        {
            if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
            {
                ReportVulkanDeviceLost( "frame finalization, submission, or presentation", _device.get() );
            }
            Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( result ).c_str() );
            if ( result != VK_ERROR_DEVICE_LOST )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    bool VK_API::frameStarted()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( s_stateTracker._immediateSubmitStatus.failed() )
        {
            Console::errorfn( "Vulkan immediate submission failed; stopping the renderer." );
            return false;
        }

        const U64 frameNumber = GFXDevice::FrameCount();
        for ( const auto& [windowGUID, windowState] : _perWindowState )
        {
            if ( windowState._swapChain == nullptr )
            {
                continue;
            }

            const VkResult result = windowState._swapChain->waitForFrameSlot( frameNumber );
            if ( result != VK_SUCCESS )
            {
                if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
                {
                    ReportVulkanDeviceLost( "Vulkan frame-slot reuse", _device.get() );
                }
                Console::errorfn( "Vulkan frame-slot reuse failed [window={}, requestedSlot={}]: {} ({})",
                                  windowGUID, VKSwapChain::FrameSlotIndex( frameNumber ),
                                  VKErrorString( result ).c_str(), to_I32( result ) );
                return false;
            }
        }

        for ( U8 i = 0u; i < to_base(DescriptorSetUsage::COUNT); ++i )
        {
            PROFILE_SCOPE( "Flip descriptor pools", Profiler::Category::Graphics);
            auto& pool = s_stateTracker._descriptorAllocators[i];
            if ( pool._frameCount > 1u )
            {
                pool._allocatorPool->Flip();
                pool._handle = pool._allocatorPool->GetAllocator();
            }
        }
        // Pools were flipped (and potentially reset), so any cached shared set from a previous frame may be recycled
        InvalidateSharedDescriptorSets();
        LockManager::CleanExpiredSyncObjects( RenderAPI::Vulkan, frameNumber );

        return true;
    }

    bool VK_API::frameEnded()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        s_transientDeleteQueue.onFrameEnd();
        s_deviceDeleteQueue.onFrameEnd();

        {
            LockGuard<Mutex> lock( s_reloadedShadersLock );
            s_reloadedShaders.swap( _shaderReloadsToProcess );
        }

        if ( !_shaderReloadsToProcess.empty() )
        {
            std::sort( _shaderReloadsToProcess.begin(), _shaderReloadsToProcess.end() );
            for ( auto& it : _compiledPipelines )
            {
                if ( it.second._isValid &&
                     std::binary_search( _shaderReloadsToProcess.begin(), _shaderReloadsToProcess.end(), it.second._programGUID ) )
                {
                    destroyPipeline( it.second, true );
                }
            }
            _shaderReloadsToProcess.clear();
        }

        //vkResetCommandPool(_device->getVKDevice(), _device->graphicsCommandPool(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

        GetStateTracker().setDefaultState();
        return true;
    }

    bool VK_API::recreateSwapChain( VKPerWindowState& windowState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( windowState._window->minimized() )
        {
            idle( false );
            SDLEventManager::pollEvents();
        }
        if ( windowState._window->minimized() )
        {
            return false;
        }

        PROFILE_SCOPE( "Wait for idle", Profiler::Category::Graphics);
        const VkResult idleResult = _device->waitIdle();
        if ( idleResult != VK_SUCCESS )
        {
            Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( idleResult ).c_str() );
            return false;
        }

        pollBufferReadbacks( VK_NULL_HANDLE, windowState._window->getGUID() );

        const ErrorCode err = windowState._swapChain->create( windowState._window->flags() & to_base(WindowFlags::VSYNC),
                                                              _context.context().config().runtime.adaptiveSync,
                                                              windowState._surface );

        DIVIDE_GPU_ASSERT( err == ErrorCode::NO_ERR );

        if ( err == ErrorCode::NO_ERR && windowState._window->getGUID() == _context.context().mainWindow().getGUID() )
        {
            const bool screenSRGB = windowState._swapChain->isSRGB();
            if ( GFXDevice::GetDeviceInformation()._screenSRGB != screenSRGB )
            {
                DeviceInformation deviceInformation = GFXDevice::GetDeviceInformation();
                deviceInformation._screenSRGB = screenSRGB;
                GFXDevice::OverrideDeviceInformation( deviceInformation );
            }

            if ( !screenSRGB )
            {
                Console::warnfn( LOCALE_STR("WARN_VK_NO_SRGB_SWAPCHAIN"), to_base( windowState._swapChain->getSwapChain().image_format ) );
            }
        }

        return err == ErrorCode::NO_ERR;
    }

    void VK_API::initStatePerWindow( VKPerWindowState& windowState)
    {
        DIVIDE_GPU_ASSERT(windowState._window != nullptr);
        if (windowState._surface == nullptr )
        {
            SDL_Vulkan_CreateSurface( windowState._window->getRawWindow(), _vkbInstance.instance, nullptr, &windowState._surface );
            DIVIDE_GPU_ASSERT(windowState._surface != nullptr);
        }

        const bool smokeReadback = RenderSmokeTest::WindowCaptureRequested() &&
                                   windowState._window->getGUID() == _context.context().mainWindow().getGUID();
        windowState._swapChain = std::make_unique<VKSwapChain>( *this, *_device, *windowState._window, smokeReadback );
        windowState._needsRecreate = !recreateSwapChain( windowState );
    }

    void VK_API::destroyStatePerWindow( VKPerWindowState& windowState )
    {
        bool hasPendingReadbacks = false;
        const I64 windowGUID = windowState._window != nullptr ? windowState._window->getGUID() : I64_LOWEST;
        for ( const PendingBufferReadback& pending : _pendingBufferReadbacks )
        {
            hasPendingReadbacks |= pending._request._windowGUID == windowGUID;
        }
        if ( hasPendingReadbacks && _device != nullptr )
        {
            if ( windowState._frameInProgress )
            {
                discardBufferReadbacks( windowGUID );
            }
            else
            {
                const VkResult idleResult = _device->waitIdle();
                if ( idleResult == VK_SUCCESS )
                {
                    pollBufferReadbacks( VK_NULL_HANDLE, windowGUID );
                }
                else
                {
                    Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( idleResult ).c_str() );
                    discardBufferReadbacks( windowGUID );
                }
            }
        }

        windowState._swapChain.reset();

        if ( _vkbInstance.instance != nullptr )
        {
            vkDestroySurfaceKHR( _vkbInstance.instance, windowState._surface, nullptr );
        }

        windowState = {};
    }

    ErrorCode VK_API::initRenderingAPI( [[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, Configuration& config ) noexcept
    {
        g_enableVMADebugLogging.store( config.debug.renderer.enableVMADebugLogging, std::memory_order_relaxed );
        InvalidateSharedDescriptorSets();

        s_transientDeleteQueue.flags( s_transientDeleteQueue.flags() | to_base( VKDeletionQueue::Flags::TREAT_AS_TRANSIENT ) );

        DisplayWindow* window = _context.context().app().windowManager().mainWindow();

        auto systemInfoRet = vkb::SystemInfo::get_system_info();
        if ( !systemInfoRet )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), systemInfoRet.error().message().c_str() );
            return ErrorCode::VK_OLD_HARDWARE;
        }

        //make the Vulkan instance, with basic debug features
        vkb::InstanceBuilder builder{};
        builder.set_app_name( window->title() )
            .set_engine_name( Config::ENGINE_NAME )
            .set_engine_version( Config::ENGINE_VERSION_MAJOR, Config::ENGINE_VERSION_MINOR, Config::ENGINE_VERSION_PATCH )
            .require_api_version( 1, Config::MINIMUM_VULKAN_MINOR_VERSION, 0 )
            .enable_validation_layers( Config::ENABLE_GPU_VALIDATION && config.debug.renderer.enableRenderAPIDebugging )
            .set_debug_callback( divide_debug_callback )
            .set_debug_callback_user_data_pointer( this );

        vkb::SystemInfo& systemInfo = systemInfoRet.value();

        if ( RenderSmokeTest::Enabled() && !RenderSmokeTest::PerfModeRequested() &&
             ( !Config::ENABLE_GPU_VALIDATION || !config.debug.renderer.enableRenderAPIDebugging ||
               !systemInfo.validation_layers_available || !systemInfo.is_extension_available( VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) )
        {
            Console::errorfn( "The Vulkan smoke test requires the Khronos validation layer and VK_EXT_debug_utils." );
            return ErrorCode::RENDER_SMOKE_TEST_FAILED;
        }

        s_hasDebugMarkerSupport = false;
        if ( Config::ENABLE_GPU_VALIDATION && (config.debug.renderer.enableRenderAPIDebugging || config.debug.renderer.enableRenderAPIBestPractices) )
        {
            if ( systemInfo.is_extension_available( VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
            {
                builder.enable_extension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
                builder.add_validation_feature_enable( VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT );
                if (config.debug.renderer.enableRenderAPIBestPractices )
                {
                    builder.add_validation_feature_enable( VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT );
                }
                s_hasDebugMarkerSupport = true;
            }

            if ( systemInfo.validation_layers_available )
            {
                builder.enable_validation_layers();
            }
        }

        auto instanceRet = builder.build();
        if ( !instanceRet )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), instanceRet.error().message().c_str() );
            return ErrorCode::VK_OLD_HARDWARE;
        }

        _vkbInstance = instanceRet.value();

        auto& perWindowContext = _perWindowState[window->getGUID()];
        perWindowContext._window = window;

        // get the surface of the window we opened with SDL
        SDL_Vulkan_CreateSurface( perWindowContext._window->getRawWindow(), _vkbInstance.instance, nullptr, &perWindowContext._surface );

        if ( perWindowContext._surface == nullptr )
        {
            return ErrorCode::VK_SURFACE_CREATE;
        }

        _device = std::make_unique<VKDevice>( _vkbInstance, perWindowContext._surface );

        VkDevice vkDevice = _device->getVKDevice();
        if ( vkDevice == VK_NULL_HANDLE )
        {
            return ErrorCode::VK_DEVICE_CREATE_FAILED;
        }

        if ( _device->getQueue( QueueType::GRAPHICS )._index == INVALID_VK_QUEUE_INDEX )
        {
            return ErrorCode::VK_NO_GRAHPICS_QUEUE;
        }

        if ( _device->getPresentQueueIndex() == INVALID_VK_QUEUE_INDEX )
        {
            return ErrorCode::VK_NO_PRESENT_QUEUE;
        }

        VKQueue graphicsQueue = _device->getQueue( QueueType::GRAPHICS );
        VkPhysicalDevice physicalDevice = _device->getVKPhysicalDevice();
        PROFILE_VK_INIT( &vkDevice, &physicalDevice, &graphicsQueue._queue, &graphicsQueue._index, 1, nullptr);

        if ( s_hasDebugMarkerSupport )
        {
            Debug::vkCmdBeginDebugUtilsLabelEXT  = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(  vkDevice, "vkCmdBeginDebugUtilsLabelEXT"  );
            Debug::vkCmdEndDebugUtilsLabelEXT    = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(    vkDevice, "vkCmdEndDebugUtilsLabelEXT"    );
            Debug::vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr( vkDevice, "vkCmdInsertDebugUtilsLabelEXT" );
            Debug::vkSetDebugUtilsObjectNameEXT  = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(  vkDevice, "vkSetDebugUtilsObjectNameEXT"  );
            Debug::vkSetDebugUtilsObjectTagEXT   = (PFN_vkSetDebugUtilsObjectTagEXT)vkGetDeviceProcAddr(   vkDevice, "vkSetDebugUtilsObjectTagEXT"   );
        }

        s_hasDynamicBlendStateSupport = _device->supportsDynamicExtension3();
        if ( s_hasDynamicBlendStateSupport )
        {
            vkCmdSetColorBlendEnableEXT   = (PFN_vkCmdSetColorBlendEnableEXT)vkGetDeviceProcAddr(   vkDevice, "vkCmdSetColorBlendEnableEXT"   );
            vkCmdSetColorBlendEquationEXT = (PFN_vkCmdSetColorBlendEquationEXT)vkGetDeviceProcAddr( vkDevice, "vkCmdSetColorBlendEquationEXT" );
            vkCmdSetColorWriteMaskEXT     = (PFN_vkCmdSetColorWriteMaskEXT)vkGetDeviceProcAddr(     vkDevice, "vkCmdSetColorWriteMaskEXT"     );
        }

        vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr( vkDevice, "vkCmdPushDescriptorSetKHR" );

        s_hasDescriptorBufferSupport = _device->supportsDescriptorBuffers();
        if ( s_hasDescriptorBufferSupport )
        {
             vkGetDescriptorSetLayoutSizeEXT              = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(              vkDevice, "vkGetDescriptorSetLayoutSizeEXT"             );
             vkGetDescriptorSetLayoutBindingOffsetEXT     = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(     vkDevice, "vkGetDescriptorSetLayoutBindingOffsetEXT"    ); 
             vkGetDescriptorEXT                           = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(                           vkDevice, "vkGetDescriptorEXT"                          ); 
             vkCmdBindDescriptorBuffersEXT                = (PFN_vkCmdBindDescriptorBuffersEXT)vkGetDeviceProcAddr(                vkDevice, "vkCmdBindDescriptorBuffersEXT"               ); 
             vkCmdSetDescriptorBufferOffsetsEXT           = (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetDeviceProcAddr(           vkDevice, "vkCmdSetDescriptorBufferOffsetsEXT"          ); 
             vkCmdBindDescriptorBufferEmbeddedSamplersEXT = (PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT)vkGetDeviceProcAddr( vkDevice, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT"); 
        }

        vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(vkDevice, "vkCmdDrawMeshTasksEXT");

        VKUtil::OnStartup( vkDevice );

        VkFormatProperties2 properties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        VkPhysicalDeviceMaintenance4Properties maintenance4{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES, .pNext = nullptr };
        // Push descriptor limits (PER_DRAW set). This is a properties (query) struct, so it belongs here and not in the device feature chain
        VkPhysicalDevicePushDescriptorProperties pushDescriptorProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES, .pNext = &maintenance4 };
        VkPhysicalDeviceProperties2 properties2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext = &pushDescriptorProperties };

        VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };

        // Maintenance7 exposes updated descriptor set dynamic limits:
        VkPhysicalDeviceMaintenance7PropertiesKHR maintenance7Properties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR };

        // Depth/stencil resolve support struct — query and store supported depth resolve modes
        VkPhysicalDeviceDepthStencilResolveProperties depthResolve{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES, .pNext = nullptr };

        if ( _device->supportsMaintenance7() )
        {
            maintenance7Properties.pNext = properties2.pNext;
            properties2.pNext = &maintenance7Properties;
        }

        // Chain extras into properties2.pNext preserving order. If mesh shaders are supported include them in the chain.
        if ( _device->supportsMeshShaders() )
        {
            // chain: depthResolve -> meshProperties -> maintenance4
            meshProperties.pNext = properties2.pNext;
            depthResolve.pNext = &meshProperties;
            properties2.pNext = &depthResolve;
        }
        else
        {
            // chain: depthResolve -> maintenance4
            depthResolve.pNext = properties2.pNext;
            properties2.pNext = &depthResolve;
        }

        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        VK_API::s_maxPushDescriptors = pushDescriptorProperties.maxPushDescriptors;
        DIVIDE_GPU_ASSERT( VK_API::s_maxPushDescriptors >= MAX_BINDINGS_PER_DESCRIPTOR_SET, "Device does not support enough push descriptors for a full PER_DRAW set!" );

        // Store supported depth resolve modes for runtime decisions
        VK_API::s_supportedDepthResolveModes = depthResolve.supportedDepthResolveModes;

        vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &properties );
        s_depthFormatInformation._d24s8Supported = properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &properties );
        s_depthFormatInformation._d32s8Supported = properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        DIVIDE_GPU_ASSERT( s_depthFormatInformation._d24s8Supported || s_depthFormatInformation._d32s8Supported );


        vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_X8_D24_UNORM_PACK32, &properties );
        s_depthFormatInformation._d24x8Supported = properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_D32_SFLOAT, &properties );
        s_depthFormatInformation._d32FSupported = properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        DIVIDE_GPU_ASSERT( s_depthFormatInformation._d24x8Supported || s_depthFormatInformation._d32FSupported );

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties( physicalDevice, &deviceProperties );

        if (_device->supportsMaintenance7())
        {
            // maintenance7.maxDescriptorSetStorageBuffersDynamic and maxDescriptorSetUniformBuffersDynamic are the modern limits
            VK_API::s_maxDescriptorSetStorageBuffersDynamic = maintenance7Properties.maxDescriptorSetTotalStorageBuffersDynamic;
            VK_API::s_maxDescriptorSetUniformBuffersDynamic = maintenance7Properties.maxDescriptorSetTotalUniformBuffersDynamic;
        }
        else
        {
            // fallback to the limits in VkPhysicalDeviceProperties
            VK_API::s_maxDescriptorSetStorageBuffersDynamic = deviceProperties.limits.maxDescriptorSetStorageBuffersDynamic;
            VK_API::s_maxDescriptorSetUniformBuffersDynamic = deviceProperties.limits.maxDescriptorSetUniformBuffersDynamic;
        }

        DeviceInformation deviceInformation{};
        deviceInformation._renderer = GPURenderer::UNKNOWN;
        switch ( deviceProperties.vendorID )
        {
            case VK_VENDOR_ID_NVIDIA:
                deviceInformation._vendor = GPUVendor::NVIDIA;
                deviceInformation._renderer = GPURenderer::GEFORCE;
                break;
            case VK_VENDOR_ID_INTEL:
                deviceInformation._vendor = GPUVendor::INTEL;
                deviceInformation._renderer = GPURenderer::INTEL;
                break;
            case VK_VENDOR_ID_AMD:
                deviceInformation._vendor = GPUVendor::AMD;
                deviceInformation._renderer = GPURenderer::RADEON;
                break;
            case VK_VENDOR_ID_ARM:
                deviceInformation._vendor = GPUVendor::ARM;
                deviceInformation._renderer = GPURenderer::MALI;
                break;
            case VK_VENDOR_ID_QUALCOMM:
                deviceInformation._vendor = GPUVendor::QUALCOMM;
                deviceInformation._renderer = GPURenderer::ADRENO;
                break;
            case VK_VENDOR_ID_IMGTECH:
                deviceInformation._vendor = GPUVendor::IMAGINATION_TECH;
                deviceInformation._renderer = GPURenderer::POWERVR;
                break;
            case VK_VENDOR_ID_MESA:
                deviceInformation._vendor = GPUVendor::MESA;
                deviceInformation._renderer = GPURenderer::SOFTWARE;
                break;
            default:
                deviceInformation._vendor = GPUVendor::OTHER;
                break;
        }

        Console::printfn( LOCALE_STR( "VK_VENDOR_STRING" ),
                          deviceProperties.deviceName,
                          deviceProperties.vendorID,
                          deviceProperties.deviceID,
                          deviceProperties.driverVersion,
                          deviceProperties.apiVersion );

        if ( RenderSmokeTest::RenderDocCaptureRequested() )
        {
            RenderSmokeTest::ReportVulkanToolEnumerationSkipped();
            Console::warnfn( "Skipping Vulkan tool listing for the supervised RenderDoc capture; validation remains enabled." );
        }
        else
        {
            std::vector<VkPhysicalDeviceToolProperties> tools;
            const VkResult toolResult = EnumeratePhysicalDeviceTools(
                [physicalDevice]( U32* count, VkPhysicalDeviceToolProperties* properties )
                {
                    return vkGetPhysicalDeviceToolProperties( physicalDevice, count, properties );
                },
                tools );
            if ( toolResult != VK_SUCCESS )
            {
                Console::errorfn( "Vulkan tool enumeration failed: {} ({}).", VKErrorString( toolResult ), to_I32( toolResult ) );
                return ErrorCode::PLATFORM_INIT_ERROR;
            }

            if ( !tools.empty() )
            {
                Console::printfn( LOCALE_STR( "VK_TOOL_INFO" ), to_U32( tools.size() ) );
                for ( const VkPhysicalDeviceToolProperties& tool : tools )
                {
                    Console::printfn( "\t{} {}\n", tool.name, tool.version );
                }
            }
        }

        deviceInformation._versionInfo._major = 1u;
        deviceInformation._versionInfo._minor = to_U8( VK_API_VERSION_MINOR( deviceProperties.apiVersion ) );

        deviceInformation._maxBufferSizeBytes = to_size( maintenance4.maxBufferSize );
        deviceInformation._maxTextureUnits = deviceProperties.limits.maxDescriptorSetSampledImages;
        deviceInformation._maxVertAttributeBindings = deviceProperties.limits.maxVertexInputBindings;
        deviceInformation._maxVertAttributes = deviceProperties.limits.maxVertexInputAttributes;
        deviceInformation._maxRTColourAttachments = deviceProperties.limits.maxColorAttachments;
        deviceInformation._maxDrawIndirectCount = deviceProperties.limits.maxDrawIndirectCount;
        deviceInformation._maxTextureSize = deviceProperties.limits.maxImageDimension2D;
        deviceInformation._max3DTextureSize = deviceProperties.limits.maxImageDimension3D;

        DIVIDE_GPU_ASSERT( deviceInformation._maxBufferSizeBytes > 0u );
        Console::printfn(LOCALE_STR("GL_VK_BUFFER_MAX_SIZE"), deviceInformation._maxBufferSizeBytes / 1024 / 1024);

        deviceInformation._shaderCompilerThreads = 0xFFFFFFFF;
        CLAMP( config.rendering.maxAnisotropicFilteringLevel,
               U8_ZERO,
               to_U8( deviceProperties.limits.maxSamplerAnisotropy ) );
        deviceInformation._maxAnisotropy = config.rendering.maxAnisotropicFilteringLevel;

        DIVIDE_GPU_ASSERT( PushConstantsStruct::MaxSize() <= deviceProperties.limits.maxPushConstantsSize );

        const VkSampleCountFlags counts = deviceProperties.limits.framebufferColorSampleCounts & deviceProperties.limits.framebufferDepthSampleCounts;
        U8 maxMSAASamples = 0u;
        if ( counts & VK_SAMPLE_COUNT_2_BIT )
        {
            maxMSAASamples = 2u;
        }
        if ( counts & VK_SAMPLE_COUNT_4_BIT )
        {
            maxMSAASamples = 4u;
        }
        if ( counts & VK_SAMPLE_COUNT_8_BIT )
        {
            maxMSAASamples = 8u;
        }
        if ( counts & VK_SAMPLE_COUNT_16_BIT )
        {
            maxMSAASamples = 16u;
        }
        if ( counts & VK_SAMPLE_COUNT_32_BIT )
        {
            maxMSAASamples = 32u;
        }
        if ( counts & VK_SAMPLE_COUNT_64_BIT )
        {
            maxMSAASamples = 64u;
        }
        // If we do not support MSAA on a hardware level for whatever reason, override user set MSAA levels
        config.rendering.MSAASamples = std::min( config.rendering.MSAASamples, maxMSAASamples );
        config.rendering.shadowMapping.csm.MSAASamples = std::min( config.rendering.shadowMapping.csm.MSAASamples, maxMSAASamples );
        config.rendering.shadowMapping.spot.MSAASamples = std::min( config.rendering.shadowMapping.spot.MSAASamples, maxMSAASamples );
        Attorney::DisplayManagerRenderingAPI::MaxMSAASamples( maxMSAASamples );

        deviceInformation._meshShading._supported = _device->supportsMeshShaders();

        // How many workgroups can we have per compute dispatch
        for ( U8 i = 0u; i < 3; ++i )
        {
            deviceInformation._maxWorkgroupCount[i] = deviceProperties.limits.maxComputeWorkGroupCount[i];
            deviceInformation._maxWorkgroupSize[i]  = deviceProperties.limits.maxComputeWorkGroupSize[i];

            if ( deviceInformation._meshShading._supported )
            {
                deviceInformation._meshShading._mesh._maxWorkgroupCount[i] = meshProperties.maxMeshWorkGroupCount[i];
                deviceInformation._meshShading._mesh._maxWorkgroupSize[i]  = meshProperties.maxMeshWorkGroupSize[i];

                deviceInformation._meshShading._task._maxWorkgroupCount[i] = meshProperties.maxTaskWorkGroupCount[i];
                deviceInformation._meshShading._task._maxWorkgroupSize[i]  = meshProperties.maxTaskWorkGroupSize[i];
            }
        }

        deviceInformation._maxWorkgroupInvocations = deviceProperties.limits.maxComputeWorkGroupInvocations;
        deviceInformation._maxComputeSharedMemoryBytes = deviceProperties.limits.maxComputeSharedMemorySize;
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_WORK_GROUP_INFO" ),
                          deviceInformation._maxWorkgroupCount[0], deviceInformation._maxWorkgroupCount[1], deviceInformation._maxWorkgroupCount[2],
                          deviceInformation._maxWorkgroupSize[0],  deviceInformation._maxWorkgroupSize[1], deviceInformation._maxWorkgroupSize[2],
                          deviceInformation._maxWorkgroupInvocations );
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_SHARED_MEMORY_SIZE" ), deviceInformation._maxComputeSharedMemoryBytes / 1024 );

        // Maximum number of varying components supported as outputs in the vertex shader
        deviceInformation._maxVertOutputComponents = deviceProperties.limits.maxVertexOutputComponents;
        Console::printfn( LOCALE_STR( "MAX_VERTEX_OUTPUT_COMPONENTS" ), deviceInformation._maxVertOutputComponents );

        if (deviceInformation._meshShading._supported)
        {
            deviceInformation._meshShading._mesh._maxOutputVertices   = meshProperties.maxMeshOutputVertices;
            deviceInformation._meshShading._mesh._maxOutputPrimitives = meshProperties.maxMeshOutputPrimitives;
            deviceInformation._meshShading._mesh._maxWorkgroupInvocations   = meshProperties.maxMeshWorkGroupInvocations;
            deviceInformation._meshShading._task._maxWorkgroupInvocations   = meshProperties.maxTaskWorkGroupInvocations;

            Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_VERTICES"),   deviceInformation._meshShading._mesh._maxOutputVertices);
            Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_PRIMITIVES"), deviceInformation._meshShading._mesh._maxOutputPrimitives);
        
            Console::printfn(LOCALE_STR("MAX_MESH_SHADER_WORKGROUP_INFO"),
                                        deviceInformation._meshShading._mesh._maxWorkgroupCount[0], deviceInformation._meshShading._mesh._maxWorkgroupCount[1], deviceInformation._meshShading._mesh._maxWorkgroupCount[2],
                                        deviceInformation._meshShading._mesh._maxWorkgroupSize[0],  deviceInformation._meshShading._mesh._maxWorkgroupSize[1],  deviceInformation._meshShading._mesh._maxWorkgroupSize[2],
                                        deviceInformation._meshShading._mesh._maxWorkgroupInvocations);
                                    
            Console::printfn(LOCALE_STR("MAX_TASK_SHADER_WORKGROUP_INFO"),
                                        deviceInformation._meshShading._task._maxWorkgroupCount[0], deviceInformation._meshShading._task._maxWorkgroupCount[1], deviceInformation._meshShading._task._maxWorkgroupCount[2],
                                        deviceInformation._meshShading._task._maxWorkgroupSize[0],  deviceInformation._meshShading._task._maxWorkgroupSize[1],  deviceInformation._meshShading._task._maxWorkgroupSize[2],
                                        deviceInformation._meshShading._task._maxWorkgroupInvocations);
        }

        deviceInformation._offsetAlignmentBytesUBO = deviceProperties.limits.minUniformBufferOffsetAlignment;
        deviceInformation._nonCoherentAtomSize = to_size( deviceProperties.limits.nonCoherentAtomSize );
        deviceInformation._maxSizeBytesUBO = deviceProperties.limits.maxUniformBufferRange;
        deviceInformation._offsetAlignmentBytesSSBO = deviceProperties.limits.minStorageBufferOffsetAlignment;
        deviceInformation._maxSizeBytesSSBO = deviceProperties.limits.maxStorageBufferRange;
        deviceInformation._maxSSBOBufferBindings = deviceProperties.limits.maxPerStageDescriptorStorageBuffers;

        const bool UBOSizeOver1Mb = deviceInformation._maxSizeBytesUBO / 1024 > 1024;
        Console::printfn( LOCALE_STR( "GL_VK_UBO_INFO" ),
                          deviceProperties.limits.maxDescriptorSetUniformBuffers,
                          (deviceInformation._maxSizeBytesUBO / 1024) / (UBOSizeOver1Mb ? 1024 : 1),
                          UBOSizeOver1Mb ? "Mb" : "Kb",
                          deviceInformation._offsetAlignmentBytesUBO );
        Console::printfn( LOCALE_STR( "GL_VK_SSBO_INFO" ),
                          deviceInformation._maxSSBOBufferBindings,
                          deviceInformation._maxSizeBytesSSBO / 1024 / 1024,
                          deviceProperties.limits.maxDescriptorSetStorageBuffers,
                          deviceInformation._offsetAlignmentBytesSSBO );

        deviceInformation._maxClipAndCullDistances = deviceProperties.limits.maxCombinedClipAndCullDistances;
        deviceInformation._maxClipDistances = deviceProperties.limits.maxClipDistances;
        deviceInformation._maxCullDistances = deviceProperties.limits.maxCullDistances;

        GFXDevice::OverrideDeviceInformation( deviceInformation );

        VK_API::s_stateTracker._device = _device.get();


        DIVIDE_GPU_ASSERT( Config::MINIMUM_VULKAN_MINOR_VERSION >= 4 );

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = vkDevice;
        allocatorInfo.instance = _vkbInstance.instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
        allocatorInfo.preferredLargeHeapBlockSize = 0;

        vmaCreateAllocator( &allocatorInfo, &_allocator );
        GetStateTracker()._allocatorInstance._allocator = &_allocator;

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        vector<Byte> pipeline_data;
        std::ifstream data;
        const FileError errCache = readFile( PipelineCacheLocation(), PipelineCacheFileName.string(), FileType::BINARY, data );
        if ( errCache == FileError::NONE )
        {
            data.seekg(0, std::ios::end);
            const size_t fileSize = to_size(data.tellg());
            data.seekg(0);
            pipeline_data.resize(fileSize);
            data.read(reinterpret_cast<char*>(pipeline_data.data()), fileSize);

            pipelineCacheCreateInfo.initialDataSize = fileSize;
            pipelineCacheCreateInfo.pInitialData = pipeline_data.data();
        }
        else if ( errCache == FileError::FILE_NOT_FOUND )
        {
            Console::warnfn(LOCALE_STR("WARN_VK_PIPELINE_CACHE_LOAD"), Names::fileError[to_base(errCache)]);
        }
        else
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_PIPELINE_CACHE_LOAD" ), Names::fileError[to_base( errCache )] );
        }

        if (data.is_open())
        {
            data.close();
        }
        
        if ( _context.context().config().runtime.usePipelineCache )
        {
            VK_CHECK( vkCreatePipelineCache( vkDevice, &pipelineCacheCreateInfo, nullptr, &_pipelineCache ) );
        }

        initStatePerWindow( perWindowContext );

        s_stateTracker.init( _context.context().config(), _device.get(), &perWindowContext);
        s_stateTracker._assertOnAPIError = &config.debug.renderer.assertOnRenderAPIError;
        s_stateTracker._enabledAPIDebugging = &config.debug.renderer.enableRenderAPIDebugging;

        return ErrorCode::NO_ERR;
    }

    void VK_API::destroyPipeline( CompiledPipeline& pipeline, bool defer )
    {
        if ( !pipeline._isValid )
        {
            // This should be the only place where this flag is set, and, as such, we already handled the destruction of the pipeline
            DIVIDE_GPU_ASSERT( pipeline._vkPipelineLayout == VK_NULL_HANDLE );
            DIVIDE_GPU_ASSERT( pipeline._vkPipeline == VK_NULL_HANDLE );
            DIVIDE_GPU_ASSERT( pipeline._vkPipelineWireframe == VK_NULL_HANDLE );
            return;
        }

        DIVIDE_GPU_ASSERT( pipeline._vkPipelineLayout != VK_NULL_HANDLE );
        DIVIDE_GPU_ASSERT( pipeline._vkPipeline != VK_NULL_HANDLE );

        const auto deletePipeline = [layout = pipeline._vkPipelineLayout, pipeline = pipeline._vkPipeline, wireframePipeline = pipeline._vkPipelineWireframe]( VkDevice device )
        {
            vkDestroyPipelineLayout( device, layout, nullptr );
            vkDestroyPipeline( device, pipeline, nullptr );
            if ( wireframePipeline != VK_NULL_HANDLE )
            {
                vkDestroyPipeline( device, wireframePipeline, nullptr );
            }
        };

        if (!defer )
        {
            deletePipeline(_device->getVKDevice());
        }
        else
        {
            VK_API::RegisterCustomAPIDelete(deletePipeline, true );
        }

        pipeline._vkPipelineLayout = VK_NULL_HANDLE;
        pipeline._vkPipeline = VK_NULL_HANDLE;
        pipeline._vkPipelineWireframe = VK_NULL_HANDLE;
        pipeline._program = nullptr;
        pipeline._programGUID = I64_LOWEST;
        pipeline._isValid = false;
    }

    void VK_API::destroyPipelineCache()
    {
        for ( auto& it : _compiledPipelines )
        {
            destroyPipeline( it.second, false );
        }

        _compiledPipelines.clear();
    }

    void VK_API::closeRenderingAPI()
    {
        vkShaderProgram::DestroyStaticData();
        {
            LockGuard<Mutex> lock( s_reloadedShadersLock );
            s_reloadedShaders.clear();
        }
        _shaderReloadsToProcess.clear();

        // Destroy sampler objects
        {
            for ( auto& sampler : s_samplerMap )
            {
                vkSamplerObject::Destruct( sampler.second );
            }
            s_samplerMap.clear();
        }

        LockManager::Clear();
        if ( _device != nullptr )
        {
            if ( _device->getVKDevice() != VK_NULL_HANDLE )
            {
                const VkResult idleResult = _device->waitIdle();
                VK_CHECK( idleResult );
                if ( idleResult == VK_SUCCESS )
                {
                    discardBufferReadbacks();
                }
                {
                    LockGuard<Mutex> w_lock( s_submitSemaphores._lock );
                    for ( const VkSemaphore semaphore : s_submitSemaphores._pendingSubmitSemaphores )
                    {
                        vkDestroySemaphore( _device->getVKDevice(), semaphore, nullptr );
                    }
                    s_submitSemaphores._pendingSubmitSemaphores.reset_lose_memory();
                }
                s_transientDeleteQueue.flush( _device->getVKDevice(), true );
                s_deviceDeleteQueue.flush( _device->getVKDevice(), true );
                for ( auto& pool : s_stateTracker._descriptorAllocators )
                {
                    pool._handle = {};
                    pool._allocatorPool.reset();
                }
                _descriptorLayoutCache.reset();
                _descriptorSetLayouts.fill( VK_NULL_HANDLE );
                InvalidateSharedDescriptorSets();

                destroyPipelineCache();
            }

            if ( _pipelineCache != nullptr )
            {
                size_t size{};
                VK_CHECK( vkGetPipelineCacheData( _device->getVKDevice(), _pipelineCache, &size, nullptr ) );
                /* Get data of pipeline cache */
                vector<Byte> data( size );
                VK_CHECK( vkGetPipelineCacheData( _device->getVKDevice(), _pipelineCache, &size, data.data() ) );
                /* Write pipeline cache data to a file in binary format */
                const FileError err = writeFile( PipelineCacheLocation(), PipelineCacheFileName.string(), data.data(), size, FileType::BINARY );
                if ( err != FileError::NONE )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_VK_PIPELINE_CACHE_SAVE" ), Names::fileError[to_base( err )] );
                }
                vkDestroyPipelineCache( _device->getVKDevice(), _pipelineCache, nullptr );
            }
            if ( _allocator != VK_NULL_HANDLE )
            {
                VmaTotalStatistics statistics{};
                vmaCalculateStatistics( _allocator, &statistics );
                if ( statistics.total.statistics.allocationCount > 0u )
                {
                    char* allocationDetails = nullptr;
                    vmaBuildStatsString( _allocator, &allocationDetails, VK_TRUE );
                    Console::errorfn( "Vulkan allocator has {} live allocations at shutdown:\n{}",
                                      statistics.total.statistics.allocationCount,
                                      allocationDetails != nullptr ? allocationDetails : "<allocation details unavailable>" );
                    if ( allocationDetails != nullptr )
                    {
                        vmaFreeStatsString( _allocator, allocationDetails );
                    }
                    Console::Flush();
                }
                vmaDestroyAllocator( _allocator );
                _allocator = VK_NULL_HANDLE;
            }

            for ( auto& state : _perWindowState )
            {
                destroyStatePerWindow(state.second);
            }
            _perWindowState.clear();
            s_stateTracker.reset();
            _device.reset();
        }

        vkb::destroy_instance( _vkbInstance );
        _vkbInstance = {};
    }


    bool VK_API::supportsIndirectDrawBatching( const GenericDrawCommand& draw ) const noexcept
    {
        const CompiledPipeline& pipeline = GetStateTracker()._pipeline;
        if ( !pipeline._isValid || pipeline._vkPipeline == VK_NULL_HANDLE ||
             pipeline._bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS || !pipeline._multiDrawCompatible ||
             draw._sourceBuffers == nullptr || draw._sourceBuffersCount != 2u )
        {
            return false;
        }

        std::array<BufferUsageType, 2u> usages{};
        for ( size_t i = 0u; i < draw._sourceBuffersCount; ++i )
        {
            GPUBuffer* buffer = GPUBuffer::s_BufferPool.find( draw._sourceBuffers[i] );
            if ( buffer == nullptr )
            {
                return false;
            }
            const auto* vkBuffer = static_cast<const vkGPUBuffer*>( buffer );
            if ( vkBuffer->_internalBuffer == nullptr )
            {
                return false;
            }
            usages[i] = vkBuffer->_internalBuffer->_params._usageType;
        }
        return IndexedIndirectBatchBuffersCompatible( usages[0], usages[1] );
    }

    bool VK_API::Draw( GenericDrawCommand cmd, VkCommandBuffer cmdBuffer )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        DIVIDE_GPU_ASSERT( cmd._drawCount < GFXDevice::GetDeviceInformation()._maxDrawIndirectCount );

        if ( cmd._sourceBuffersCount == 0u )
        {
            DIVIDE_GPU_ASSERT( cmd._cmd.indexCount == 0u );

            if ( cmd._cmd.vertexCount == 0u )
            {
                GenericDrawCommand drawCmd = cmd;
                switch ( VK_API::GetStateTracker()._pipeline._topology )
                {
                    case PrimitiveTopology::POINTS: drawCmd._cmd.vertexCount = 1u; break;
                    case PrimitiveTopology::LINES:
                    case PrimitiveTopology::LINE_STRIP:
                    case PrimitiveTopology::LINE_STRIP_ADJACENCY:
                    case PrimitiveTopology::LINES_ADJACENCY: drawCmd._cmd.vertexCount = 2u; break;
                    case PrimitiveTopology::TRIANGLES:
                    case PrimitiveTopology::TRIANGLE_STRIP:
                    case PrimitiveTopology::TRIANGLE_FAN:
                    case PrimitiveTopology::TRIANGLES_ADJACENCY:
                    case PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY: drawCmd._cmd.vertexCount = 3u; break;
                    case PrimitiveTopology::PATCH: drawCmd._cmd.vertexCount = 4u; break;
                    default: return false;
                }
                VKUtil::SubmitRenderCommand(drawCmd, cmdBuffer );
            }
            else
            {
                VKUtil::SubmitRenderCommand( cmd, cmdBuffer );
            }

            return true;
        }
        
        bool ret = true;
        bool hasIndexBuffer = false;
        U32 firstIndex = cmd._cmd.firstIndex;
        VKBufferBindingState& bufferBindings = GetStateTracker()._activeWindow->_bufferBindings;

        {
            PROFILE_SCOPE( "Resolve Vulkan draw buffers", Profiler::Category::Graphics );
            for (size_t i = 0u; i < cmd._sourceBuffersCount; ++i)
            {
                GPUBuffer* buffer = GPUBuffer::s_BufferPool.find(cmd._sourceBuffers[i]);

                DIVIDE_GPU_ASSERT(buffer != nullptr, "GL_API::Draw - Invalid GPU buffer handle!");
                auto vkBuffer = static_cast<vkGPUBuffer*>(buffer);
                vkBufferImpl* impl = vkBuffer->_internalBuffer.get();
                DIVIDE_GPU_ASSERT(impl != nullptr, "GL_API::Draw - GPU buffer has no internal implementation!");

                const VkDeviceSize elementSizeInBytes = impl->_params._elementSize;
                VkDeviceSize offset = 0u;

                if (vkBuffer->queueLength() > 1)
                {
                    offset += impl->_params._elementCount * elementSizeInBytes * vkBuffer->queueIndex();
                }

                if ( impl->_params._usageType == BufferUsageType::VERTEX_BUFFER )
                {
                    if ( bufferBindings.updateVertexBinding( vkBuffer->_bindConfig._bindIdx, impl->_buffer, offset ) )
                    {
                        VK_PROFILE(vkCmdBindVertexBuffers, cmdBuffer, vkBuffer->_bindConfig._bindIdx, 1, &impl->_buffer, &offset);
                    }
                }
                else if ( impl->_params._usageType == BufferUsageType::INDEX_BUFFER )
                {
                    DIVIDE_GPU_ASSERT(vkBuffer->firstIndexOffsetCount() != GPUBuffer::INVALID_INDEX_OFFSET);

                    DIVIDE_GPU_ASSERT(!hasIndexBuffer, "GL_API::Draw - Multiple index buffers bound!");
                    hasIndexBuffer = true;

                    firstIndex += vkBuffer->firstIndexOffsetCount();

                    const VkIndexType indexType = elementSizeInBytes == sizeof(U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                    if ( bufferBindings.updateIndexBinding( impl->_buffer, offset, indexType ) )
                    {
                        VK_PROFILE(vkCmdBindIndexBuffer, cmdBuffer, impl->_buffer, offset, indexType);
                    }
                    cmd._cmd.firstIndex = firstIndex;
                }
            }
        }

        VKUtil::SubmitRenderCommand(cmd, cmdBuffer, hasIndexBuffer);

        return ret;
    }

    namespace
    {
        [[nodiscard]] bool IsEmpty( const ShaderProgram::BindingsPerSetArray& bindings ) noexcept
        {
            for ( const auto& binding : bindings )
            {
                if ( binding._type != DescriptorSetBindingType::COUNT && binding._visibility != 0u )
                {
                    return false;
                }
            }

            return true;
        }
    }

    bool VK_API::bindShaderResources( const DescriptorSetEntries& descriptorSetEntries )
    {
        VkCommandBuffer cmdBuffer = GetCurrentCommandBuffer();
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        VKStateTracker& stateTracker = GetStateTracker();
        if ( stateTracker.needsGraphicsPipelineRebind() )
        {
            const Pipeline* logicalPipeline = stateTracker._logicalPipeline;
            if ( bindPipeline( *logicalPipeline, cmdBuffer ) == ShaderResult::Failed )
            {
                const auto handle = logicalPipeline->descriptor()._shaderProgramHandle;
                Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_BIND" ), handle._index, handle._generation );
                stateTracker._pipeline = {};
                stateTracker._logicalPipeline = nullptr;
                return false;
            }
        }

        const auto& pipeline = stateTracker._pipeline;
        if ( pipeline._program == nullptr || pipeline._vkPipeline == VK_NULL_HANDLE )
        {
            Console::errorfn( "Vulkan submission skipped: no valid pipeline is bound." );
            return false;
        }
        auto& program = pipeline._program;
        const bool uniformBindingsChanged = program->commitUniformData( _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).impl() );
        if ( _context.uniformUploadFailed() )
        {
            return false;
        }
        auto& drawDescriptor = program->perDrawDescriptorSetLayout();
        const bool targetDescriptorEmpty = IsEmpty( drawDescriptor );
        const auto& setUsageData = program->setUsage();
        const auto& sharedBindingData = ShaderProgram::GetBindingSetData();

        if ( s_boundDescriptorCmdBuffer != cmdBuffer )
        {
            ResetBoundDescriptorState();
            s_boundDescriptorCmdBuffer = cmdBuffer;
        }

        BoundDescriptorState& boundState = s_boundDescriptorState[pipeline._bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ? 1u : 0u];

        const auto getLayoutBindingType = [&](const DescriptorSetUsage usage, const U8 slot) noexcept
        {
            return usage == DescriptorSetUsage::PER_DRAW
                ? drawDescriptor[slot]._type
                : sharedBindingData[to_base(usage)][slot]._type;
        };

        const auto areCompatibleBindingTypes = [](const DescriptorSetBindingType layoutType,
                                              const DescriptorSetBindingType runtimeType) noexcept
        {
            switch ( layoutType )
            {
                case DescriptorSetBindingType::UNIFORM_BUFFER_STATIC:
                case DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC:
                    return runtimeType == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC ||
                           runtimeType == DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC;

                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC:
                    return runtimeType == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC ||
                           runtimeType == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;

                default:
                    return layoutType == runtimeType;
            }
        };

        // Resolves a single binding into a VK descriptor. Returns false if nothing should be written for this slot.
        const auto resolveSlot = [&]( const DescriptorSetBinding& srcBinding, const DescriptorSetBindingType layoutBindingType, VKDescriptorSlotState& slotOut, U32& dynamicOffsetOut ) -> bool
        {
            switch ( srcBinding._data._type )
            {
                case DescriptorSetBindingType::UNIFORM_BUFFER_STATIC:
                case DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC:
                {
                    const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;
                    DIVIDE_GPU_ASSERT( bufferEntry._buffer != nullptr && bufferEntry._range._length > 0u );

                    const size_t readOffset = bufferEntry._queueReadIndex * bufferEntry._buffer->alignedBufferSize();
                    const VkDeviceSize offset = bufferEntry._range._startOffset * bufferEntry._buffer->getPrimitiveSize() + readOffset;
                    const bool isDynamicBuffer = IsDescriptorSetBindingTypeDynamic( layoutBindingType );

                    slotOut._type = VKUtil::vkDescriptorType( layoutBindingType );
                    slotOut._bufferInfo.buffer = static_cast<vkBufferImpl*>(bufferEntry._buffer->getBufferImpl())->_buffer;
                    slotOut._bufferInfo.offset = isDynamicBuffer ? 0u : offset;
                    slotOut._bufferInfo.range = bufferEntry._range._length * bufferEntry._buffer->getPrimitiveSize();
                    dynamicOffsetOut = isDynamicBuffer ? to_U32( offset ) : 0u;
                    return true;
                }
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                {
                    if ( srcBinding._slot == INVALID_TEXTURE_BINDING )
                    {
                        return false;
                    }

                    const DescriptorCombinedImageSampler& imageSampler = srcBinding._data._sampledImage;
                    if ( imageSampler._image._srcTexture == nullptr ) [[unlikely]]
                    {
                        return false; //unbind request;
                    }

                    DIVIDE_GPU_ASSERT( TargetType( imageSampler._image ) != TextureType::COUNT );

                    const vkTexture* vkTex = static_cast<const vkTexture*>(imageSampler._image._srcTexture);

                    vkTexture::CachedImageView::Descriptor descriptor{};
                    descriptor._usage = ImageUsage::SHADER_READ;
                    descriptor._format = vkTex->vkFormat();
                    descriptor._type = TargetType( imageSampler._image );
                    descriptor._subRange = imageSampler._image._subRange;

                    size_t samplerHash = imageSampler._samplerHash;
                    slotOut._type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    slotOut._imageInfo = vk::descriptorImageInfo( GetSamplerHandle( imageSampler._sampler, samplerHash ),
                                                                  vkTex->getImageView( descriptor ),
                                                                  VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL );
                    return true;
                }
                case DescriptorSetBindingType::IMAGE:
                {
                    const DescriptorImageView& imageView = srcBinding._data._imageView;
                    if ( imageView._image._srcTexture == nullptr )
                    {
                        return false;
                    }

                    DIVIDE_GPU_ASSERT( imageView._image._subRange._mipLevels._count == 1u );

                    const vkTexture* vkTex = static_cast<const vkTexture*>(imageView._image._srcTexture);

                    DIVIDE_GPU_ASSERT(imageView._usage == ImageUsage::SHADER_READ || imageView._usage == ImageUsage::SHADER_WRITE || imageView._usage == ImageUsage::SHADER_READ_WRITE);

                    vkTexture::CachedImageView::Descriptor descriptor{};
                    descriptor._usage = imageView._usage;
                    descriptor._format = vkTex->vkFormat();
                    descriptor._type = TargetType( imageView._image );
                    descriptor._subRange = imageView._image._subRange;

                    // Should use TextureType::TEXTURE_CUBE_ARRAY
                    DIVIDE_GPU_ASSERT( descriptor._type != TextureType::TEXTURE_CUBE_MAP || descriptor._subRange._layerRange._count == 1u );

                    slotOut._type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    slotOut._imageInfo = vk::descriptorImageInfo( VK_NULL_HANDLE,
                                                                  vkTex->getImageView( descriptor ),
                                                                  descriptor._usage == ImageUsage::SHADER_READ ? VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL );
                    return true;
                }
                case DescriptorSetBindingType::INDIRECT_BUFFER: // Not a descriptor. Handled separately
                    return false;
                default:
                case DescriptorSetBindingType::COUNT:
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            };
        };

        thread_local fixed_vector<VkWriteDescriptorSet, MAX_BINDINGS_PER_DESCRIPTOR_SET> descriptorWrites;
        thread_local VKDescriptorSlotStates perDrawSlots;
        thread_local std::array<SampledImageResolutionCache, to_base(DescriptorSetUsage::COUNT)> resolvedSets;

        for ( DescriptorSetEntry entry : descriptorSetEntries )
        {
            SampledImageResolutionCache& resolutionCache = resolvedSets[to_base(entry._usage)];
            const bool hasFallback = program->resolveSampledImages( entry, resolutionCache._resolvedSet );
            const bool alreadyDirty = entry._isDirty ||
                                      (entry._usage == DescriptorSetUsage::PER_DRAW && uniformBindingsChanged);
            entry._isDirty = resolutionCache.update( hasFallback, alreadyDirty ) || alreadyDirty;
            if ( hasFallback )
            {
                entry._set = &resolutionCache._resolvedSet;
            }
            const BaseType<DescriptorSetUsage> usageIdx = to_base( entry._usage );

            // Indirect command buffers are not descriptors (vkCmdDraw*Indirect source), but are specified in the same way as any other
            // resource. The same buffer may still be bound as a regular (writable) SSBO in another slot (e.g. for GPU culling).
            if ( entry._isDirty || GetStateTracker()._drawIndirectBuffer == VK_NULL_HANDLE )
            {
                for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
                {
                    const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];
                    if ( srcBinding._data._type == DescriptorSetBindingType::INDIRECT_BUFFER )
                    {
                        const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;
                        DIVIDE_GPU_ASSERT( bufferEntry._buffer != nullptr && bufferEntry._buffer->getUsage() == BufferUsageType::COMMAND_BUFFER );

                        GetStateTracker()._drawIndirectBuffer = static_cast<vkBufferImpl*>(bufferEntry._buffer->getBufferImpl())->_buffer;
                        GetStateTracker()._drawIndirectBufferOffset = bufferEntry._queueReadIndex * bufferEntry._buffer->alignedBufferSize();
                    }
                }
            }

            if ( entry._usage == DescriptorSetUsage::PER_DRAW )
            {
                if ( entry._isDirty )
                {
                    ++s_perDrawDescriptorVersion;
                }

                if ( !setUsageData[usageIdx] || targetDescriptorEmpty )
                {
                    continue;
                }

                if ( !boundState._perDraw.needsUpdate( pipeline._vkPipelineLayout, s_perDrawDescriptorVersion ) )
                {
                    continue;
                }

                PROFILE_SCOPE( "Push per draw descriptors", Profiler::Category::Graphics );
                boundState._perDraw.beginUpdate( pipeline._vkPipelineLayout );

                for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
                {
                    const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];
                    const DescriptorSetBindingType layoutBindingType = getLayoutBindingType( entry._usage, srcBinding._slot );
                    if ( layoutBindingType == DescriptorSetBindingType::COUNT )
                    {
                        continue;
                    }

                    DIVIDE_GPU_ASSERT( areCompatibleBindingTypes( layoutBindingType, srcBinding._data._type ) );

                    VKDescriptorSlotState& slot = perDrawSlots[srcBinding._slot];
                    U32 dynamicOffset = 0u;
                    if ( resolveSlot( srcBinding, layoutBindingType, slot, dynamicOffset ) )
                    {
                        DIVIDE_GPU_ASSERT( !IsDescriptorSetBindingTypeDynamic( layoutBindingType ) );
                        if ( boundState._perDraw.updateSlot( srcBinding._slot, slot ) )
                        {
                            descriptorWrites.push_back( slot.isImage()
                                                            ? vk::writeDescriptorSet( slot._type, srcBinding._slot, &slot._imageInfo, 1u )
                                                            : vk::writeDescriptorSet( slot._type, srcBinding._slot, &slot._bufferInfo, 1u ) );
                        }
                    }
                }

                if ( !descriptorWrites.empty() )
                {
                    VK_PROFILE( vkCmdPushDescriptorSetKHR, cmdBuffer,
                                                           pipeline._bindPoint,
                                                           pipeline._vkPipelineLayout,
                                                           to_base( DescriptorSetUsage::PER_DRAW ),
                                                           to_U32( descriptorWrites.size() ),
                                                           descriptorWrites.data() );
                    descriptorWrites.reset_lose_memory();
                }

                boundState._perDraw.finishUpdate( s_perDrawDescriptorVersion );
                continue;
            }

            SharedDescriptorSetCache& cache = s_sharedDescriptorSets[usageIdx];
            cache._pendingUpdate = cache._pendingUpdate || entry._isDirty;

            // Keep the pending flag if the current program doesn't use this set. We'll process it when a program needs it.
            if ( !setUsageData[usageIdx] || (!cache._pendingUpdate && cache._set != VK_NULL_HANDLE) )
            {
                continue;
            }

            PROFILE_SCOPE( "Update shared descriptor set", Profiler::Category::Graphics );
            PROFILE_TAG( "Usage IDX", usageIdx );

            VKDescriptorSlotStates desiredSlots{};
            for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
            {
                const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];
                const DescriptorSetBindingType layoutBindingType = getLayoutBindingType( entry._usage, srcBinding._slot );

                if ( layoutBindingType == DescriptorSetBindingType::COUNT || layoutBindingType == DescriptorSetBindingType::INDIRECT_BUFFER )
                {
                    continue;
                }

                DIVIDE_GPU_ASSERT( areCompatibleBindingTypes( layoutBindingType, srcBinding._data._type ) );

                U32 dynamicOffset = 0u;
                if ( resolveSlot( srcBinding, layoutBindingType, desiredSlots[srcBinding._slot], dynamicOffset ) &&
                     IsDescriptorSetBindingTypeDynamic( layoutBindingType ) )
                {
                    bool foundDynamicBinding = false;
                    for ( DynamicBinding& dynamicBinding : _descriptorDynamicBindings[usageIdx] )
                    {
                        if ( dynamicBinding._slot == srcBinding._slot )
                        {
                            dynamicBinding._offset = dynamicOffset;
                            foundDynamicBinding = true;
                            break;
                        }
                    }
                    DIVIDE_GPU_ASSERT( foundDynamicBinding );
                }
            }
            cache._pendingUpdate = false;

            if ( cache._set != VK_NULL_HANDLE && cache._slots == desiredSlots )
            {
                // Same resources. Only (potentially) different dynamic offsets which are handled at bind time
                continue;
            }

            PROFILE_SCOPE( "Build and update set", Profiler::Category::Graphics );

            DescriptorAllocator& allocator = s_stateTracker._descriptorAllocators[usageIdx];
            if ( allocator._handle.ownerPool == nullptr )
            {
                allocator._handle = allocator._allocatorPool->GetAllocator();
            }
            DescriptorBuilder builder = DescriptorBuilder::Begin( _descriptorLayoutCache.get(), &allocator._handle );
            VkDescriptorSet newSet = VK_NULL_HANDLE;
            if ( !builder.buildSetFromLayout( newSet, _descriptorSetLayouts[usageIdx], _device->getVKDevice() ) )
            {
                cache._pendingUpdate = true;
                return false;
            }
            cache._set = newSet;
            cache._slots = desiredSlots;

            for ( U8 slotIdx = 0u; slotIdx < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slotIdx )
            {
                const VKDescriptorSlotState& slot = cache._slots[slotIdx];
                if ( slot._type == VK_DESCRIPTOR_TYPE_MAX_ENUM )
                {
                    continue;
                }

                VkWriteDescriptorSet& write = descriptorWrites.emplace_back( slot.isImage()
                                                                                ? vk::writeDescriptorSet( slot._type, slotIdx, &slot._imageInfo, 1u )
                                                                                : vk::writeDescriptorSet( slot._type, slotIdx, &slot._bufferInfo, 1u ) );
                write.dstSet = cache._set;
            }

            if ( !descriptorWrites.empty() )
            {
                VK_PROFILE( vkUpdateDescriptorSets, _device->getVKDevice(), to_U32( descriptorWrites.size() ), descriptorWrites.data(), 0, nullptr );
                descriptorWrites.reset_lose_memory();
            }
        }

        {
            // Bind every shared set the program uses whose handle or dynamic offsets differ from what is already bound on this bind point.
            // Shared set layouts (and the push constant range) are identical in every pipeline layout and occupy the lowest set indices,
            // so these bindings stay valid across pipeline changes. Contiguous runs are batched in a single call.
            thread_local VkDescriptorSet runSets[to_base( DescriptorSetUsage::COUNT )];
            thread_local fixed_vector<U32, MAX_BINDINGS_PER_DESCRIPTOR_SET * to_base( DescriptorSetUsage::COUNT )> runOffsets;

            U8 runStart = U8_MAX;
            U8 runCount = 0u;

            const auto flushRun = [&]()
            {
                if ( runCount > 0u )
                {
                    PROFILE_SCOPE( "Bind descriptor sets", Profiler::Category::Graphics );
                    VK_PROFILE( vkCmdBindDescriptorSets, cmdBuffer,
                                                         pipeline._bindPoint,
                                                         pipeline._vkPipelineLayout,
                                                         runStart,
                                                         runCount,
                                                         runSets,
                                                         to_U32( runOffsets.size() ),
                                                         runOffsets.data() );
                }
                runStart = U8_MAX;
                runCount = 0u;
                runOffsets.reset_lose_memory();
            };

            for ( U8 vkSet = 0u; vkSet < to_base( DescriptorSetUsage::COUNT ); ++vkSet )
            {
                const DescriptorSetUsage usage = static_cast<DescriptorSetUsage>( vkSet );
                const U8 usageIdx = vkSet;

                bool needsBind = false;
                if ( usage != DescriptorSetUsage::PER_DRAW && setUsageData[usageIdx] )
                {
                    const SharedDescriptorSetCache& cache = s_sharedDescriptorSets[usageIdx];
                    DIVIDE_GPU_ASSERT( cache._set != VK_NULL_HANDLE );

                    needsBind = boundState._sets[vkSet] != cache._set;

                    const DynamicBindings& dynamicBindings = _descriptorDynamicBindings[usageIdx];
                    for ( size_t d = 0u; d < dynamicBindings.size(); ++d )
                    {
                        if ( boundState._dynamicOffsets[vkSet][d] != dynamicBindings[d]._offset )
                        {
                            needsBind = true;
                            boundState._dynamicOffsets[vkSet][d] = dynamicBindings[d]._offset;
                        }
                    }

                    if ( needsBind )
                    {
                        boundState._sets[vkSet] = cache._set;
                        if ( runCount == 0u )
                        {
                            runStart = vkSet;
                        }
                        runSets[runCount++] = cache._set;
                        for ( const DynamicBinding& binding : dynamicBindings )
                        {
                            runOffsets.push_back( binding._offset );
                        }
                    }
                }

                if ( !needsBind )
                {
                    flushRun();
                }
            }
            flushRun();
        }

        return true;
    }

    void VK_API::bindDynamicState( const RenderStateBlock& currentState, const RTBlendStates& blendStates, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        bool ret = false;

        auto& activeState = GetStateTracker()._activeWindow->_activeState;

        if ( activeState.needsStencilTestUpdate( currentState._stencilEnabled ) )
        {
            vkCmdSetStencilTestEnable( cmdBuffer, currentState._stencilEnabled ? VK_TRUE : VK_FALSE );
            ret = true;
        }

        if ( currentState._stencilEnabled )
        {
            const bool resetStencilValues = activeState.needsStencilValuesReset();
            if ( resetStencilValues || activeState._block._stencilMask != currentState._stencilMask )
            {
                vkCmdSetStencilCompareMask( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilMask );
                ret = true;
            }
            if ( resetStencilValues || activeState._block._stencilWriteMask != currentState._stencilWriteMask )
            {
                vkCmdSetStencilWriteMask( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilWriteMask );
                ret = true;
            }
            if ( resetStencilValues || activeState._block._stencilRef != currentState._stencilRef )
            {
                vkCmdSetStencilReference( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilRef );
                ret = true;
            }
            if ( resetStencilValues ||
                  activeState._block._stencilFailOp != currentState._stencilFailOp ||
                  activeState._block._stencilPassOp != currentState._stencilPassOp ||
                  activeState._block._stencilZFailOp != currentState._stencilZFailOp ||
                  activeState._block._stencilFunc != currentState._stencilFunc )
            {
                vkCmdSetStencilOp(cmdBuffer,
                                  VK_STENCIL_FACE_FRONT_AND_BACK,
                                  vkStencilOpTable[to_base( currentState._stencilFailOp )],
                                  vkStencilOpTable[to_base( currentState._stencilPassOp )],
                                  vkStencilOpTable[to_base( currentState._stencilZFailOp )],
                                  vkCompareFuncTable[to_base( currentState._stencilFunc )]);
                ret = true;
            }
        }

        if ( !activeState._isSet || activeState._block._zFunc != currentState._zFunc )
        {
            vkCmdSetDepthCompareOp( cmdBuffer, vkCompareFuncTable[to_base( currentState._zFunc )] );
            ret = true;
        }

        if ( !activeState._isSet || activeState._block._depthWriteEnabled != currentState._depthWriteEnabled )
        {
            vkCmdSetDepthWriteEnable( cmdBuffer, currentState._depthWriteEnabled );
            ret = true;
        }

        if ( !activeState._isSet || !COMPARE( activeState._block._zBias, currentState._zBias ) || !COMPARE( activeState._block._zUnits, currentState._zUnits ) )
        {
            if ( !IS_ZERO( currentState._zBias ) )
            {
                vkCmdSetDepthBiasEnable(cmdBuffer, VK_TRUE);
                vkCmdSetDepthBias( cmdBuffer, currentState._zUnits, 0.f, currentState._zBias );
            }
            else
            {
                vkCmdSetDepthBiasEnable( cmdBuffer, VK_FALSE );
            }
            ret = true;
        }

        if ( !activeState._isSet || activeState._block._cullMode != currentState._cullMode )
        {
            vkCmdSetCullMode( cmdBuffer, vkCullModeTable[to_base( currentState._cullMode )] );
            ret = true;
        }

        // Offscreen targets use an unflipped viewport (see setViewportInternal), which mirrors triangles in framebuffer space, so winding must be inverted to match OpenGL
        const bool invertFrontFace = !IsSwapChainTargetActive();
        if ( !activeState._isSet ||
             activeState._block._frontFaceCCW != currentState._frontFaceCCW ||
             activeState._frontFaceInverted != invertFrontFace )
        {
            vkCmdSetFrontFace( cmdBuffer, (currentState._frontFaceCCW != invertFrontFace) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE );
            activeState._frontFaceInverted = invertFrontFace;
            ret = true;
        }

        // Scissor rects and the scissor test flag are independent state (as in OpenGL), so re-apply the last requested rect whenever the test toggles
        const bool scissorTestChanged = !activeState._isSet || activeState._block._scissorTestEnabled != currentState._scissorTestEnabled;
        ret = ret || scissorTestChanged;

        if ( !activeState._isSet || activeState._block._depthTestEnabled != currentState._depthTestEnabled )
        {
            vkCmdSetDepthTestEnable( cmdBuffer, currentState._depthTestEnabled );
            ret = true;
        }

        if ( !activeState._isSet || activeState._block._rasterizationEnabled != currentState._rasterizationEnabled )
        {
            vkCmdSetRasterizerDiscardEnable( cmdBuffer, !currentState._rasterizationEnabled );
            ret = true;
        }

        if ( !activeState._isSet || activeState._block._primitiveRestartEnabled != currentState._primitiveRestartEnabled )
        {
            vkCmdSetPrimitiveRestartEnable( cmdBuffer, currentState._primitiveRestartEnabled );
            ret = true;
        }

        if ( s_hasDynamicBlendStateSupport )
        {
            constexpr U8 attCount = to_base(RTColourAttachmentSlot::COUNT);
            if ( !activeState._isSet || activeState._block._colourWrite != currentState._colourWrite )
            {
                VkColorComponentFlags writeMask[attCount];
                const VkColorComponentFlags colourFlags = (currentState._colourWrite.b[0] == 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
                                                          (currentState._colourWrite.b[1] == 1 ? VK_COLOR_COMPONENT_G_BIT : 0) |
                                                          (currentState._colourWrite.b[2] == 1 ? VK_COLOR_COMPONENT_B_BIT : 0) |
                                                          (currentState._colourWrite.b[3] == 1 ? VK_COLOR_COMPONENT_A_BIT : 0);
                std::fill(writeMask, writeMask + attCount, colourFlags);
                vkCmdSetColorWriteMaskEXT( cmdBuffer, 0, attCount, writeMask );
                ret = true;
            }

            if ( !activeState._isSet || activeState._blendStates != blendStates )
            {
                VkBool32 blendEnabled[attCount];
                VkColorBlendEquationEXT blendEquations[attCount];

                for ( U8 i = 0u; i < attCount; ++i )
                {
                    const BlendingSettings& blendState = blendStates._settings[i];

                    blendEnabled[i] = blendState.enabled() ? VK_TRUE : VK_FALSE;

                    auto& equation = blendEquations[i];
                    equation.srcColorBlendFactor = vkBlendTable[to_base( blendState.blendSrc() )];
                    equation.dstColorBlendFactor = vkBlendTable[to_base( blendState.blendDest() )];
                    equation.colorBlendOp = vkBlendOpTable[to_base( blendState.blendOp() )];
                    if ( blendState.blendOpAlpha() != BlendOperation::COUNT )
                    {
                        equation.alphaBlendOp = vkBlendOpTable[to_base( blendState.blendOpAlpha() )];
                        equation.dstAlphaBlendFactor = vkBlendTable[to_base( blendState.blendDestAlpha() )];
                        equation.srcAlphaBlendFactor = vkBlendTable[to_base( blendState.blendSrcAlpha() )];
                    }
                    else
                    {
                        equation.srcAlphaBlendFactor = equation.srcColorBlendFactor;
                        equation.dstAlphaBlendFactor = equation.dstColorBlendFactor;
                        equation.alphaBlendOp = equation.colorBlendOp;
                    }
                }

                vkCmdSetColorBlendEnableEXT(cmdBuffer, 0, attCount, blendEnabled);
                vkCmdSetColorBlendEquationEXT(cmdBuffer, 0, attCount, blendEquations);

                activeState._blendStates = blendStates;
                ret = true;
            }
        }

        if ( ret )
        {
            activeState._block = currentState;
            activeState._isSet = true;
        }

        if ( scissorTestChanged )
        {
            setScissorInternal( _context.activeScissor(), cmdBuffer );
        }
    }

    ShaderResult VK_API::bindPipeline( const Pipeline& pipeline, VkCommandBuffer cmdBuffer )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        VKStateTracker& stateTracker = GetStateTracker();
        const VKPipelineCacheKey key = VKPipelineCacheKey::From( pipeline, stateTracker._renderingSignature, s_hasDynamicBlendStateSupport );
        CompiledPipeline& compiledPipeline = _compiledPipelines[key];
        if ( !compiledPipeline._isValid )
        {
            PROFILE_SCOPE( "Compile PSO", Profiler::Category::Graphics);

            thread_local RenderStateBlock defaultState{};

            const PipelineDescriptor& pipelineDescriptor = pipeline.descriptor();
            ShaderProgram* program = Get( pipelineDescriptor._shaderProgramHandle );
            if ( program == nullptr )
            {
                const auto handle = pipelineDescriptor._shaderProgramHandle;
                Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_HANDLE" ), handle._index, handle._generation );
                return ShaderResult::Failed;
            }

            compiledPipeline._program = static_cast<vkShaderProgram*>(program);
            compiledPipeline._programGUID = program->getGUID();
            compiledPipeline._topology = pipelineDescriptor._primitiveTopology;
            const RenderStateBlock& currentState = pipelineDescriptor._stateBlock;

            VkPushConstantRange push_constant;
            push_constant.offset = 0u;
            push_constant.size = to_U32( PushConstantsStruct::MaxSize() );
            // Identical push constant range in every pipeline layout keeps all layouts compatible for push constants and shared sets
            push_constant.stageFlags = VK_SHADER_STAGE_ALL;
            compiledPipeline._stageFlags = push_constant.stageFlags;

            VkPipelineLayoutCreateInfo pipeline_layout_info = vk::pipelineLayoutCreateInfo( 0u );
            pipeline_layout_info.pPushConstantRanges = &push_constant;
            pipeline_layout_info.pushConstantRangeCount = 1;

            const ShaderProgram::BindingsPerSetArray& drawLayout = compiledPipeline._program->perDrawDescriptorSetLayout();

            DynamicBindings dynamicBindings{};
            const VkDescriptorSetLayout perDrawLayout = createLayoutFromBindings(DescriptorSetUsage::PER_DRAW, drawLayout, dynamicBindings);
            compiledPipeline._program->dynamicBindings(dynamicBindings);
            compiledPipeline._program->descriptorSetLayout( perDrawLayout );

            // Shared sets come from the global layouts. Only PER_DRAW is per-program, so build the array locally instead of mutating shared state.
            std::array<VkDescriptorSetLayout, to_base( DescriptorSetUsage::COUNT )> setLayouts = _descriptorSetLayouts;
            setLayouts[to_base( DescriptorSetUsage::PER_DRAW )] = perDrawLayout;
            
            // Sets are ordered by update frequency (PER_FRAME = 0 ... PER_DRAW = last). Shared sets always use their real layouts
            // (even if unused by this program) so that every pipeline layout is compatible for them and bound sets survive pipeline changes.
            pipeline_layout_info.pSetLayouts = setLayouts.data();
            pipeline_layout_info.setLayoutCount = to_base( DescriptorSetUsage::COUNT );

            VK_CHECK( vkCreatePipelineLayout( _device->getVKDevice(), &pipeline_layout_info, nullptr, &compiledPipeline._vkPipelineLayout ) );

            //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
            const auto& shaderStages = compiledPipeline._program->shaderStages();

            PipelineBuilder pipelineBuilder;

            bool isGraphicsPipeline = false;
            bool hasVertexStage = false;
            bool multiDrawCompatible = true;
            const bool tracePipeline = RenderSmokeTest::PipelineTraceRequested();
            std::array<RenderSmokeTest::PipelineTraceStage, to_base( ShaderType::COUNT )> traceStages{};
            size_t traceStageCount = 0u;
            for ( const auto& stage : shaderStages )
            {
                pipelineBuilder._shaderStages.push_back( vk::pipelineShaderStageCreateInfo( stage._shader->stageMask(), stage._shader->handle() ) );
                isGraphicsPipeline = isGraphicsPipeline || stage._shader->stageMask() != VK_SHADER_STAGE_COMPUTE_BIT;
                hasVertexStage = hasVertexStage || stage._shader->stageMask() == VK_SHADER_STAGE_VERTEX_BIT;
                multiDrawCompatible = multiDrawCompatible && stage._shader->multiDrawCompatible();
                if ( tracePipeline )
                {
                    traceStages[traceStageCount++] =
                    {
                        ._name = stage._shader->name().c_str(),
                        ._module = ShaderModuleHandleBits( stage._shader->handle() ),
                        ._stage = to_U32( stage._shader->stageMask() ),
                        ._generation = stage._shader->generation()
                    };
                }
            }
            compiledPipeline._bindPoint = isGraphicsPipeline ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
            compiledPipeline._multiDrawCompatible = isGraphicsPipeline && hasVertexStage && multiDrawCompatible;

            pipelineBuilder._vertexInputInfo = vk::pipelineVertexInputStateCreateInfo();
            const VertexInputDescription vertexDescription = getVertexDescription( pipelineDescriptor._vertexFormat,
                                                                                   compiledPipeline._program->vertexInputs() );
            pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
            pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = to_U32( vertexDescription.attributes.size() );
            pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
            pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = to_U32( vertexDescription.bindings.size() );

            //input assembly is the configuration for drawing triangle lists, strips, or individual points.
            //we are just going to draw triangle list
            pipelineBuilder._inputAssembly = vk::pipelineInputAssemblyStateCreateInfo( vkPrimitiveTypeTable[to_base( pipelineDescriptor._primitiveTopology )], 0u, defaultState._primitiveRestartEnabled );
            //configure the rasterizer to draw filled triangles
            pipelineBuilder._rasterizer = vk::pipelineRasterizationStateCreateInfo(
                vkFillModeTable[to_base( currentState._fillMode )],
                vkCullModeTable[to_base( defaultState._cullMode)],
                defaultState._frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
            pipelineBuilder._rasterizer.rasterizerDiscardEnable = !defaultState._rasterizationEnabled;

            VkSampleCountFlagBits msaaSampleFlags = VK_SAMPLE_COUNT_1_BIT;
            const U8 msaaSamples = GetStateTracker()._activeMSAASamples;
            if ( msaaSamples > 0u )
            {
                assert( isPowerOfTwo( msaaSamples ) );
                msaaSampleFlags = static_cast<VkSampleCountFlagBits>(msaaSamples);
            }
            // The initializer disables forced per-sample shading, matching OpenGL. Sample-qualified shaders still run per sample.
            pipelineBuilder._multisampling = vk::pipelineMultisampleStateCreateInfo( msaaSampleFlags );
            pipelineBuilder._multisampling.alphaToCoverageEnable = pipelineDescriptor._alphaToCoverage ? VK_TRUE : VK_FALSE;
            VkStencilOpState stencilOpState{};
            stencilOpState.failOp = vkStencilOpTable[to_base( defaultState._stencilFailOp )];
            stencilOpState.passOp = vkStencilOpTable[to_base( defaultState._stencilPassOp )];
            stencilOpState.depthFailOp = vkStencilOpTable[to_base( defaultState._stencilZFailOp )];
            stencilOpState.compareOp = vkCompareFuncTable[to_base( defaultState._stencilFunc )];
            stencilOpState.compareMask = defaultState._stencilMask;
            stencilOpState.writeMask = defaultState._stencilWriteMask;
            stencilOpState.reference = defaultState._stencilRef;

            pipelineBuilder._depthStencil = vk::pipelineDepthStencilStateCreateInfo( defaultState._depthTestEnabled, defaultState._depthWriteEnabled, vkCompareFuncTable[to_base(defaultState._zFunc)]);
            pipelineBuilder._depthStencil.stencilTestEnable = defaultState._stencilEnabled;
            pipelineBuilder._depthStencil.front = stencilOpState;
            pipelineBuilder._depthStencil.back = stencilOpState;
            pipelineBuilder._rasterizer.depthBiasEnable = !IS_ZERO(defaultState._zBias);
            pipelineBuilder._rasterizer.depthBiasConstantFactor = defaultState._zUnits;
            pipelineBuilder._rasterizer.depthBiasClamp = defaultState._zUnits;
            pipelineBuilder._rasterizer.depthBiasSlopeFactor = defaultState._zBias;

            if ( !s_hasDynamicBlendStateSupport )
            {
                const P32 cWrite = currentState._colourWrite;
                VkPipelineColorBlendAttachmentState blend = vk::pipelineColorBlendAttachmentState(
                    (cWrite.b[0] == 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
                    (cWrite.b[1] == 1 ? VK_COLOR_COMPONENT_G_BIT : 0) |
                    (cWrite.b[2] == 1 ? VK_COLOR_COMPONENT_B_BIT : 0) |
                    (cWrite.b[3] == 1 ? VK_COLOR_COMPONENT_A_BIT : 0),
                    VK_FALSE );

                const size_t attCount = std::min<size_t>(GetStateTracker()._activeRenderTargetColourAttachmentCount, to_base(RTColourAttachmentSlot::COUNT));
                for ( U8 i = 0u; i < attCount; ++i )
                {
                    const BlendingSettings& blendState = pipelineDescriptor._blendStates._settings[i];

                    blend.blendEnable = blendState.enabled() ? VK_TRUE : VK_FALSE;
                    blend.colorBlendOp = vkBlendOpTable[to_base( blendState.blendOp() )];
                    blend.srcColorBlendFactor = vkBlendTable[to_base( blendState.blendSrc() )];
                    blend.dstColorBlendFactor = vkBlendTable[to_base( blendState.blendDest() )];
                    if ( blendState.blendOpAlpha() != BlendOperation::COUNT )
                    {
                        blend.alphaBlendOp = vkBlendOpTable[to_base( blendState.blendOpAlpha() )];
                        blend.dstAlphaBlendFactor = vkBlendTable[to_base( blendState.blendDestAlpha() )];
                        blend.srcAlphaBlendFactor = vkBlendTable[to_base( blendState.blendSrcAlpha() )];
                    }
                    else
                    {
                        blend.srcAlphaBlendFactor = blend.srcColorBlendFactor;
                        blend.dstAlphaBlendFactor = blend.dstColorBlendFactor;
                        blend.alphaBlendOp = blend.colorBlendOp;
                    }

                    pipelineBuilder._colorBlendAttachments.emplace_back( blend );
                }
            }

            //use the triangle layout we created
            pipelineBuilder._pipelineLayout = compiledPipeline._vkPipelineLayout;
            pipelineBuilder._tessellation = vk::pipelineTessellationStateCreateInfo( currentState._tessControlPoints );

            const std::span<const RenderSmokeTest::PipelineTraceStage> pipelineTrace{ traceStages.data(), traceStageCount };
            if ( tracePipeline )
            {
                RenderSmokeTest::ReportPipelineTrace( program->resourceName().c_str(), program->getGUID(), isGraphicsPipeline, false,
                                                     pipelineTrace );
            }
            compiledPipeline._vkPipeline = pipelineBuilder.build_pipeline( _device->getVKDevice(), _pipelineCache, isGraphicsPipeline );

            if ( isGraphicsPipeline && IsTriangles( pipelineDescriptor._primitiveTopology ) )
            {
                pipelineBuilder._rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
                if ( tracePipeline )
                {
                    RenderSmokeTest::ReportPipelineTrace( program->resourceName().c_str(), program->getGUID(), true, true, pipelineTrace );
                }
                compiledPipeline._vkPipelineWireframe = pipelineBuilder.build_pipeline( _device->getVKDevice(), _pipelineCache, true );

                Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipelineWireframe, VK_OBJECT_TYPE_PIPELINE, Util::StringFormat("{}_wireframe", program->resourceName().c_str()).c_str());
            }

            Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, program->resourceName().c_str() );
            Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipeline, VK_OBJECT_TYPE_PIPELINE, program->resourceName().c_str() );

            compiledPipeline._isValid = true;
        }

        VK_PROFILE( vkCmdBindPipeline, cmdBuffer, compiledPipeline._bindPoint, compiledPipeline._vkPipeline );

        stateTracker._pipeline = compiledPipeline;
        stateTracker._logicalPipeline = &pipeline;
        stateTracker._boundRenderingSignature = stateTracker._renderingSignature;
        if ( stateTracker._pipelineStageMask != compiledPipeline._stageFlags )
        {
            stateTracker._pipelineStageMask = compiledPipeline._stageFlags;
            stateTracker._pushConstantsValid = false;
        }

        bindDynamicState( pipeline.descriptor()._stateBlock, pipeline.descriptor()._blendStates, cmdBuffer );

        return compiledPipeline._program->validatePreBind(_context.context().config(), false);
    }

    void VK_API::SubmitTransferRequest( const VKTransferQueue::TransferRequest& request, VkCommandBuffer cmd )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmd );

        VkBufferMemoryBarrier2 barriers[2] = {};
        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.bufferMemoryBarrierCount = 1u;
        if ( request.srcBuffer != VK_NULL_HANDLE )
        {
            PrepareTransferRequest( request, true, barriers[0] );

            VkBufferCopy2 copy{ .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2 };
            copy.dstOffset = request.dstOffset;
            copy.srcOffset = request.srcOffset;
            copy.size = request.size;

            VkCopyBufferInfo2 copyInfo = { .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2 };
            copyInfo.dstBuffer = request.dstBuffer;
            copyInfo.srcBuffer = request.srcBuffer;
            copyInfo.regionCount = 1u;
            copyInfo.pRegions = &copy;

            vkCmdCopyBuffer2( cmd, &copyInfo );
            dependencyInfo.bufferMemoryBarrierCount = 2u;
        }

        PrepareTransferRequest( request, false, barriers[1] );
        dependencyInfo.pBufferMemoryBarriers = barriers;
        VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );
    }

    /*static*/ void VK_API::PushPendingSubmitSemaphore(VkSemaphore semaphore)
    {
        LockGuard<Mutex> lock(s_submitSemaphores._lock);
        s_submitSemaphores._pendingSubmitSemaphores.push_back(semaphore);
    }
    void VK_API::FlushBufferTransferRequests( VkCommandBuffer cmdBuffer  )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        // Atomic load is way cheaper than an atomic exchange, so try this first as it will early-out most often
        if (!s_transferQueue._dirty.load(std::memory_order_acquire))
        {
            return;
        }

        if (!s_transferQueue._dirty.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        VK_NON_UT_ASSERT( cmdBuffer != VK_NULL_HANDLE );

        TransferQueueRequestsContainer requests{};
        // Keep processor thread-local to preserve internal vector capacities and avoid reallocations.
        thread_local VkBufferTransferProcessor s_processor{};
        // ConsumerToken must be per-consumer-thread
        thread_local moodycamel::ConsumerToken s_transferConsumerToken( s_transferQueue._requests );

        s_processor.reset();

        while (true)
        {
            const size_t dequeued = s_transferQueue._requests.try_dequeue_bulk(s_transferConsumerToken, requests.data(), VkBufferTransferProcessor::MAX_BUFFER_COPIES_PER_FLUSH);

            if ( 0u == dequeued )
            {
                break;
            }

            for ( size_t i = 0u; i < dequeued; ++i )
            {
                const VKTransferQueue::TransferRequest& request = requests[i];

                if ( VK_NULL_HANDLE == request.srcBuffer )
                {
                    // no copy required — just a post-copy (or standalone) barrier
                    VkBufferMemoryBarrier2 b = vk::bufferMemoryBarrier2();
                    PrepareTransferRequest(request, false, b);
                    s_processor.addBarrier(b, cmdBuffer);
                    continue;
                }

                STUBBED("ToDo: Move this logic inside the VkBufferTransferProcessor struct itself. - Ionut");
                s_processor._transferQueueBatched[s_processor._transferBatchedCount++] = request;
                if ( VkBufferTransferProcessor::MAX_BUFFER_COPIES_PER_FLUSH == s_processor._transferBatchedCount )
                {
                    s_processor.processCurrentBatch( cmdBuffer );
                    // flush any barriers produced by the batch (processCurrentBatch leaves barriers in s_barriers)
                    s_processor.flushBarriers( cmdBuffer);
                }
            }
        }

        // process any remaining batched transfers
        s_processor.processCurrentBatch( cmdBuffer );

        // final barrier flush (if any)
        s_processor.flushBarriers( cmdBuffer );
    }

    void VK_API::flushCommand( GFX::CommandBase* cmd ) noexcept
    {
        VkCommandBuffer cmdBuffer = GetCurrentCommandBuffer();
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT(cmdBuffer);

        auto& stateTracker = GetStateTracker();

        if ( GFXDevice::IsSubmitCommand( cmd->type() ) )
        {
            FlushBufferTransferRequests( cmdBuffer );
        }

        switch ( cmd->type() )
        {
            case GFX::CommandType::BEGIN_RENDER_PASS:
            {
                PROFILE_SCOPE( "BEGIN_RENDER_PASS", Profiler::Category::Graphics );
                const GFX::BeginRenderPassCommand* crtCmd = cmd->As<GFX::BeginRenderPassCommand>();
                PushDebugMessage( _context.context().config(), cmdBuffer, crtCmd->_name.c_str() );

                stateTracker._activeRenderTargetID = crtCmd->_target;

                // We can do this outside of a renderpass
                FlushBufferTransferRequests( cmdBuffer );

                stateTracker._pipelineRenderInfo = {};
                stateTracker._pipelineRenderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                VkRenderingInfo renderingInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
                VkRenderingAttachmentInfo swapchainAttachment{};
                if ( crtCmd->_target == SCREEN_TARGET_ID )
                {
                    VKSwapChain* swapChain = stateTracker._activeWindow->_swapChain.get();
                    const RTClearEntry& colourClearEntry = crtCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )];
                    const bool shouldClear = colourClearEntry._enabled;
                    const bool imageWasPresented = swapChain->currentImageWasPresented();
                    const bool imageWasRenderedThisFrame = swapChain->currentImageWasRenderedThisFrame();
                    const bool canLoadPreviousContents = !shouldClear &&
                                                         (imageWasPresented || imageWasRenderedThisFrame);

                    swapchainAttachment =
                    {
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = VK_NULL_HANDLE,
                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

                        .loadOp = shouldClear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                              : (canLoadPreviousContents ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE),
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue =
                        {
                            .color =
                            {
                                colourClearEntry._colour.r,
                                colourClearEntry._colour.g,
                                colourClearEntry._colour.b,
                                colourClearEntry._colour.a
                            }
                        }
                    };

                    PROFILE_SCOPE( "Draw to screen", Profiler::Category::Graphics);

                    swapchainAttachment.imageView = swapChain->getCurrentImageView();
                    stateTracker._pipelineRenderInfo.colorAttachmentCount = 1u;
                    stateTracker._pipelineRenderInfo.pColorAttachmentFormats = &swapChain->getSwapChain().image_format;

                    renderingInfo.renderArea = 
                    {
                        .offset = {0, 0},
                        .extent = swapChain->surfaceExtent()
                    };
                    renderingInfo.layerCount = 1u;
                    renderingInfo.colorAttachmentCount = 1u;
                    renderingInfo.pColorAttachments = &swapchainAttachment;

                    VkImageMemoryBarrier2 imageBarrier = vk::imageMemoryBarrier2();
                    imageBarrier.image = swapChain->getCurrentImage();
                    imageBarrier.subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    };

                    imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                                 (canLoadPreviousContents ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT : VK_ACCESS_2_NONE);
                    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                    imageBarrier.srcAccessMask = imageWasRenderedThisFrame ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE;
                    imageBarrier.srcStageMask = imageWasRenderedThisFrame
                                              ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                              : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    imageBarrier.oldLayout = VKSwapChain::ScreenPassInitialLayout( swapChain->deferPresentTransition(),
                                                                                   imageWasRenderedThisFrame,
                                                                                   canLoadPreviousContents );

                    VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                    dependencyInfo.imageMemoryBarrierCount = 1u;
                    dependencyInfo.pImageMemoryBarriers = &imageBarrier;
                    
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo);

                    swapChain->markCurrentImageRenderedThisFrame();
                    stateTracker._activeMSAASamples = 1u;
                }
                else
                {
                    vkRenderTarget* rt = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget( crtCmd->_target ));
                    Attorney::VKAPIRenderTarget::begin( *rt, cmdBuffer, crtCmd->_descriptor, crtCmd->_clearDescriptor, stateTracker._pipelineRenderInfo );
                    renderingInfo = rt->renderingInfo();

                    stateTracker._activeMSAASamples = rt->getSampleCount();
                }

                {
                    PROFILE_SCOPE( "Begin Rendering", Profiler::Category::Graphics );

                    stateTracker._renderingSignature = VKRenderingSignature::From( stateTracker._pipelineRenderInfo,
                                                                                 stateTracker._activeMSAASamples );
                
                    const Rect<I32> renderArea
                    { 
                         renderingInfo.renderArea.offset.x,
                         renderingInfo.renderArea.offset.y,
                         to_I32(renderingInfo.renderArea.extent.width),
                         to_I32(renderingInfo.renderArea.extent.height)
                    };

                    stateTracker._activeRenderTargetDimensions = { renderArea.sizeX, renderArea.sizeY};
                    stateTracker._activeRenderTargetColourAttachmentCount = renderingInfo.colorAttachmentCount;

                    _context.setViewport( renderArea );
                    _context.setScissor( renderArea );

                    // A pipeline bound before this pass may have set its front face for the other target type
                    auto& activeState = stateTracker._activeWindow->_activeState;
                    const bool invertFrontFace = !IsSwapChainTargetActive();
                    if ( activeState._isSet && activeState._frontFaceInverted != invertFrontFace )
                    {
                        vkCmdSetFrontFace( cmdBuffer, (activeState._block._frontFaceCCW != invertFrontFace) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE );
                        activeState._frontFaceInverted = invertFrontFace;
                    }

                    VK_PROFILE( vkCmdBeginRendering, cmdBuffer, &renderingInfo);
                }
            } break;
            case GFX::CommandType::END_RENDER_PASS:
            {
                PROFILE_SCOPE( "END_RENDER_PASS", Profiler::Category::Graphics );

                VK_PROFILE( vkCmdEndRendering, cmdBuffer );
                if ( stateTracker._activeRenderTargetID == SCREEN_TARGET_ID &&
                     !stateTracker._activeWindow->_swapChain->deferPresentTransition() )
                {
                    VkImageMemoryBarrier2 imageBarrier = vk::imageMemoryBarrier2();
                    imageBarrier.image = stateTracker._activeWindow->_swapChain->getCurrentImage();
                    imageBarrier.subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    };

                    imageBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                    imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                    VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                    dependencyInfo.imageMemoryBarrierCount = 1u;
                    dependencyInfo.pImageMemoryBarriers = &imageBarrier;
                    
                    VK_PROFILE( vkCmdPipelineBarrier2,cmdBuffer, &dependencyInfo );
                }
                else if ( stateTracker._activeRenderTargetID != SCREEN_TARGET_ID )
                {
                    vkRenderTarget* rt = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget( stateTracker._activeRenderTargetID ));
                    const GFX::EndRenderPassCommand* crtCmd = cmd->As<GFX::EndRenderPassCommand>();
                    Attorney::VKAPIRenderTarget::end( *rt, cmdBuffer, crtCmd->_transitionMask );
                    stateTracker._activeRenderTargetID = SCREEN_TARGET_ID;
                }

                PopDebugMessage( _context.context().config(), cmdBuffer );
                stateTracker._renderingSignature = {};
                stateTracker._activeMSAASamples = _context.context().config().rendering.MSAASamples;
                stateTracker._activeRenderTargetDimensions = s_stateTracker._activeWindow->_window->getDrawableSize();
                stateTracker._activeRenderTargetColourAttachmentCount = 1u;
                // We can do this outside of a renderpass
                FlushBufferTransferRequests( cmdBuffer );
            }break;
            case GFX::CommandType::BLIT_RT:
            {
                PROFILE_SCOPE( "BLIT_RT", Profiler::Category::Graphics );

                const GFX::BlitRenderTargetCommand* crtCmd = cmd->As<GFX::BlitRenderTargetCommand>();
                vkRenderTarget* source = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget( crtCmd->_source ));
                vkRenderTarget* destination = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget( crtCmd->_destination ));
                Attorney::VKAPIRenderTarget::blitFrom( *destination, cmdBuffer, source, crtCmd->_params );

            } break;
            case GFX::CommandType::BEGIN_GPU_QUERY:
            {
                PROFILE_SCOPE( "BEGIN_GPU_QUERY", Profiler::Category::Graphics );
            }break;
            case GFX::CommandType::END_GPU_QUERY:
            {
                PROFILE_SCOPE( "END_GPU_QUERY", Profiler::Category::Graphics );
            }break;
            case GFX::CommandType::COPY_TEXTURE:
            {
                PROFILE_SCOPE( "COPY_TEXTURE", Profiler::Category::Graphics );

                const GFX::CopyTextureCommand* crtCmd = cmd->As<GFX::CopyTextureCommand>();
                vkTexture::Copy( cmdBuffer,
                                 static_cast<vkTexture*>(Get(crtCmd->_source)),
                                 crtCmd->_sourceMSAASamples,
                                 static_cast<vkTexture*>(Get(crtCmd->_destination)),
                                 crtCmd->_destinationMSAASamples,
                                 crtCmd->_params );
            }break;
            case GFX::CommandType::CLEAR_TEXTURE:
            {
                PROFILE_SCOPE( "CLEAR_TEXTURE", Profiler::Category::Graphics );

                const GFX::ClearTextureCommand* crtCmd = cmd->As<GFX::ClearTextureCommand>();
                static_cast<vkTexture*>(Get(crtCmd->_texture))->clearData( cmdBuffer, crtCmd->_clearColour, crtCmd->_layerRange, crtCmd->_mipLevel );
            }break;
            case GFX::CommandType::READ_TEXTURE:
            {
                PROFILE_SCOPE( "READ_TEXTURE", Profiler::Category::Graphics );

                const GFX::ReadTextureCommand* crtCmd = cmd->As<GFX::ReadTextureCommand>();
                const ImageReadbackData data = static_cast<vkTexture*>(Get(crtCmd->_texture))->readData( cmdBuffer, crtCmd->_mipLevel, crtCmd->_pixelPackAlignment);
                crtCmd->_callback( data );
            }break;
            case GFX::CommandType::BIND_PIPELINE:
            {
                PROFILE_SCOPE( "BIND_PIPELINE", Profiler::Category::Graphics );

                const Pipeline* pipeline = cmd->As<GFX::BindPipelineCommand>()->_pipeline;
                assert( pipeline != nullptr );
                if ( bindPipeline( *pipeline, cmdBuffer ) == ShaderResult::Failed )
                {
                    const auto handle = pipeline->descriptor()._shaderProgramHandle;
                    Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_BIND" ), handle._index, handle._generation );
                    stateTracker._pipeline = {};
                    stateTracker._logicalPipeline = nullptr;
                }
            } break;
            case GFX::CommandType::SEND_PUSH_CONSTANTS:
            {
                PROFILE_SCOPE( "SEND_PUSH_CONSTANTS", Profiler::Category::Graphics );

                if ( stateTracker._pipeline._vkPipeline != VK_NULL_HANDLE )
                {
                    GFX::SendPushConstantsCommand* pushConstantsCmd = cmd->As<GFX::SendPushConstantsCommand>();
                    UniformData* uniforms = pushConstantsCmd->_uniformData;
                    if ( uniforms != nullptr )
                    {
                        stateTracker._pipeline._program->uploadUniformData( *uniforms );
                    }
                    if ( pushConstantsCmd->_fastData.dataFlags() != PushConstantsStruct::DataFlags::NONE )
                    {
                        _lastPushConstants.merge( pushConstantsCmd->_fastData );
                        _hasLastPushConstants = true;
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        VK_SHADER_STAGE_ALL,
                                                        0,
                                                        to_U32( PushConstantsStruct::MaxSize() ),
                                                        _lastPushConstants.dataPtr() );

                        stateTracker._pushConstantsValid = true;
                    }
                }
            } break;
            case GFX::CommandType::BEGIN_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "BEGIN_DEBUG_SCOPE", Profiler::Category::Graphics );

                const GFX::BeginDebugScopeCommand* crtCmd = cmd->As<GFX::BeginDebugScopeCommand>();
                PushDebugMessage( _context.context().config(), cmdBuffer, crtCmd->_scopeName.c_str(), crtCmd->_scopeId );
            } break;
            case GFX::CommandType::END_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "END_DEBUG_SCOPE", Profiler::Category::Graphics );

                PopDebugMessage( _context.context().config(), cmdBuffer );
            } break;
            case GFX::CommandType::ADD_DEBUG_MESSAGE:
            {
                PROFILE_SCOPE( "ADD_DEBUG_MESSAGE", Profiler::Category::Graphics );

                const GFX::AddDebugMessageCommand* crtCmd = cmd->As<GFX::AddDebugMessageCommand>();
                AddDebugMessage( _context.context().config(), cmdBuffer, crtCmd->_msg.c_str(), crtCmd->_msgId );
            }break;
            case GFX::CommandType::COMPUTE_MIPMAPS:
            {
                PROFILE_SCOPE( "COMPUTE_MIPMAPS", Profiler::Category::Graphics );

                const GFX::ComputeMipMapsCommand* crtCmd = cmd->As<GFX::ComputeMipMapsCommand>();

                static_cast<vkTexture*>(Get( crtCmd->_texture ))->generateMipmaps( cmdBuffer, 0u, crtCmd->_layerRange._offset, crtCmd->_layerRange._count, crtCmd->_usage );
            }break;
            case GFX::CommandType::DRAW_COMMANDS:
            {
                PROFILE_SCOPE( "DRAW_COMMANDS", Profiler::Category::Graphics );

                const auto& drawCommands = cmd->As<GFX::DrawCommand>()->_drawCommands;

                if ( !_context.uniformUploadFailed() && stateTracker._pipeline._vkPipeline != VK_NULL_HANDLE )
                {
                    if ( !stateTracker._pushConstantsValid && _hasLastPushConstants )
                    {
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        VK_SHADER_STAGE_ALL,
                                                        0,
                                                        to_U32( PushConstantsStruct::MaxSize() ),
                                                        _lastPushConstants.dataPtr() );
                        stateTracker._pushConstantsValid = true;
                    }


                    U32 drawCount = 0u;
                    for ( const GenericDrawCommand& currentDrawCommand : drawCommands )
                    {
                        DIVIDE_GPU_ASSERT( currentDrawCommand._drawCount < _context.GetDeviceInformation()._maxDrawIndirectCount );

                        if ( isEnabledOption( currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY ) )
                        {
                            Draw( currentDrawCommand, cmdBuffer );
                            ++drawCount;
                        }

                        if ( isEnabledOption( currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME ) )
                        {
                            PrimitiveTopology oldTopology = stateTracker._pipeline._topology;
                            stateTracker._pipeline._topology = PrimitiveTopology::LINES;
                            VK_PROFILE( vkCmdBindPipeline, cmdBuffer, stateTracker._pipeline._bindPoint, stateTracker._pipeline._vkPipelineWireframe );
                            Draw( currentDrawCommand, cmdBuffer );
                            ++drawCount;
                            VK_PROFILE( vkCmdBindPipeline, cmdBuffer, stateTracker._pipeline._bindPoint, stateTracker._pipeline._vkPipeline );
                            stateTracker._pipeline._topology = oldTopology;
                        }
                    }

                    _context.registerDrawCalls( drawCount );
                }
            }break;
            case GFX::CommandType::DISPATCH_SHADER_TASK:
            {
                PROFILE_SCOPE( "DISPATCH_SHADER_TASK", Profiler::Category::Graphics );
                if ( !_context.uniformUploadFailed() && stateTracker._pipeline._vkPipeline != VK_NULL_HANDLE )
                {
                    if ( !stateTracker._pushConstantsValid && _hasLastPushConstants )
                    {
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        VK_SHADER_STAGE_ALL,
                                                        0,
                                                        to_U32( PushConstantsStruct::MaxSize() ),
                                                        _lastPushConstants.dataPtr() );
                        stateTracker._pushConstantsValid = true;
                    }

                    const GFX::DispatchShaderTaskCommand* crtCmd = cmd->As<GFX::DispatchShaderTaskCommand>();

                    switch ( stateTracker._pipeline._topology )
                    {
                        case PrimitiveTopology::COMPUTE:
                        {
                            VK_PROFILE( vkCmdDispatch, cmdBuffer, crtCmd->_workGroupSize.x, crtCmd->_workGroupSize.y, crtCmd->_workGroupSize.z );
                        } break;
                        case PrimitiveTopology::MESHLET:
                        {
                            vkCmdDrawMeshTasksEXT( cmdBuffer, crtCmd->_workGroupSize.x, crtCmd->_workGroupSize.y, crtCmd->_workGroupSize.z );
                            _context.registerDrawCalls( 1u );
                        } break;
                        default:
                            DIVIDE_UNEXPECTED_CALL();
                    }
                }
            } break;
            case GFX::CommandType::MEMORY_BARRIER:
            {
                PROFILE_SCOPE( "MEMORY_BARRIER", Profiler::Category::Graphics );

                constexpr U8 MAX_BUFFER_BARRIERS_PER_CMD{64};

                std::array<VkImageMemoryBarrier2, RT_MAX_ATTACHMENT_COUNT> imageBarriers{};
                U8 imageBarrierCount = 0u;

                std::array<VkBufferMemoryBarrier2, MAX_BUFFER_BARRIERS_PER_CMD> bufferBarriers{};
                U8 bufferBarrierCount = 0u;

                const GFX::MemoryBarrierCommand* crtCmd = cmd->As<GFX::MemoryBarrierCommand>();

                SyncObjectHandle handle{};
                for ( const BufferLock& lock : crtCmd->_bufferLocks )
                {
                    if ( lock._buffer == nullptr || lock._range._length == 0u )
                    {
                        continue;
                    }

                    vkBufferImpl* vkBuffer = static_cast<vkBufferImpl*>(lock._buffer);

                    VkBufferMemoryBarrier2& memoryBarrier = bufferBarriers[bufferBarrierCount++];
                    memoryBarrier = vk::bufferMemoryBarrier2();
                    memoryBarrier.offset = lock._range._startOffset;
                    memoryBarrier.size = lock._range._length == U32_MAX ? VK_WHOLE_SIZE : lock._range._length;
                    memoryBarrier.buffer = vkBuffer->_buffer;

                    const bool isCommandBuffer = vkBuffer->_params._usageType == BufferUsageType::COMMAND_BUFFER;

                    switch (lock._type )
                    {
                        case BufferSyncUsage::CPU_WRITE_TO_GPU_READ:
                        {
                            if ( handle._id == SyncObjectHandle::INVALID_SYNC_ID )
                            {
                                handle = LockManager::CreateSyncObject( RenderAPI::Vulkan );
                            }

                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | AllShaderStages();
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
                            if ( isCommandBuffer )
                            {
                                memoryBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                                memoryBarrier.dstAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            }

                            DIVIDE_EXPECTED_CALL( lock._buffer->lockRange( lock._range, handle ) );

                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_CPU_READ:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_GPU_READ:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                            memoryBarrier.dstStageMask = AllShaderStages();
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                            if ( isCommandBuffer )
                            {
                                memoryBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                                memoryBarrier.dstAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            }
                        } break;
                        case BufferSyncUsage::GPU_READ_TO_GPU_WRITE:
                        {
                            memoryBarrier.srcStageMask = AllShaderStages();
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                            if ( isCommandBuffer )
                            {
                                memoryBarrier.srcStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                                memoryBarrier.srcAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            }
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
                            if ( isCommandBuffer )
                            {
                                memoryBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
                                memoryBarrier.dstAccessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
                            }
                        } break;
                        case BufferSyncUsage::CPU_READ_TO_CPU_WRITE:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT; 
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT; 
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                        }break;
                        case BufferSyncUsage::CPU_WRITE_TO_CPU_WRITE:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                        }break;
                        default : DIVIDE_UNEXPECTED_CALL();
                    }

                    if ( bufferBarrierCount == MAX_BUFFER_BARRIERS_PER_CMD )
                    {
                        // Too many buffer barriers. Flushing ...
                        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                        dependencyInfo.bufferMemoryBarrierCount = bufferBarrierCount;
                        dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();

                        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                        bufferBarrierCount = 0u;
                    }
                }

                for ( const auto& it : crtCmd->_textureLayoutChanges )
                {
                    if ( it._sourceLayout == it._targetLayout )
                    {
                        continue;
                    }
                    DIVIDE_GPU_ASSERT( it._targetLayout != ImageUsage::UNDEFINED);

                    const vkTexture* vkTex = static_cast<const vkTexture*>(it._targetView._srcTexture);

                    vkTexture::TransitionType transitionType = vkTexture::TransitionType::COUNT;

                    switch ( it._targetLayout )
                    {
                        case ImageUsage::UNDEFINED:
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                        case ImageUsage::SHADER_READ:
                        {
                            switch ( it._sourceLayout )
                            {
                                case ImageUsage::UNDEFINED:
                                {
                                    transitionType = vkTexture::TransitionType::UNDEFINED_TO_SHADER_READ;
                                } break;
                                case ImageUsage::SHADER_READ:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                                case ImageUsage::SHADER_WRITE:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_WRITE_TO_SHADER_READ;
                                }break;
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_READ_WRITE_TO_SHADER_READ;
                                } break;
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                            }
                        } break;
                        case ImageUsage::SHADER_WRITE:
                        {
                            switch ( it._sourceLayout )
                            {
                                case ImageUsage::UNDEFINED:
                                {
                                    transitionType = vkTexture::TransitionType::UNDEFINED_TO_SHADER_WRITE;
                                } break;
                                case ImageUsage::SHADER_READ:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_READ_TO_SHADER_WRITE;
                                } break;
                                case ImageUsage::SHADER_WRITE:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    NOP(); // Both in general layout
                                } break;
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_WRITE;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                            }
                        } break;
                        case ImageUsage::SHADER_READ_WRITE:
                        {
                            switch ( it._sourceLayout )
                            {
                                case ImageUsage::UNDEFINED:
                                {
                                    transitionType = vkTexture::TransitionType::UNDEFINED_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::SHADER_READ:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_READ_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::SHADER_WRITE:
                                {
                                    NOP(); // Both in general layout
                                }break;
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ_WRITE;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }
                            }
                        } break;
                        case ImageUsage::RT_COLOUR_ATTACHMENT:
                        {
                            switch (it._sourceLayout)
                            {
                                case ImageUsage::SHADER_READ:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT;
                                } break;
                                default: DIVIDE_UNEXPECTED_CALL();
                            }
                        } break;
                        case ImageUsage::RT_DEPTH_ATTACHMENT:
                        case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                        {
                            switch (it._sourceLayout)
                            {
                                case ImageUsage::SHADER_READ:
                                {
                                    transitionType = vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT;
                                } break;
                                default: DIVIDE_UNEXPECTED_CALL();
                            }
                        } break;
                        default: DIVIDE_UNEXPECTED_CALL();
                    };

                    if ( transitionType != vkTexture::TransitionType::COUNT )
                    {
                        const VkImageSubresourceRange subResourceRange = vkTexture::GetViewSubresourceRange(
                            TargetType( it._targetView ), it._targetView._subRange, vkTexture::GetAspectFlags( vkTex->descriptor() ) );

                        vkTexture::TransitionTexture(
                            transitionType,
                            subResourceRange,
                            {
                                ._image = vkTex->image()->_image,
                                ._name = vkTex->resourceName().c_str(),
                                ._isResolveImage = HasUsageFlagSet(vkTex->descriptor(), ImageUsage::RT_RESOLVE_TARGET)
                            },
                            imageBarriers[imageBarrierCount++] );
                    }
                }

                if ( imageBarrierCount > 0u || bufferBarrierCount > 0u)
                {
                    VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                    dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
                    dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
                    dependencyInfo.bufferMemoryBarrierCount = bufferBarrierCount;
                    dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();

                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                }
            } break;

            case GFX::CommandType::SET_VIEWPORT:
            case GFX::CommandType::PUSH_VIEWPORT:
            case GFX::CommandType::POP_VIEWPORT:
            case GFX::CommandType::SET_SCISSOR:
            case GFX::CommandType::SET_CAMERA:
            case GFX::CommandType::PUSH_CAMERA:
            case GFX::CommandType::POP_CAMERA:
            case GFX::CommandType::SET_CLIP_PLANES:
            case GFX::CommandType::BIND_SHADER_RESOURCES:
            case GFX::CommandType::READ_BUFFER_DATA:
            case GFX::CommandType::CLEAR_BUFFER_DATA: break;

            case GFX::CommandType::COUNT:
            default: DIVIDE_UNEXPECTED_CALL();
        }
    }

    void VK_API::preFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VkCommandBuffer cmdBuffer = GetCurrentCommandBuffer();

        GetStateTracker()._activeRenderTargetID = SCREEN_TARGET_ID;
        GetStateTracker()._activeRenderTargetColourAttachmentCount = 1u;
        GetStateTracker()._activeRenderTargetDimensions = s_stateTracker._activeWindow->_window->getDrawableSize();
        // We don't really know what happened before this state and at worst this is going to end up into an 
        // extra vkCmdPushConstants call with default data, so better safe.
        GetStateTracker()._pushConstantsValid = false;
        ResetBoundDescriptorState();
        FlushBufferTransferRequests( cmdBuffer );

        if ( !s_imageBarrierQueue._imageBarriers.empty() )
        {
            UniqueLock<Mutex> w_lock(s_imageBarrierQueue._lock);
            if (!s_imageBarrierQueue._imageBarriers.empty())
            {
                VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                dependencyInfo.imageMemoryBarrierCount = to_U32(s_imageBarrierQueue._imageBarriers.size());
                dependencyInfo.pImageMemoryBarriers = s_imageBarrierQueue._imageBarriers.data();

                vkTexture::FlushPipelineBarriers( cmdBuffer, dependencyInfo );

                s_imageBarrierQueue._imageBarriers.reset_lose_memory();
            }
        }
    }

    void VK_API::postFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        s_transientDeleteQueue.flush( _device->getDevice() );
        GetStateTracker()._activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        GetStateTracker()._activeRenderTargetColourAttachmentCount = 1u;
        GetStateTracker()._activeRenderTargetDimensions = s_stateTracker._activeWindow->_window->getDrawableSize();
    }

    void VK_API::writePassTimestamp( const U32 index )
    {
        VKPerWindowState* activeWindow = GetStateTracker()._activeWindow;
        if ( activeWindow != nullptr && activeWindow->_swapChain != nullptr )
        {
            activeWindow->_swapChain->writePassTimestamp( index );
        }
    }

    U32 VK_API::readPassTimestamps( const std::span<F32> outMS, U64& frameNumberOut )
    {
        VKPerWindowState* activeWindow = GetStateTracker()._activeWindow;
        if ( activeWindow != nullptr && activeWindow->_swapChain != nullptr )
        {
            return activeWindow->_swapChain->readPassTimestamps( outMS, frameNumberOut );
        }
        return 0u;
    }

    bool VK_API::setViewportInternal( const Rect<I32>& newViewport ) noexcept
    {
        return setViewportInternal( newViewport, GetCurrentCommandBuffer() );
    }

    bool VK_API::setViewportInternal( const Rect<I32>& newViewport, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        // Engine convention (shared with OpenGL): clip-space Y points up and viewport/scissor origins are bottom-left.
        // Offscreen targets: an unflipped viewport maps clip-space -Y to texel row 0, which is exactly OpenGL's memory layout
        //                    (sampling, texelFetch(gl_FragCoord), copies and readbacks all match). Winding is compensated in bindDynamicState.
        // Swapchain:         a negative-height viewport keeps the presented image upright. Nothing samples the swapchain, so only the
        //                    on-screen orientation matters there.
        VkViewport targetViewport{};
        targetViewport.x = to_F32( newViewport.offsetX );
        targetViewport.width = to_F32( newViewport.sizeX );

        if ( IsSwapChainTargetActive() )
        {
            const I32 targetHeight = to_I32( GetStateTracker()._activeRenderTargetDimensions.height );
            targetViewport.y = to_F32( targetHeight - newViewport.offsetY );
            targetViewport.height = -to_F32( newViewport.sizeY );
        }
        else
        {
            targetViewport.y = to_F32( newViewport.offsetY );
            targetViewport.height = to_F32( newViewport.sizeY );
        }
        targetViewport.minDepth = 0.f;
        targetViewport.maxDepth = 1.f;

        vkCmdSetViewport( cmdBuffer, 0, 1, &targetViewport );
        return true;
    }

    bool VK_API::setScissorInternal( const Rect<I32>& newScissor ) noexcept
    {
        return setScissorInternal( newScissor, GetCurrentCommandBuffer() );
    }

    bool VK_API::setScissorInternal( const Rect<I32>& newScissor, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        const bool scissorEnabled = GetStateTracker()._activeWindow->_activeState._isSet &&
                                    GetStateTracker()._activeWindow->_activeState._block._scissorTestEnabled;
        const vec2<U16> rtDimensions = GetStateTracker()._activeRenderTargetDimensions;
        const VkRect2D fullScissor{ VkOffset2D{ 0, 0 }, VkExtent2D{ rtDimensions.width, rtDimensions.height } };

        if ( !scissorEnabled )
        {
            vkCmdSetScissor( cmdBuffer, 0, 1, &fullScissor );
            return true;
        }

        const I32 maxWidth = to_I32( rtDimensions.width );
        const I32 maxHeight = to_I32( rtDimensions.height );
        const I32 requestX0 = newScissor.offsetX;
        const I32 requestY0 = newScissor.offsetY;
        const I32 requestX1 = newScissor.offsetX + std::max( 0, newScissor.sizeX );
        const I32 requestY1 = newScissor.offsetY + std::max( 0, newScissor.sizeY );

        if ( requestX0 <= 0 && requestY0 <= 0 && requestX1 >= maxWidth && requestY1 >= maxHeight )
        {
            vkCmdSetScissor( cmdBuffer, 0, 1, &fullScissor );
            return true;
        }

        const I32 x0 = std::max( 0, std::min( requestX0, maxWidth ) );
        const I32 y0 = std::max( 0, std::min( requestY0, maxHeight ) );
        const I32 x1 = std::max( 0, std::min( requestX1, maxWidth ) );
        const I32 y1 = std::max( 0, std::min( requestY1, maxHeight ) );
        // Bottom-left origin rects only need converting where the viewport is flipped (swapchain). Offscreen targets share OpenGL's row order.
        const VkRect2D targetScissor{
            VkOffset2D{ x0, IsSwapChainTargetActive() ? maxHeight - y1 : y0 },
            VkExtent2D{ to_U32( std::max( 0, x1 - x0 ) ), to_U32( std::max( 0, y1 - y0 ) ) }
        };

        vkCmdSetScissor( cmdBuffer, 0, 1, &targetScissor );
        return true;
    }

    VkDescriptorSetLayout VK_API::createLayoutFromBindings(const DescriptorSetUsage usage, const ShaderProgram::BindingsPerSetArray& bindings, DynamicBindings& dynamicBindings )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        thread_local fixed_vector<VkDescriptorSetLayoutBinding, MAX_BINDINGS_PER_DESCRIPTOR_SET> layoutBinding{};

        dynamicBindings.reset_lose_memory();
        layoutBinding.reset_lose_memory();

        for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
        {
            if ( bindings[slot]._type == DescriptorSetBindingType::COUNT || bindings[slot]._type == DescriptorSetBindingType::INDIRECT_BUFFER )
            {
                continue;
            }

            VkDescriptorSetLayoutBinding& newBinding = layoutBinding.emplace_back();
            newBinding.descriptorCount = 1u;
            newBinding.descriptorType = VKUtil::vkDescriptorType( bindings[slot]._type );
            newBinding.stageFlags = GetFlagsForStageVisibility( bindings[slot]._visibility );
            newBinding.binding = slot;
            newBinding.pImmutableSamplers = nullptr;

            if ( bindings[slot]._type == DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC ||
                 bindings[slot]._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC )
            {
                dynamicBindings.emplace_back(DynamicBinding{
                    ._offset = 0u,
                    ._slot = slot
                });
            }
        }

        eastl::sort(begin(dynamicBindings), end(dynamicBindings), []( const DynamicBinding& bindingA, const DynamicBinding& bindingB ) { return bindingA._slot <= bindingB._slot; });

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo = vk::descriptorSetLayoutCreateInfo( layoutBinding.data(), to_U32( layoutBinding.size() ) );
        if ( DescriptorSetUsage::PER_DRAW == usage )
        {
            DIVIDE_GPU_ASSERT( layoutBinding.size() <= s_maxPushDescriptors, "PER_DRAW layout exceeds the device's maxPushDescriptors limit!" );
            layoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
        }
        return _descriptorLayoutCache->createDescriptorLayout( &layoutCreateInfo );
    }
    
    void VK_API::initDescriptorSets()
    {
        const ShaderProgram::BindingSetData& bindingData = ShaderProgram::GetBindingSetData();

        _descriptorLayoutCache = std::make_unique<DescriptorLayoutCache>( _device->getVKDevice() );

        // PER_DRAW layouts are per-program (see vkShaderProgram::descriptorSetLayout()), so the shared array only holds the shared sets
        _descriptorSetLayouts.fill( VK_NULL_HANDLE );
        for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
        {
            if ( i != to_base( DescriptorSetUsage::PER_DRAW ) )
            {
                _descriptorSetLayouts[i] = createLayoutFromBindings( static_cast<DescriptorSetUsage>(i), bindingData[i], _descriptorDynamicBindings[i] );
            }
        }

        for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
        {
            auto& pool = s_stateTracker._descriptorAllocators[i];
            pool._frameCount = Config::MAX_FRAMES_IN_FLIGHT + 1u;
            pool._allocatorPool.reset( vke::DescriptorAllocatorPool::Create( _device->getVKDevice(), pool._frameCount) );
       
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0.f );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 0.f );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0.f );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.f );

            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::UNIFORM_BUFFER_STATIC ) );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ShaderProgram::GetBindingCount(static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER) * 1.f);
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::IMAGE ) * 1.f );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC ) * 1.f );
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, ShaderProgram::GetBindingCount(static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC) * 1.f);
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, ShaderProgram::GetBindingCount(static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC) * 1.f);

            pool._handle = pool._allocatorPool->GetAllocator();
        }
    }

    void VK_API::onThreadCreated( [[maybe_unused]] const size_t threadIndex, [[maybe_unused]] const std::thread::id& threadID, [[maybe_unused]] const bool isMainRenderThread ) noexcept
    {
    }

    void VK_API::OnShaderReloaded( const I64 programGUID )
    {
        LockGuard<Mutex> lock( s_reloadedShadersLock );
        s_reloadedShaders.push_back( programGUID );
    }

    VKStateTracker& VK_API::GetStateTracker() noexcept
    {
        return s_stateTracker;
    }

    void VK_API::AddDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer, const char* message, const U32 id )
    {
        if ( s_hasDebugMarkerSupport && config.debug.renderer.enableRenderAPIDebugGrouping  )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

            constexpr F32 color[4] = { 0.0f, 1.0f, 0.0f, 1.f };

            VkDebugUtilsLabelEXT labelInfo{};
            labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            labelInfo.pLabelName = message;
            memcpy( labelInfo.color, &color[0], sizeof( F32 ) * 4 );

            Debug::vkCmdInsertDebugUtilsLabelEXT( cmdBuffer, &labelInfo );
        }

        GFXDevice::AddDebugMessage(message, id);
    }

    void VK_API::PushDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer, const char* message, const U32 id )
    {
        if ( s_hasDebugMarkerSupport && config.debug.renderer.enableRenderAPIDebugGrouping  )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

            constexpr F32 color[4] = { 0.5f, 0.5f, 0.5f, 1.f };

            VkDebugUtilsLabelEXT labelInfo{};
            labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            labelInfo.pLabelName = message;
            memcpy( labelInfo.color, &color[0], sizeof( F32 ) * 4 );
            Debug::vkCmdBeginDebugUtilsLabelEXT( cmdBuffer, &labelInfo );
        }

        GFXDevice::PushDebugMessage(message, id);
    }

    void VK_API::PopDebugMessage( const Configuration& config, VkCommandBuffer cmdBuffer )
    {
        if ( s_hasDebugMarkerSupport && config.debug.renderer.enableRenderAPIDebugGrouping )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

            Debug::vkCmdEndDebugUtilsLabelEXT( cmdBuffer );
        }

        GFXDevice::PopDebugMessage();
    }

    /// Return the Vulkan sampler object's handle for the given hash value
    VkSampler VK_API::GetSamplerHandle( const SamplerDescriptor sampler, size_t& samplerHashInOut )
    {
        thread_local size_t cached_hash = 0u;
        thread_local VkSampler cached_handle = VK_NULL_HANDLE;

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( samplerHashInOut == SamplerDescriptor::INVALID_SAMPLER_HASH )
        {
            samplerHashInOut = GetHash( sampler );
        }
        DIVIDE_GPU_ASSERT( samplerHashInOut != 0u );

        if ( cached_hash == samplerHashInOut )
        {
            return cached_handle;
        }
        cached_hash = samplerHashInOut;

        {
            SharedLock<SharedMutex> r_lock( s_samplerMapLock );
            // If we fail to find the sampler object for the given hash, we print an error and return the default OpenGL handle
            const SamplerObjectMap::const_iterator it = s_samplerMap.find( cached_hash );
            if ( it != std::cend( s_samplerMap ) )
            {
                // Return the Vulkan handle for the sampler object matching the specified hash value
                cached_handle = it->second;
                return cached_handle;
            }
        }

        cached_handle = VK_NULL_HANDLE;
        {
            LockGuard<SharedMutex> w_lock( s_samplerMapLock );
            // Check again
            const SamplerObjectMap::const_iterator it = s_samplerMap.find( cached_hash );
            if ( it == std::cend( s_samplerMap ) )
            {
                // Cache miss. Create the sampler object now.
                // Create and store the newly created sample object. GL_API is responsible for deleting these!
                const VkSampler samplerHandler = vkSamplerObject::Construct( sampler );
                emplace( s_samplerMap, cached_hash, samplerHandler );
                cached_handle = samplerHandler;
            }
            else
            {
                cached_handle = it->second;
            }
        }

        return cached_handle;
    }

    VkPipelineStageFlagBits2 VK_API::AllShaderStages() noexcept
    {
        static bool meshShadersSupported = GFXDevice::GetDeviceInformation()._meshShading._supported;
        return meshShadersSupported ? VK_API::ALL_SHADER_STAGES_WITH_MESH : VK_API::ALL_SHADER_STAGES_NO_MESH;
    }

    RenderTarget_uptr VK_API::newRenderTarget( const RenderTargetDescriptor& descriptor ) const
    {
        return std::make_unique<vkRenderTarget>( _context, descriptor );
    }

    GPUBuffer_uptr VK_API::newGPUBuffer( U32 ringBufferLength, const std::string_view name ) const
    {
        return std::make_unique<vkGPUBuffer>( _context, ringBufferLength, name );
    }

    ShaderBuffer_uptr VK_API::newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const
    {
        return std::make_unique<vkShaderBuffer>( _context, descriptor );
    }

}; //namespace Divide
