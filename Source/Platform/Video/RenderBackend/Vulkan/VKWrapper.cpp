

#include "Headers/VKWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
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

constexpr size_t MAX_BUFFER_COPIES_PER_FLUSH = 32u;

namespace
{
    inline VKAPI_ATTR VkBool32 VKAPI_CALL divide_debug_callback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void* )
    {

        if ( Divide::VK_API::GetStateTracker()._enabledAPIDebugging && !(*Divide::VK_API::GetStateTracker()._enabledAPIDebugging) )
        {
            return VK_FALSE;
        }

        using namespace Divide;

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

        const bool isConsoleImmediate = Console::IsFlagSet( Console::Flags::PRINT_IMMEDIATE );
        const bool severityDecoration = Console::IsFlagSet( Console::Flags::DECORATE_SEVERITY );

        Console::ToggleFlag( Console::Flags::PRINT_IMMEDIATE, true );
        Console::ToggleFlag( Console::Flags::DECORATE_SEVERITY, false );

        if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ||
             messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT )
        {
            Console::printfn( outputError.c_str() );
        }
        else if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
        {
            Console::warnfn( outputError.c_str() );
        }
        else
        {
            Console::errorfn( outputError.c_str() );
            DIVIDE_GPU_ASSERT( VK_API::GetStateTracker()._assertOnAPIError && !(*VK_API::GetStateTracker()._assertOnAPIError), outputError.c_str() );
        }

        Console::ToggleFlag( Console::Flags::DECORATE_SEVERITY, severityDecoration );
        Console::ToggleFlag( Console::Flags::PRINT_IMMEDIATE, isConsoleImmediate );

        return VK_FALSE; // Applications must return false here
    }
}

namespace Divide
{
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

        struct DynamicEntry
        {
            VkDescriptorBufferInfo _info{};
            VkShaderStageFlags _stageFlags{};
        };

        using DynamicBufferEntry = std::array<DynamicEntry, MAX_BINDINGS_PER_DESCRIPTOR_SET>;
        thread_local std::array<DynamicBufferEntry, to_base(DescriptorSetUsage::COUNT)> s_dynamicBindings;
        thread_local fixed_vector<U32, MAX_BINDINGS_PER_DESCRIPTOR_SET * to_base(DescriptorSetUsage::COUNT)> s_dynamicOffsets;
        thread_local bool s_pipelineReset = true;

        void ResetDescriptorDynamicOffsets()
        {
            for ( auto& bindings : s_dynamicBindings )
            {
                bindings.fill( {} );
            }
            s_pipelineReset = true;
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
    VkResolveModeFlags VK_API::s_supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

    VKDeletionQueue VK_API::s_transientDeleteQueue;
    VKDeletionQueue VK_API::s_deviceDeleteQueue;
    VKTransferQueue VK_API::s_transferQueue;
    VKStateTracker VK_API::s_stateTracker;
    eastl::stack<vkShaderProgram*> VK_API::s_reloadedShaders;
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
        pipelineInfo.pTessellationState = &_tessellation;
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

    void VKImmediateCmdContext::flushCommandBuffer(FlushCallback&& function, const char* scopeName )
    {
        LockGuard<Mutex> w_lock( _submitLock );

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

        _context.submitToQueue( _type, submitInfo, fence );
        _bufferIndex = (_bufferIndex + 1u) % BUFFER_COUNT;

        if ( _bufferIndex == 0u )
        {
            ++_wrapCounter;
        }
    }

    void VKStateTracker::init( const Configuration& config, VKDevice* device, VKPerWindowState* mainWindow )
    {
        DIVIDE_GPU_ASSERT(device != nullptr && mainWindow != nullptr);
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
        _pipelineRenderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    }

    VKImmediateCmdContext* VKStateTracker::IMCmdContext( const QueueType type ) const
    {
        return _cmdContexts[to_base( type )].get();
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

    VkCommandBuffer VK_API::getCurrentCommandBuffer() const noexcept
    {
        return s_stateTracker._activeWindow->_swapChain->getFrameData()._commandBuffer;
    }

    void VK_API::idle( [[maybe_unused]] const bool fast ) noexcept
    {
        NOP();
    }

    bool VK_API::drawToWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VKPerWindowState& windowState = _perWindowState[window.getGUID()];
        if ( windowState._window == nullptr )
        {
            windowState._window = &window;
            initStatePerWindow(windowState);
        }

        GetStateTracker()._activeWindow = &windowState;

        const vec2<U16> windowDimensions = window.getDrawableSize();
        VkExtent2D surfaceExtent = windowState._swapChain->surfaceExtent();

        if ( windowDimensions.width != surfaceExtent.width || windowDimensions.height != surfaceExtent.height )
        {
            recreateSwapChain( windowState );
            surfaceExtent = windowState._swapChain->surfaceExtent();
        }

        const VkResult result = windowState._swapChain->beginFrame();
        if ( result != VK_SUCCESS )
        {
            if ( result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR )
            {
                Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( result ).c_str() );
                DIVIDE_UNEXPECTED_CALL();
            }

            recreateSwapChain( windowState );
            windowState._skipEndFrame = true;
            return false;
        }

        return true;
    }

    void VK_API::onRenderThreadLoopStart( )
    {
    }

    void VK_API::onRenderThreadLoopEnd()
    {
    }

    void VK_API::prepareFlushWindow( [[maybe_unused]] DisplayWindow& window )
    {
    }

    void VK_API::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VKPerWindowState& windowState = _perWindowState[window.getGUID()];
        assert( windowState._window != nullptr );

        SCOPE_EXIT{
            GetStateTracker()._activeWindow = nullptr;
        };

        if ( windowState._skipEndFrame )
        {
            windowState._skipEndFrame = false;
            return;
        }

        FlushBufferTransferRequests( windowState._swapChain->getFrameData()._commandBuffer );

        windowState._activeState = {};
        s_dynamicOffsets.reset_lose_memory();

        const VkResult result = windowState._swapChain->endFrame();

        if ( result != VK_SUCCESS )
        {
            if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
            {
                recreateSwapChain( windowState );
            }
            else
            {
                Console::errorfn( LOCALE_STR( "ERROR_GENERIC_VK" ), VKErrorString( result ).c_str() );
                DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    bool VK_API::frameStarted()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

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
        _dummyDescriptorSet = VK_NULL_HANDLE;
        LockManager::CleanExpiredSyncObjects( RenderAPI::Vulkan, GFXDevice::FrameCount() );

        return true;
    }

    bool VK_API::frameEnded()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        s_transientDeleteQueue.onFrameEnd();
        s_deviceDeleteQueue.onFrameEnd();

        while ( !s_reloadedShaders.empty() )
        {
            vkShaderProgram* program = s_reloadedShaders.top();
            for ( auto& it : _compiledPipelines )
            {
                if ( !it.second._isValid )
                {
                    continue;
                }
                if ( it.second._program->getGUID() == program->getGUID() )
                {
                    destroyPipeline( it.second, true );
                }
            }
            s_reloadedShaders.pop();
        }

        //vkResetCommandPool(_device->getVKDevice(), _device->graphicsCommandPool(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

        GetStateTracker().setDefaultState();
        return true;
    }

    void VK_API::recreateSwapChain( VKPerWindowState& windowState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( windowState._window->minimized() )
        {
            idle( false );
            SDLEventManager::pollEvents();
        }
        if ( windowState._window->minimized() )
        {
            return;
        }

        if (windowState._window->getGUID() == _context.context().mainWindow().getGUID() )
        {
            PROFILE_SCOPE( "Wait for idle", Profiler::Category::Graphics);
            s_deviceDeleteQueue.flush( _device->getVKDevice(), true );
            vkDeviceWaitIdle( _device->getVKDevice() );
        }
        const ErrorCode err = windowState._swapChain->create( windowState._window->flags() & to_base(WindowFlags::VSYNC),
                                                              _context.context().config().runtime.adaptiveSync,
                                                              windowState._surface );

        DIVIDE_GPU_ASSERT( err == ErrorCode::NO_ERR );
        // Clear ALL sync objects as they are all invalid after recreating the swapchain. vkDeviceWaitIdle should resolve potential sync issues.
        LockManager::CleanExpiredSyncObjects( RenderAPI::Vulkan, U64_MAX);
    }

    void VK_API::initStatePerWindow( VKPerWindowState& windowState)
    {
        DIVIDE_GPU_ASSERT(windowState._window != nullptr);
        if (windowState._surface == nullptr )
        {
            SDL_Vulkan_CreateSurface( windowState._window->getRawWindow(), _vkbInstance.instance, nullptr, &windowState._surface );
            DIVIDE_GPU_ASSERT(windowState._surface != nullptr);
        }

        windowState._swapChain = std::make_unique<VKSwapChain>( *this, *_device, *windowState._window );
        recreateSwapChain( windowState );
    }

    void VK_API::destroyStatePerWindow( VKPerWindowState& windowState )
    {
        windowState._swapChain.reset();

        if ( _vkbInstance.instance != nullptr )
        {
            vkDestroySurfaceKHR( _vkbInstance.instance, windowState._surface, nullptr );
        }

        windowState = {};
    }

    ErrorCode VK_API::initRenderingAPI( [[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, Configuration& config ) noexcept
    {
        _descriptorSets.fill( VK_NULL_HANDLE );
        _dummyDescriptorSet = VK_NULL_HANDLE ;

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
        VkPhysicalDeviceMaintenance4Properties maintenance4{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES };
        VkPhysicalDeviceProperties2 properties2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext = &maintenance4 };

        VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };

        // Depth/stencil resolve support struct — query and store supported depth resolve modes
        VkPhysicalDeviceDepthStencilResolveProperties depthResolve{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES, .pNext = nullptr };

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

        {
            U32 toolCount = 0u;
            VK_CHECK( vkGetPhysicalDeviceToolProperties( physicalDevice, &toolCount, NULL ) );

            if ( toolCount > 0u )
            {
                std::vector<VkPhysicalDeviceToolPropertiesEXT> tools;
                tools.resize( toolCount );
                VK_CHECK( vkGetPhysicalDeviceToolProperties( physicalDevice, &toolCount, tools.data() ) );

                Console::printfn( LOCALE_STR( "VK_TOOL_INFO" ), toolCount );
    
                for ( VkPhysicalDeviceToolPropertiesEXT& tool : tools )
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

        DIVIDE_GPU_ASSERT( deviceInformation._maxBufferSizeBytes > 0u );
        Console::printfn(LOCALE_STR("GL_VK_BUFFER_MAX_SIZE"), deviceInformation._maxBufferSizeBytes / 1024 / 1024);

        deviceInformation._shaderCompilerThreads = 0xFFFFFFFF;
        CLAMP( config.rendering.maxAnisotropicFilteringLevel,
               U8_ZERO,
               to_U8( deviceProperties.limits.maxSamplerAnisotropy ) );
        deviceInformation._maxAnisotropy = config.rendering.maxAnisotropicFilteringLevel;

        DIVIDE_GPU_ASSERT( PushConstantsStruct::Size() <= deviceProperties.limits.maxPushConstantsSize );

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

        deviceInformation._meshShadingSupported = _device->supportsMeshShaders();

        // How many workgroups can we have per compute dispatch
        for ( U8 i = 0u; i < 3; ++i )
        {
            deviceInformation._maxWorkgroupCount[i] = deviceProperties.limits.maxComputeWorkGroupCount[i];
            deviceInformation._maxWorkgroupSize[i] = deviceProperties.limits.maxComputeWorkGroupSize[i];

            deviceInformation._maxMeshWorkgroupCount[i] = meshProperties.maxMeshWorkGroupCount[i];
            deviceInformation._maxMeshWorkgroupSize[i] = meshProperties.maxMeshWorkGroupSize[i];

            deviceInformation._maxTaskWorkgroupCount[i] = meshProperties.maxTaskWorkGroupCount[i];
            deviceInformation._maxTaskWorkgroupSize[i] = meshProperties.maxTaskWorkGroupSize[i];
        }

        deviceInformation._maxWorkgroupInvocations = deviceProperties.limits.maxComputeWorkGroupInvocations;
        deviceInformation._maxComputeSharedMemoryBytes = deviceProperties.limits.maxComputeSharedMemorySize;
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_WORK_GROUP_INFO" ),
                          deviceInformation._maxWorkgroupCount[0], deviceInformation._maxWorkgroupCount[1], deviceInformation._maxWorkgroupCount[2],
                          deviceInformation._maxWorkgroupSize[0], deviceInformation._maxWorkgroupSize[1], deviceInformation._maxWorkgroupSize[2],
                          deviceInformation._maxWorkgroupInvocations );
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_SHARED_MEMORY_SIZE" ), deviceInformation._maxComputeSharedMemoryBytes / 1024 );

        // Maximum number of varying components supported as outputs in the vertex shader
        deviceInformation._maxVertOutputComponents = deviceProperties.limits.maxVertexOutputComponents;
        Console::printfn( LOCALE_STR( "MAX_VERTEX_OUTPUT_COMPONENTS" ), deviceInformation._maxVertOutputComponents );

        deviceInformation._maxMeshShaderOutputVertices = meshProperties.maxMeshOutputVertices;
        deviceInformation._maxMeshShaderOutputPrimitives = meshProperties.maxMeshOutputPrimitives;
        deviceInformation._maxMeshWorkgroupInvocations = meshProperties.maxMeshWorkGroupInvocations;
        deviceInformation._maxTaskWorkgroupInvocations = meshProperties.maxTaskWorkGroupInvocations;

        Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_VERTICES"), deviceInformation._maxMeshShaderOutputVertices);
        Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_PRIMITIVES"), deviceInformation._maxMeshShaderOutputPrimitives);
        
        Console::printfn(LOCALE_STR("MAX_MESH_SHADER_WORKGROUP_INFO"),
                                    deviceInformation._maxMeshWorkgroupCount[0], deviceInformation._maxMeshWorkgroupCount[1], deviceInformation._maxMeshWorkgroupCount[2],
                                    deviceInformation._maxMeshWorkgroupSize[0], deviceInformation._maxMeshWorkgroupSize[1], deviceInformation._maxMeshWorkgroupSize[2],
                                    deviceInformation._maxMeshWorkgroupInvocations);
                                    
        Console::printfn(LOCALE_STR("MAX_TASK_SHADER_WORKGROUP_INFO"),
                                    deviceInformation._maxTaskWorkgroupCount[0], deviceInformation._maxTaskWorkgroupCount[1], deviceInformation._maxTaskWorkgroupCount[2],
                                    deviceInformation._maxTaskWorkgroupSize[0], deviceInformation._maxTaskWorkgroupSize[1], deviceInformation._maxTaskWorkgroupSize[2],
                                    deviceInformation._maxTaskWorkgroupInvocations);

        deviceInformation._offsetAlignmentBytesUBO = deviceProperties.limits.minUniformBufferOffsetAlignment;
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


        DIVIDE_GPU_ASSERT( Config::MINIMUM_VULKAN_MINOR_VERSION > 2 );

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = vkDevice;
        allocatorInfo.instance = _vkbInstance.instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
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
                vkDeviceWaitIdle( _device->getVKDevice() );
                s_transientDeleteQueue.flush( _device->getVKDevice(), true );
                s_deviceDeleteQueue.flush( _device->getVKDevice(), true );
                for ( auto& pool : s_stateTracker._descriptorAllocators )
                {
                    pool._handle = {};
                    pool._allocatorPool.reset();
                }
                _descriptorLayoutCache.reset();
                _descriptorSetLayouts.fill( VK_NULL_HANDLE );
                _descriptorSets.fill( VK_NULL_HANDLE );
                _dummyDescriptorSet = VK_NULL_HANDLE;

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
                VK_PROFILE(vkCmdBindVertexBuffers, cmdBuffer, vkBuffer->_bindConfig._bindIdx, 1, &impl->_buffer, &offset);
            }
            else if ( impl->_params._usageType == BufferUsageType::INDEX_BUFFER )
            {
                DIVIDE_GPU_ASSERT(vkBuffer->firstIndexOffsetCount() != GPUBuffer::INVALID_INDEX_OFFSET);
                
                DIVIDE_GPU_ASSERT(!hasIndexBuffer, "GL_API::Draw - Multiple index buffers bound!");
                hasIndexBuffer = true;

                //firstIndex += to_U32(activeConfig._offset / elementSizeInBytes);
                firstIndex += vkBuffer->firstIndexOffsetCount();

                VK_PROFILE(vkCmdBindIndexBuffer, cmdBuffer, impl->_buffer, offset, elementSizeInBytes == sizeof(U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
                cmd._cmd.firstIndex = firstIndex;
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
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( getCurrentCommandBuffer() );

        auto& program = GetStateTracker()._pipeline._program;
        DIVIDE_GPU_ASSERT( program != nullptr );
        auto& drawDescriptor = program->perDrawDescriptorSetLayout();
        const bool targetDescriptorEmpty = IsEmpty( drawDescriptor );
        const auto& setUsageData = program->setUsage();

        thread_local VkDescriptorImageInfo imageInfoArray[MAX_BINDINGS_PER_DESCRIPTOR_SET];
        thread_local fixed_vector<VkWriteDescriptorSet, MAX_BINDINGS_PER_DESCRIPTOR_SET> descriptorWrites;
        U8 imageInfoIndex = 0u;

        bool needsBind = false;
        for ( const DescriptorSetEntry& entry : descriptorSetEntries )
        {
            const BaseType<DescriptorSetUsage> usageIdx = to_base( entry._usage );

            if ( !setUsageData[usageIdx] )
            {
                continue;
            }

            if ( entry._usage == DescriptorSetUsage::PER_DRAW && targetDescriptorEmpty )
            {
                continue;
            }

            const bool isPushDescriptor = entry._usage == DescriptorSetUsage::PER_DRAW;

            for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
            {
                const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];

                if ( entry._usage == DescriptorSetUsage::PER_DRAW &&
                     drawDescriptor[srcBinding._slot]._type == DescriptorSetBindingType::COUNT )
                {
                    continue;
                }

                const VkShaderStageFlags stageFlags = GetFlagsForStageVisibility( srcBinding._shaderStageVisibility );

                switch ( srcBinding._data._type )
                {
                    case DescriptorSetBindingType::UNIFORM_BUFFER:
                    case DescriptorSetBindingType::SHADER_STORAGE_BUFFER:
                    {
                        PROFILE_SCOPE( "Bind buffer", Profiler::Category::Graphics);

                        const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;

                        DIVIDE_GPU_ASSERT( bufferEntry._buffer != nullptr );

                        VkBuffer buffer = static_cast<vkBufferImpl*>(bufferEntry._buffer->getBufferImpl())->_buffer;

                        const size_t readOffset = bufferEntry._queueReadIndex * bufferEntry._buffer->alignedBufferSize();

                        if ( entry._usage == DescriptorSetUsage::PER_BATCH && srcBinding._slot == 0 )
                        {
                            // Draw indirect buffer!
                            DIVIDE_GPU_ASSERT( bufferEntry._buffer->getUsage() == BufferUsageType::COMMAND_BUFFER );
                            GetStateTracker()._drawIndirectBuffer = buffer;
                            GetStateTracker()._drawIndirectBufferOffset = readOffset;
                        }
                        else
                        {
                            const VkDeviceSize offset = bufferEntry._range._startOffset * bufferEntry._buffer->getPrimitiveSize() + readOffset;
                            DIVIDE_GPU_ASSERT( bufferEntry._range._length > 0u );
                            const size_t boundRange = bufferEntry._range._length* bufferEntry._buffer->getPrimitiveSize();

                            DynamicEntry& crtBufferInfo = s_dynamicBindings[usageIdx][srcBinding._slot];
                            if ( isPushDescriptor || crtBufferInfo._info.buffer != buffer || crtBufferInfo._info.range > boundRange || (crtBufferInfo._stageFlags & stageFlags) != stageFlags)
                            {
                                crtBufferInfo._info.buffer = buffer;
                                crtBufferInfo._info.offset = isPushDescriptor ? offset : 0u;
                                crtBufferInfo._info.range = boundRange;
                                crtBufferInfo._stageFlags |= stageFlags;


                                VkDescriptorSetLayoutBinding newBinding{};
                                newBinding.descriptorCount = 1u;
                                newBinding.descriptorType = VKUtil::vkDescriptorType( srcBinding._data._type, isPushDescriptor );
                                newBinding.stageFlags = crtBufferInfo._stageFlags;
                                newBinding.binding = srcBinding._slot;
                                newBinding.pImmutableSamplers = nullptr;

                                descriptorWrites.push_back( vk::writeDescriptorSet( newBinding.descriptorType, newBinding.binding, &crtBufferInfo._info, 1u ) );
                            }
                            if (!isPushDescriptor )
                            {
                                for ( auto& dynamicBinding : _descriptorDynamicBindings[usageIdx] )
                                {
                                    if ( dynamicBinding._slot == srcBinding._slot )
                                    {
                                        dynamicBinding._offset = to_U32(offset);
                                        needsBind = true;
                                        break;
                                    }
                                }
                                DIVIDE_GPU_ASSERT( needsBind );
                            }
                        }
                    } break;
                    case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                    {
                        PROFILE_SCOPE( "Bind image sampler", Profiler::Category::Graphics );

                        if ( srcBinding._slot == INVALID_TEXTURE_BINDING )
                        {
                            continue;
                        }

                        const DescriptorCombinedImageSampler& imageSampler = srcBinding._data._sampledImage;
                        if ( imageSampler._image._srcTexture == nullptr ) [[unlikely]]
                        {
                            NOP(); //unbind request;
                        }
                        else
                        {
                            DIVIDE_GPU_ASSERT( TargetType( imageSampler._image ) != TextureType::COUNT );

                            const vkTexture* vkTex = static_cast<const vkTexture*>(imageSampler._image._srcTexture);

                            vkTexture::CachedImageView::Descriptor descriptor{};
                            descriptor._usage = ImageUsage::SHADER_READ;
                            descriptor._format = vkTex->vkFormat();
                            descriptor._type = TargetType( imageSampler._image );
                            descriptor._subRange = imageSampler._image._subRange;

                            const VkImageLayout targetLayout = IsDepthTexture( vkTex->descriptor()._packing ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                            size_t samplerHash = imageSampler._samplerHash;
                            const VkSampler samplerHandle = GetSamplerHandle( imageSampler._sampler, samplerHash );
                            const VkImageView imageView = vkTex->getImageView( descriptor );

                            VkDescriptorImageInfo& imageInfo = imageInfoArray[imageInfoIndex++];
                            imageInfo = vk::descriptorImageInfo( samplerHandle, imageView, targetLayout );
                            
                            VkDescriptorSetLayoutBinding newBinding{};

                            newBinding.descriptorCount = 1;
                            newBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                            newBinding.stageFlags = stageFlags;
                            newBinding.binding = srcBinding._slot;

                            descriptorWrites.push_back( vk::writeDescriptorSet( newBinding.descriptorType, newBinding.binding, &imageInfo, 1u ) );
                        }
                    } break;
                    case DescriptorSetBindingType::IMAGE:
                    {
                        PROFILE_SCOPE( "Bind image", Profiler::Category::Graphics );

                        const DescriptorImageView& imageView = srcBinding._data._imageView;
                        if ( imageView._image._srcTexture == nullptr )
                        {
                            continue;
                        }

                        DIVIDE_GPU_ASSERT( imageView._image._srcTexture != nullptr && imageView._image._subRange._mipLevels._count == 1u );

                        const vkTexture* vkTex = static_cast<const vkTexture*>(imageView._image._srcTexture);

                        DIVIDE_GPU_ASSERT(imageView._usage == ImageUsage::SHADER_READ || imageView._usage == ImageUsage::SHADER_WRITE || imageView._usage == ImageUsage::SHADER_READ_WRITE);

                        vkTexture::CachedImageView::Descriptor descriptor{};
                        descriptor._usage = imageView._usage;
                        descriptor._format = vkTex->vkFormat();
                        descriptor._type = TargetType( imageView._image );
                        descriptor._subRange = imageView._image._subRange;

                        // Should use TextureType::TEXTURE_CUBE_ARRAY
                        DIVIDE_GPU_ASSERT( descriptor._type != TextureType::TEXTURE_CUBE_MAP || descriptor._subRange._layerRange._count == 1u );

                        const VkImageLayout targetLayout = descriptor._usage == ImageUsage::SHADER_READ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                        VkDescriptorImageInfo& imageInfo = imageInfoArray[imageInfoIndex++];
                        imageInfo = vk::descriptorImageInfo( VK_NULL_HANDLE, vkTex->getImageView( descriptor ), targetLayout );


                        VkDescriptorSetLayoutBinding newBinding{};

                        newBinding.descriptorCount = 1;
                        newBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        newBinding.stageFlags = stageFlags;
                        newBinding.binding = srcBinding._slot;

                        descriptorWrites.push_back( vk::writeDescriptorSet( newBinding.descriptorType, newBinding.binding, &imageInfo, 1u ) );
                    } break;
                    default:
                    case DescriptorSetBindingType::COUNT:
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    } break;
                };
            }

            if (!descriptorWrites.empty())
            {
                if ( !isPushDescriptor )
                {
                    PROFILE_SCOPE( "Build and update sets", Profiler::Category::Graphics );
                    PROFILE_TAG("Usage IDX", usageIdx);

                    {
                        PROFILE_SCOPE( "Build", Profiler::Category::Graphics);
                        DescriptorBuilder builder = DescriptorBuilder::Begin( _descriptorLayoutCache.get(), &s_stateTracker._descriptorAllocators[usageIdx]._handle );
                        builder.buildSetFromLayout( _descriptorSets[usageIdx], _descriptorSetLayouts[usageIdx], _device->getVKDevice() );
                    }
                    {
                        for ( VkWriteDescriptorSet& w : descriptorWrites )
                        {
                            w.dstSet = _descriptorSets[usageIdx];
                        }
                        VK_PROFILE( vkUpdateDescriptorSets, _device->getVKDevice(), to_U32( descriptorWrites.size() ), descriptorWrites.data(), 0, nullptr );
                    }
                    needsBind = true;
                }
                else
                {
                    const auto& pipeline = GetStateTracker()._pipeline;
                    VK_PROFILE( vkCmdPushDescriptorSetKHR, getCurrentCommandBuffer(),
                                                           pipeline._bindPoint,
                                                           pipeline._vkPipelineLayout,
                                                           0,
                                                           to_U32(descriptorWrites.size()),
                                                           descriptorWrites.data());
                }
                descriptorWrites.reset_lose_memory();
                s_dynamicBindings[usageIdx] = {};
            }
        }

        if ( needsBind )
        {
            PROFILE_SCOPE( "Bind descriptor sets", Profiler::Category::Graphics );

            thread_local VkDescriptorSetLayout tempLayout{ VK_NULL_HANDLE };

            if ( _dummyDescriptorSet == VK_NULL_HANDLE )
            {
                if ( tempLayout == VK_NULL_HANDLE )
                {
                    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
                    {
                        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                        .bindingCount = 0u
                    };
                    tempLayout = _descriptorLayoutCache->createDescriptorLayout( &descriptorSetLayoutCreateInfo );
                }

                DescriptorAllocator& pool = s_stateTracker._descriptorAllocators[to_base( DescriptorSetUsage::PER_FRAME )];
                DescriptorBuilder::Begin( _descriptorLayoutCache.get(), &pool._handle ).buildSetFromLayout( _dummyDescriptorSet, tempLayout, _device->getVKDevice() );
            }

            VkDescriptorSet tempSets[to_base(DescriptorSetUsage::COUNT)];
            s_dynamicOffsets.reset_lose_memory();

            const U8 offset = 1u;
            U8 setCount = 0u;
            for ( U8 i = 0; i < to_base( DescriptorSetUsage::COUNT ) - offset; ++i )
            {
                const bool setUsed = setUsageData[i + offset];
                tempSets[setCount++] = setUsed ? _descriptorSets[i + offset] : _dummyDescriptorSet;
                if ( setUsed )
                {
                    for ( const DynamicBinding& binding : _descriptorDynamicBindings[i + offset] )
                    {
                        if ( binding._slot != U8_MAX )
                        {
                            s_dynamicOffsets.push_back( binding._offset );
                        }
                    }
                }
            }

            const auto& pipeline = GetStateTracker()._pipeline;
            VK_PROFILE( vkCmdBindDescriptorSets, getCurrentCommandBuffer(),
                                                 pipeline._bindPoint,
                                                 pipeline._vkPipelineLayout,
                                                 offset,
                                                 setCount,
                                                 tempSets,
                                                 to_U32( s_dynamicOffsets.size() ),
                                                 s_dynamicOffsets.data() );
        }


        return true;
    }

    void VK_API::bindDynamicState( const RenderStateBlock& currentState, const RTBlendStates& blendStates, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        bool ret = false;

        auto& activeState = GetStateTracker()._activeWindow->_activeState;

        if ( currentState._stencilEnabled )
        {
            if ( !activeState._isSet || !activeState._block._stencilEnabled )
            {
                vkCmdSetStencilTestEnable( cmdBuffer, VK_TRUE );
                ret = true;
            }
            if ( !activeState._isSet || activeState._block._stencilMask != currentState._stencilMask )
            {
                vkCmdSetStencilCompareMask( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilMask );
                ret = true;
            }
            if ( !activeState._isSet || activeState._block._stencilWriteMask != currentState._stencilWriteMask )
            {
                vkCmdSetStencilWriteMask( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilWriteMask );
                ret = true;
            }
            if ( !activeState._isSet || activeState._block._stencilRef != currentState._stencilRef )
            {
                vkCmdSetStencilReference( cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState._stencilRef );
                ret = true;
            }
            if ( !activeState._isSet || 
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
        else if ( !activeState._isSet || !activeState._block._stencilEnabled )
        {
            vkCmdSetStencilTestEnable( cmdBuffer, VK_FALSE );
            ret = true;
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

        if ( !activeState._isSet || activeState._block._frontFaceCCW != currentState._frontFaceCCW )
        {
            vkCmdSetFrontFace( cmdBuffer, currentState._frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE );
            ret = true;
        }

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
            constexpr U8 attCcount = to_base(RTColourAttachmentSlot::COUNT);
            if ( !activeState._isSet || activeState._block._colourWrite != currentState._colourWrite )
            {
                static std::array<VkColorComponentFlags, attCcount> writeMask;
                const VkColorComponentFlags colourFlags = (currentState._colourWrite.b[0] == 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
                                                          (currentState._colourWrite.b[1] == 1 ? VK_COLOR_COMPONENT_G_BIT : 0) |
                                                          (currentState._colourWrite.b[2] == 1 ? VK_COLOR_COMPONENT_B_BIT : 0) |
                                                          (currentState._colourWrite.b[3] == 1 ? VK_COLOR_COMPONENT_A_BIT : 0);
                writeMask.fill(colourFlags);
                vkCmdSetColorWriteMaskEXT( cmdBuffer, 0, attCcount, writeMask.data() );
                ret = true;
            }

            if ( !activeState._isSet || activeState._blendStates != blendStates )
            {
                static std::array<VkBool32, attCcount> blendEnabled;
                static std::array<VkColorBlendEquationEXT, attCcount> blendEquations;

                for ( U8 i = 0u; i < attCcount; ++i )
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

                vkCmdSetColorBlendEnableEXT(cmdBuffer, 0, attCcount, blendEnabled.data());
                vkCmdSetColorBlendEquationEXT(cmdBuffer, 0, attCcount, blendEquations.data());

                activeState._blendStates = blendStates;
                ret = true;
            }
        }

        if ( ret )
        {
            activeState._block = currentState;
            activeState._isSet = true;
        }
    }

    ShaderResult VK_API::bindPipeline( const Pipeline& pipeline, VkCommandBuffer cmdBuffer )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        size_t stateHash = pipeline.stateHash();
        Util::Hash_combine(stateHash, GetStateTracker()._renderTargetFormatHash );
        if ( !s_hasDynamicBlendStateSupport )
        {
            Util::Hash_combine(stateHash, pipeline.blendStateHash());
        }

        CompiledPipeline& compiledPipeline = _compiledPipelines[stateHash];
        if ( !compiledPipeline._isValid )
        {
            PROFILE_SCOPE( "Compile PSO", Profiler::Category::Graphics);

            thread_local RenderStateBlock defaultState{};
            thread_local VkDescriptorSetLayout dummyLayout = VK_NULL_HANDLE;

            if ( dummyLayout == VK_NULL_HANDLE )
            {
                VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
                {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                    .bindingCount = 0u
                };
                dummyLayout = _descriptorLayoutCache->createDescriptorLayout( &descriptorSetLayoutCreateInfo );
            }

            const PipelineDescriptor& pipelineDescriptor = pipeline.descriptor();
            ShaderProgram* program = Get( pipelineDescriptor._shaderProgramHandle );
            if ( program == nullptr )
            {
                const auto handle = pipelineDescriptor._shaderProgramHandle;
                Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_HANDLE" ), handle._index, handle._generation );
                return ShaderResult::Failed;
            }

            compiledPipeline._program = static_cast<vkShaderProgram*>(program);
            compiledPipeline._topology = pipelineDescriptor._primitiveTopology;
            const RenderStateBlock& currentState = pipelineDescriptor._stateBlock;

            VkPushConstantRange push_constant;
            push_constant.offset = 0u;
            push_constant.size = to_U32( PushConstantsStruct::Size() );
            push_constant.stageFlags = compiledPipeline._program->stageMask();
            compiledPipeline._stageFlags = push_constant.stageFlags;

            VkPipelineLayoutCreateInfo pipeline_layout_info = vk::pipelineLayoutCreateInfo( 0u );
            pipeline_layout_info.pPushConstantRanges = &push_constant;
            pipeline_layout_info.pushConstantRangeCount = 1;

            const ShaderProgram::BindingsPerSetArray& drawLayout = compiledPipeline._program->perDrawDescriptorSetLayout();

            DynamicBindings dynamicBindings{};
            _descriptorSetLayouts[to_base(DescriptorSetUsage::PER_DRAW)] = createLayoutFromBindings(DescriptorSetUsage::PER_DRAW, drawLayout, dynamicBindings);
            compiledPipeline._program->dynamicBindings(dynamicBindings);
            compiledPipeline._program->descriptorSetLayout( _descriptorSetLayouts[to_base( DescriptorSetUsage::PER_DRAW )] );
            
            const auto& setUsageData = compiledPipeline._program->setUsage();

            VkDescriptorSetLayout tempLayouts[to_base( DescriptorSetUsage::COUNT )];

            for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
            {
                tempLayouts[i] = setUsageData[i] ? _descriptorSetLayouts[i] : dummyLayout;
            }
            pipeline_layout_info.pSetLayouts = tempLayouts;
            pipeline_layout_info.setLayoutCount = to_base( DescriptorSetUsage::COUNT );

            VK_CHECK( vkCreatePipelineLayout( _device->getVKDevice(), &pipeline_layout_info, nullptr, &compiledPipeline._vkPipelineLayout ) );

            //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
            const auto& shaderStages = compiledPipeline._program->shaderStages();

            PipelineBuilder pipelineBuilder;

            bool isGraphicsPipeline = false;
            for ( const auto& stage : shaderStages )
            {
                pipelineBuilder._shaderStages.push_back( vk::pipelineShaderStageCreateInfo( stage._shader->stageMask(), stage._shader->handle() ) );
                isGraphicsPipeline = isGraphicsPipeline || stage._shader->stageMask() != VK_SHADER_STAGE_COMPUTE_BIT;
            }
            compiledPipeline._bindPoint = isGraphicsPipeline ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;

            //vertex input controls how to read vertices from vertex buffers. We aren't using it yet
            pipelineBuilder._vertexInputInfo = vk::pipelineVertexInputStateCreateInfo();
            //connect the pipeline builder vertex input info to the one we get from Vertex
            const VertexInputDescription vertexDescription = getVertexDescription( pipelineDescriptor._vertexFormat );
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
            pipelineBuilder._multisampling = vk::pipelineMultisampleStateCreateInfo( msaaSampleFlags );
            pipelineBuilder._multisampling.minSampleShading = 1.f;
            pipelineBuilder._multisampling.alphaToCoverageEnable = pipelineDescriptor._alphaToCoverage ? VK_TRUE : VK_FALSE;
            if ( msaaSamples > 0u )
            {
                pipelineBuilder._multisampling.sampleShadingEnable = VK_TRUE;
            }
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

            compiledPipeline._vkPipeline = pipelineBuilder.build_pipeline( _device->getVKDevice(), _pipelineCache, isGraphicsPipeline );

            if ( isGraphicsPipeline && IsTriangles( pipelineDescriptor._primitiveTopology ) )
            {
                pipelineBuilder._rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
                compiledPipeline._vkPipelineWireframe = pipelineBuilder.build_pipeline( _device->getVKDevice(), _pipelineCache, true );

                Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipelineWireframe, VK_OBJECT_TYPE_PIPELINE, Util::StringFormat("{}_wireframe", program->resourceName().c_str()).c_str());
            }

            Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, program->resourceName().c_str() );
            Debug::SetObjectName( _device->getVKDevice(), (uint64_t)compiledPipeline._vkPipeline, VK_OBJECT_TYPE_PIPELINE, program->resourceName().c_str() );

            compiledPipeline._isValid = true;
        }

        VK_PROFILE( vkCmdBindPipeline, cmdBuffer, compiledPipeline._bindPoint, compiledPipeline._vkPipeline );

        GetStateTracker()._pipeline = compiledPipeline;
        if ( GetStateTracker()._pipelineStageMask != compiledPipeline._stageFlags )
        {
            GetStateTracker()._pipelineStageMask = compiledPipeline._stageFlags;
            GetStateTracker()._pushConstantsValid = false;
        }

        bindDynamicState( pipeline.descriptor()._stateBlock, pipeline.descriptor()._blendStates, cmdBuffer );
        ResetDescriptorDynamicOffsets();

        const U8 stageIdx = to_base( DescriptorSetUsage::PER_DRAW );
        _descriptorSetLayouts[stageIdx] = compiledPipeline._program->descriptorSetLayout();

        return compiledPipeline._program->validatePreBind(false);
    }

    void VK_API::flushPushConstantsLocks()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _uniformsNeedLock )
        {
            _uniformsNeedLock = false;
            flushCommand( &_uniformsMemCommand );
            _uniformsMemCommand._bufferLocks.clear();
        }
    }

    namespace
    {
        struct PerBufferCopies
        {
            VkBuffer _srcBuffer{ VK_NULL_HANDLE };
            VkBuffer _dstBuffer{ VK_NULL_HANDLE };
            fixed_vector<VkBufferCopy2, MAX_BUFFER_COPIES_PER_FLUSH> _copiesPerBuffer;
        };


        using CopyContainer = std::array<PerBufferCopies, MAX_BUFFER_COPIES_PER_FLUSH>;
        using BarrierContainer = std::array<VkBufferMemoryBarrier2, MAX_BUFFER_COPIES_PER_FLUSH>;
        using BatchedTransferQueue = std::array<VKTransferQueue::TransferRequest, MAX_BUFFER_COPIES_PER_FLUSH>;
        using TransferQueueRequestsContainer = std::array<VKTransferQueue::TransferRequest, MAX_BUFFER_COPIES_PER_FLUSH>;

        void PrepareTransferRequest( const VKTransferQueue::TransferRequest& request, bool toWrite, VkBufferMemoryBarrier2& memBarrierOut )
        {
            memBarrierOut = vk::bufferMemoryBarrier2();

            if ( toWrite )
            {
                memBarrierOut.srcStageMask = request.dstStageMask;
                memBarrierOut.srcAccessMask = request.dstAccessMask;

                memBarrierOut.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrierOut.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            else
            {
                memBarrierOut.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrierOut.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

                memBarrierOut.dstStageMask = request.dstStageMask;
                memBarrierOut.dstAccessMask = request.dstAccessMask;
            }

            memBarrierOut.offset = request.dstOffset;
            memBarrierOut.size = request.size;
            memBarrierOut.buffer = request.dstBuffer;
        }
    };

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

    struct BufferTransferProcessor
    {
        void flushBarriers(VkCommandBuffer cmdBuffer) noexcept
        {
            if (0u == _barrierCount)
            {
                return;
            }

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.bufferMemoryBarrierCount = to_U32(_barrierCount);
            dependencyInfo.pBufferMemoryBarriers = _barriers.data();

            VK_UT_IF_CHECK(cmdBuffer != VK_NULL_HANDLE)
            {
                VK_PROFILE(vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo);
            }

            _barrierCount = 0u;
        };

        void flushCopies(VkCommandBuffer cmdBuffer) noexcept
        {
            if (0u == _copyRequestsCount)
            {
                return;
            }

            for (size_t k = 0u; k < _copyRequestsCount; ++k)
            {
                PerBufferCopies& entry = _copyRequests[k];

                if (entry._copiesPerBuffer.empty())
                {
                    continue;
                }

                VkCopyBufferInfo2 copyInfo
                {
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = entry._srcBuffer,
                    .dstBuffer = entry._dstBuffer,
                    .regionCount = to_U32(entry._copiesPerBuffer.size()),
                    .pRegions = entry._copiesPerBuffer.data()
                };

                VK_UT_IF_CHECK(cmdBuffer != VK_NULL_HANDLE)
                {
                    VK_PROFILE(vkCmdCopyBuffer2, cmdBuffer, &copyInfo);
                }

                entry._srcBuffer = VK_NULL_HANDLE;
                entry._dstBuffer = VK_NULL_HANDLE;
                entry._copiesPerBuffer.reset_lose_memory();
            }
            _copyRequestsCount = 0u;
        };

        void processCurrentBatch( VkCommandBuffer cmdBuffer) noexcept
        {
            if (0u == _transferBatchedCount)
            {
                return;
            }

            // prepare pre-copy barriers
            for (size_t i = 0u; i < _transferBatchedCount; ++i)
            {
                VkBufferMemoryBarrier2 b = vk::bufferMemoryBarrier2();
                PrepareTransferRequest(_transferQueueBatched[i], true, b);
                addBarrier(b, cmdBuffer);
            }

            flushBarriers(cmdBuffer);

            _copyRequestsCount = 0u;
            for (size_t i = 0u; i < _transferBatchedCount; ++i)
            {
                const auto& tr = _transferQueueBatched[i];
                VkBufferCopy2 copy
                {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = tr.srcOffset,
                    .dstOffset = tr.dstOffset,
                    .size = tr.size
                };

                bool found = false;
                for (size_t j = 0u; j < _copyRequestsCount; ++j)
                {
                    PerBufferCopies& entry = _copyRequests[j];

                    if (entry._srcBuffer == tr.srcBuffer &&
                        entry._dstBuffer == tr.dstBuffer &&
                        entry._copiesPerBuffer.size() < MAX_BUFFER_COPIES_PER_FLUSH)
                    {
                        entry._copiesPerBuffer.emplace_back( copy );
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    // initialize new slot
                    PerBufferCopies& entry = _copyRequests[_copyRequestsCount++];
                    entry._srcBuffer = tr.srcBuffer;
                    entry._dstBuffer = tr.dstBuffer;
                    entry._copiesPerBuffer.reset_lose_memory();
                    entry._copiesPerBuffer.emplace_back(copy);

                    if (MAX_BUFFER_COPIES_PER_FLUSH == _copyRequestsCount)
                    {
                        flushCopies(cmdBuffer);
                        _copyRequestsCount = 0u;
                    }
                }
            }

            flushCopies(cmdBuffer);

            for (size_t i = 0u; i < _transferBatchedCount; ++i)
            {
                VkBufferMemoryBarrier2 b = vk::bufferMemoryBarrier2();
                PrepareTransferRequest(_transferQueueBatched[i], false, b);
                addBarrier(b, cmdBuffer);
            }

            _transferBatchedCount = 0u;
        };

        void addBarrier(const VkBufferMemoryBarrier2& barrier, VkCommandBuffer cmdBuffer) noexcept
        {
            _barriers[_barrierCount++] = barrier;
            if (MAX_BUFFER_COPIES_PER_FLUSH == _barrierCount)
            {
                flushBarriers(cmdBuffer);
                _barrierCount = 0u;
            }
        }

        void reset() noexcept
        {
            _transferBatchedCount = 0u;
            _barrierCount = 0u;
            _copyRequestsCount = 0u;
        }

        size_t _transferBatchedCount{0u};
        size_t _barrierCount{0u};
        size_t _copyRequestsCount{0u};
        CopyContainer _copyRequests{};
        BarrierContainer _barriers{};
        BatchedTransferQueue _transferQueueBatched{};
    };

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
        thread_local BufferTransferProcessor s_processor{};
        // ConsumerToken must be per-consumer-thread
        thread_local moodycamel::ConsumerToken s_transferConsumerToken( s_transferQueue._requests );

        s_processor.reset();

        while (true)
        {
            const size_t dequeued = s_transferQueue._requests.try_dequeue_bulk(s_transferConsumerToken, requests.data(), MAX_BUFFER_COPIES_PER_FLUSH);
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
                
                s_processor._transferQueueBatched[s_processor._transferBatchedCount++] = request;
                if ( MAX_BUFFER_COPIES_PER_FLUSH == s_processor._transferBatchedCount )
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
        static mat4<F32> s_defaultPushConstants[2] = { MAT4_ZERO, MAT4_ZERO };

        auto& stateTracker = GetStateTracker();

        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        if ( GFXDevice::IsSubmitCommand( cmd->type() ) )
        {
            FlushBufferTransferRequests( cmdBuffer );
        }

        if ( stateTracker._activeRenderTargetID == SCREEN_TARGET_ID )
        {
            flushPushConstantsLocks();
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

                VkRenderingInfo renderingInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
                if ( crtCmd->_target == SCREEN_TARGET_ID )
                {
                    VkRenderingAttachmentInfo attachmentInfo
                    {
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = VK_NULL_HANDLE,
                        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue =
                        {
                            .color =
                            {
                                DefaultColours::DIVIDE_BLUE.r,
                                DefaultColours::DIVIDE_BLUE.g,
                                DefaultColours::DIVIDE_BLUE.b,
                                DefaultColours::DIVIDE_BLUE.a
                            }
                        }
                    };

                    PROFILE_SCOPE( "Draw to screen", Profiler::Category::Graphics);

                    VKSwapChain* swapChain = stateTracker._activeWindow->_swapChain.get();

                    attachmentInfo.imageView = swapChain->getCurrentImageView();
                    stateTracker._pipelineRenderInfo.colorAttachmentCount = 1u;
                    stateTracker._pipelineRenderInfo.pColorAttachmentFormats = &swapChain->getSwapChain().image_format;

                    renderingInfo.renderArea = 
                    {
                        .offset = {0, 0},
                        .extent = swapChain->surfaceExtent()
                    };
                    renderingInfo.layerCount = 1u;
                    renderingInfo.colorAttachmentCount = 1u;
                    renderingInfo.pColorAttachments = &attachmentInfo;

                    VkImageMemoryBarrier2 imageBarrier = vk::imageMemoryBarrier2();
                    imageBarrier.image = swapChain->getCurrentImage();
                    imageBarrier.subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    };

                    imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                    imageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                    VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                    dependencyInfo.imageMemoryBarrierCount = 1u;
                    dependencyInfo.pImageMemoryBarriers = &imageBarrier;
                    
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo);

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

                    stateTracker._renderTargetFormatHash = 0u;
                    for ( U32 i = 0u; i < stateTracker._pipelineRenderInfo.colorAttachmentCount; ++i )
                    {
                        Util::Hash_combine( stateTracker._renderTargetFormatHash, stateTracker._pipelineRenderInfo.pColorAttachmentFormats[i]);
                    }
                
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
                    VK_PROFILE( vkCmdBeginRendering, cmdBuffer, &renderingInfo);
                }
            } break;
            case GFX::CommandType::END_RENDER_PASS:
            {
                PROFILE_SCOPE( "END_RENDER_PASS", Profiler::Category::Graphics );

                VK_PROFILE( vkCmdEndRendering, cmdBuffer );
                if ( stateTracker._activeRenderTargetID == SCREEN_TARGET_ID )
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
                else
                {
                    vkRenderTarget* rt = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget( stateTracker._activeRenderTargetID ));
                    const GFX::EndRenderPassCommand* crtCmd = cmd->As<GFX::EndRenderPassCommand>();
                    Attorney::VKAPIRenderTarget::end( *rt, cmdBuffer, crtCmd->_transitionMask );
                    stateTracker._activeRenderTargetID = SCREEN_TARGET_ID;
                }

                PopDebugMessage( _context.context().config(), cmdBuffer );
                stateTracker._renderTargetFormatHash = 0u;
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
                        if ( stateTracker._pipeline._program->uploadUniformData( *uniforms, _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).impl(), _uniformsMemCommand ) )
                        {
                            _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).dirty( true );
                            _uniformsNeedLock = _uniformsNeedLock || _uniformsMemCommand._bufferLocks.empty();
                        }
                    }
                    if ( pushConstantsCmd->_fastData.set() )
                    {
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        stateTracker._pipeline._program->stageMask(),
                                                        0,
                                                        to_U32( PushConstantsStruct::Size() ),
                                                        pushConstantsCmd->_fastData.dataPtr() );

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

                if ( stateTracker._pipeline._vkPipeline != VK_NULL_HANDLE )
                {
                    if ( !stateTracker._pushConstantsValid )
                    {
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        stateTracker._pipeline._program->stageMask(),
                                                        0,
                                                        to_U32( PushConstantsStruct::Size() ),
                                                        &s_defaultPushConstants[0].mat );
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
                if (stateTracker._pipeline._vkPipeline != VK_NULL_HANDLE)
                {
                    if ( !stateTracker._pushConstantsValid )
                    {
                        VK_PROFILE( vkCmdPushConstants, cmdBuffer,
                                                        stateTracker._pipeline._vkPipelineLayout,
                                                        stateTracker._pipeline._program->stageMask(),
                                                        0,
                                                        to_U32( PushConstantsStruct::Size() ),
                                                        &s_defaultPushConstants[0].mat );
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
                            break;
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
                        case BufferSyncUsage::CPU_WRITE_TO_CPU_READ:
                        {
                            memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                            memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                            memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                            memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
                        }break;
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
                        default : DIVIDE_UNEXPECTED_CALL(); break;
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

                    const bool isDepthTexture = IsDepthTexture( vkTex->descriptor()._packing );
                    const bool isCube = IsCubeTexture( vkTex->descriptor()._texType);

                    vkTexture::TransitionType transitionType = vkTexture::TransitionType::COUNT;

                    switch ( it._targetLayout )
                    {
                        case ImageUsage::UNDEFINED:
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        } break;
                        case ImageUsage::SHADER_READ:
                        {
                            switch ( it._sourceLayout )
                            {
                                case ImageUsage::UNDEFINED:
                                {
                                    transitionType = isDepthTexture ? vkTexture::TransitionType::UNDEFINED_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::UNDEFINED_TO_SHADER_READ_COLOUR;
                                } break;
                                case ImageUsage::SHADER_READ:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                                case ImageUsage::SHADER_WRITE:
                                {
                                    transitionType = isDepthTexture ? vkTexture::TransitionType::GENERAL_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::GENERAL_TO_SHADER_READ_COLOUR;
                                }break;
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    transitionType = isDepthTexture ? vkTexture::TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_COLOUR;
                                } break;
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ;
                                } break;
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                            }
                        } break;
                        case ImageUsage::SHADER_WRITE:
                        {
                            switch ( it._sourceLayout )
                            {
                                case ImageUsage::UNDEFINED:
                                {
                                    transitionType = vkTexture::TransitionType::UNDEFINED_TO_GENERAL;
                                } break;
                                case ImageUsage::SHADER_READ:
                                {
                                    transitionType = isDepthTexture ? vkTexture::TransitionType::SHADER_READ_DEPTH_TO_GENERAL : vkTexture::TransitionType::SHADER_READ_COLOUR_TO_GENERAL;
                                } break;
                                case ImageUsage::SHADER_WRITE:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                }break;
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    NOP(); // Both in general layout
                                } break;
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_WRITE;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
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
                                    transitionType = isDepthTexture ? vkTexture::TransitionType::SHADER_READ_DEPTH_TO_SHADER_READ_WRITE : vkTexture::TransitionType::SHADER_READ_COLOUR_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::SHADER_WRITE:
                                {
                                    NOP(); // Both in general layout
                                }break;
                                case ImageUsage::SHADER_READ_WRITE:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                                case ImageUsage::RT_COLOUR_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ_WRITE;
                                } break;
                                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                                {
                                    transitionType = vkTexture::TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ_WRITE;
                                } break;
                                default:
                                {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                            }
                        } break;
                        case ImageUsage::RT_COLOUR_ATTACHMENT:
                        {
                            transitionType = vkTexture::TransitionType::UNDEFINED_TO_COLOUR_ATTACHMENT;
                        } break;
                        case ImageUsage::RT_DEPTH_ATTACHMENT:
                        {
                            transitionType = vkTexture::TransitionType::UNDEFINED_TO_DEPTH_ATTACHMENT;
                        } break;
                        case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                        {
                            transitionType = vkTexture::TransitionType::UNDEFINED_TO_DEPTH_STENCIL_ATTACHMENT;
                        } break;
                        default: DIVIDE_UNEXPECTED_CALL();
                    };

                    if ( transitionType != vkTexture::TransitionType::COUNT )
                    {
                        auto subRange = it._targetView._subRange;
                        const VkImageSubresourceRange subResourceRange = {
                            .aspectMask = vkTexture::GetAspectFlags( vkTex->descriptor() ),
                            .baseMipLevel = subRange._mipLevels._offset,
                            .levelCount = subRange._mipLevels._count == ALL_MIPS ? VK_REMAINING_MIP_LEVELS : subRange._mipLevels._count,
                            .baseArrayLayer = subRange._layerRange._offset,
                            .layerCount = subRange._layerRange._count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : subRange._layerRange._count * (isCube ? 6u : 1u),
                        };

                        vkTexture::TransitionTexture( transitionType, subResourceRange, { vkTex->image()->_image, vkTex->resourceName().c_str() }, imageBarriers[imageBarrierCount++] );
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
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    void VK_API::preFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GetStateTracker()._activeRenderTargetID = SCREEN_TARGET_ID;
        GetStateTracker()._activeRenderTargetColourAttachmentCount = 1u;
        GetStateTracker()._activeRenderTargetDimensions = s_stateTracker._activeWindow->_window->getDrawableSize();
        // We don't really know what happened before this state and at worst this is going to end up into an 
        // extra vkCmdPushConstants call with default data, so better safe.
        GetStateTracker()._pushConstantsValid = false;
        FlushBufferTransferRequests( getCurrentCommandBuffer() );
    }

    void VK_API::postFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        flushPushConstantsLocks();
        s_transientDeleteQueue.flush( _device->getDevice() );
        GetStateTracker()._activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        GetStateTracker()._activeRenderTargetColourAttachmentCount = 1u;
        GetStateTracker()._activeRenderTargetDimensions = s_stateTracker._activeWindow->_window->getDrawableSize();
    }

    bool VK_API::setViewportInternal( const Rect<I32>& newViewport ) noexcept
    {
        return setViewportInternal( newViewport, getCurrentCommandBuffer() );
    }

    bool VK_API::setViewportInternal( const Rect<I32>& newViewport, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        VkViewport targetViewport{};
        targetViewport.width = to_F32( newViewport.sizeX );
        targetViewport.height = -to_F32( newViewport.sizeY );
        targetViewport.x = to_F32(newViewport.offsetX);
        if ( newViewport.offsetY == 0 )
        {
            targetViewport.y = to_F32(newViewport.sizeY);
        }
        else
        {
            targetViewport.y = to_F32(/*newViewport.sizeY - */newViewport.offsetY);
            targetViewport.y = GetStateTracker()._activeRenderTargetDimensions.height + targetViewport.y;
        }
        targetViewport.minDepth = 0.f;
        targetViewport.maxDepth = 1.f;

        vkCmdSetViewport( cmdBuffer, 0, 1, &targetViewport );
        return true;
    }

    bool VK_API::setScissorInternal( const Rect<I32>& newScissor ) noexcept
    {
        return setScissorInternal( newScissor, getCurrentCommandBuffer() );
    }

    bool VK_API::setScissorInternal( const Rect<I32>& newScissor, VkCommandBuffer cmdBuffer ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        const VkOffset2D offset{ std::max( 0, newScissor.offsetX ), std::max( 0, newScissor.offsetY ) };
        const VkExtent2D extent{ to_U32( newScissor.sizeX ),to_U32( newScissor.sizeY ) };
        const VkRect2D targetScissor{ offset, extent };
        vkCmdSetScissor( cmdBuffer, 0, 1, &targetScissor );
        return true;
    }

    VkDescriptorSetLayout VK_API::createLayoutFromBindings(const DescriptorSetUsage usage, const ShaderProgram::BindingsPerSetArray& bindings, DynamicBindings& dynamicBindings )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        thread_local fixed_vector<VkDescriptorSetLayoutBinding, MAX_BINDINGS_PER_DESCRIPTOR_SET> layoutBinding{};

        dynamicBindings.reset_lose_memory();
        layoutBinding.reset_lose_memory();

        const bool isPushDescriptor = usage == DescriptorSetUsage::PER_DRAW;

        for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
        {
            if ( bindings[slot]._type == DescriptorSetBindingType::COUNT || (slot == 0 && usage == DescriptorSetUsage::PER_BATCH ))
            {
                continue;
            }

            VkDescriptorSetLayoutBinding& newBinding = layoutBinding.emplace_back();
            newBinding.descriptorCount = 1u;
            newBinding.descriptorType = VKUtil::vkDescriptorType( bindings[slot]._type, isPushDescriptor );
            newBinding.stageFlags = GetFlagsForStageVisibility( bindings[slot]._visibility );
            newBinding.binding = slot;
            newBinding.pImmutableSamplers = nullptr;

            if ( !isPushDescriptor && (bindings[slot]._type == DescriptorSetBindingType::UNIFORM_BUFFER || bindings[slot]._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER ))
            {
                dynamicBindings.emplace_back(DynamicBinding{
                    ._offset = 0u,
                    ._slot = slot
                });
            }
        }

        eastl::sort(begin(dynamicBindings), end(dynamicBindings), []( const DynamicBinding& bindingA, const DynamicBinding& bindingB ) { return bindingA._slot <= bindingB._slot; });

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo = vk::descriptorSetLayoutCreateInfo( layoutBinding.data(), to_U32( layoutBinding.size() ) );
        if ( isPushDescriptor )
        {
            layoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
        }
        return _descriptorLayoutCache->createDescriptorLayout( &layoutCreateInfo );
    }
    
    void VK_API::initDescriptorSets()
    {
        const ShaderProgram::BindingSetData& bindingData = ShaderProgram::GetBindingSetData();

        _descriptorLayoutCache = std::make_unique<DescriptorLayoutCache>( _device->getVKDevice() );

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

            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ShaderProgram::GetBindingCount(static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER) * 1.f);
            pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::IMAGE ) * 1.f );
            if ( i == to_base( DescriptorSetUsage::PER_DRAW ) )
            {
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::UNIFORM_BUFFER ) * 1.f );
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::SHADER_STORAGE_BUFFER ) * 1.f );
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 0.f);
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 0.f);
            }
            else
            {
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::UNIFORM_BUFFER ) * 1.f );
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, ShaderProgram::GetBindingCount( static_cast<DescriptorSetUsage>(i), DescriptorSetBindingType::SHADER_STORAGE_BUFFER ) * 1.f );
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0.f );
                pool._allocatorPool->SetPoolSizeMultiplier( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0.f );
            }

            pool._handle = pool._allocatorPool->GetAllocator();
        }
    }

    void VK_API::onThreadCreated( [[maybe_unused]] const size_t threadIndex, [[maybe_unused]] const std::thread::id& threadID, [[maybe_unused]] const bool isMainRenderThread ) noexcept
    {
    }

    void VK_API::OnShaderReloaded( vkShaderProgram* program )
    {
        if ( program == nullptr )
        {
            return;
        }

        s_reloadedShaders.push(program);
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
        static bool meshShadersSupported = GFXDevice::GetDeviceInformation()._meshShadingSupported;
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
