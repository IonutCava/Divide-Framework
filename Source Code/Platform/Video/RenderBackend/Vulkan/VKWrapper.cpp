#include "stdafx.h"

#include "Headers/VKWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Buffers/Headers/vkFramebuffer.h"
#include "Buffers/Headers/vkShaderBuffer.h"
#include "Buffers/Headers/vkGenericVertexData.h"

#include "Shaders/Headers/vkShaderProgram.h"

#include "Textures/Headers/vkTexture.h"

#include <sdl/include/SDL_vulkan.h>

#define VMA_IMPLEMENTATION
#include "Headers/VMAInclude.h"

namespace {
    inline VKAPI_ATTR VkBool32 VKAPI_CALL divide_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                void*)
    {

        const auto to_string_message_severity = [](VkDebugUtilsMessageSeverityFlagBitsEXT s) -> const char* {
            switch (s) {
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                    return "VERBOSE";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                    return "ERROR";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                    return "WARNING";
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                    return "INFO";
                default:
                    return "UNKNOWN";
            }
        };

        const auto to_string_message_type = [](VkDebugUtilsMessageTypeFlagsEXT s) -> const char* {
            if (s == 7) return "General | Validation | Performance";
            if (s == 6) return "Validation | Performance";
            if (s == 5) return "General | Performance";
            if (s == 4 /*VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT*/) return "Performance";
            if (s == 3) return "General | Validation";
            if (s == 2 /*VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT*/) return "Validation";
            if (s == 1 /*VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT*/) return "General";
            return "Unknown";
        };

        Divide::Console::errorfn("[%s: %s]\n%s\n", to_string_message_severity(messageSeverity), to_string_message_type(messageType), pCallbackData->pMessage);
        if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
        return VK_FALSE; // Applications must return false here
    }
}

namespace Divide {
    bool VK_API::s_hasDebugMarkerSupport = false;
    VKDeletionQueue VK_API::s_transientDeleteQueue;
    VKDeletionQueue VK_API::s_deviceDeleteQueue;
    VKTransferQueue VK_API::s_transferQueue;

    VKStateTracker_uptr VK_API::s_stateTracker = nullptr;

    VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
        //make viewport state from our stored viewport and scissor.
        //at the moment we won't support multiple viewports or scissors
        VkPipelineViewportStateCreateInfo viewportState = vk::pipelineViewportStateCreateInfo(1, 1);
        viewportState.pViewports = &_viewport;
        viewportState.pScissors = &_scissor;

        //setup dummy color blending. We aren't using transparent objects yet
        //the blending is just "no blend", but we do write to the color attachment
        const VkPipelineColorBlendStateCreateInfo colorBlending = vk::pipelineColorBlendStateCreateInfo(1, &_colorBlendAttachment);

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_DEPTH_BIAS,

            //ToDo:
             /*VK_DYNAMIC_STATE_CULL_MODE,
             VK_DYNAMIC_STATE_FRONT_FACE,
             VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
             VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
             VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
             VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
             VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
             VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
             VK_DYNAMIC_STATE_STENCIL_OP,
             VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
             VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
             VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,*/
        };

        constexpr U32 stateCount = to_U32(std::size(dynamicStates));
        const VkPipelineDynamicStateCreateInfo dynamicState = vk::pipelineDynamicStateCreateInfo(dynamicStates, stateCount);

        //build the actual pipeline
        //we now use all of the info structs we have been writing into into this one to create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = vk::pipelineCreateInfo(_pipelineLayout, pass);
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.stageCount = to_U32(_shaderStages.size());
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

        //it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
        VkPipeline newPipeline;
        if (vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
            Console::errorfn("failed to create pipeline");
            return VK_NULL_HANDLE; // failed to create graphics pipeline
        } else {
            return newPipeline;
        }
    }

    void VKDeletionQueue::push(DELEGATE<void, VkDevice>&& function) {
        ScopedLock<Mutex> w_lock(_deletionLock);
        _deletionQueue.push_back(std::move(function));
    }

    void VKDeletionQueue::flush(VkDevice device) {
        ScopedLock<Mutex> w_lock(_deletionLock);
        // reverse iterate the deletion queue to delete all the buffers
        for (auto it = _deletionQueue.rbegin(); it != _deletionQueue.rend(); it++) {
            (*it)(device);
        }

        _deletionQueue.clear();
    }

    bool VKDeletionQueue::empty() const {
        ScopedLock<Mutex> w_lock(_deletionLock);
        return _deletionQueue.empty();
    }

    VKImmediateCmdContext::VKImmediateCmdContext(VKDevice& context)
        : _context(context)
    {
        const VkFenceCreateInfo fenceCreateInfo = vk::fenceCreateInfo();
        vkCreateFence(_context.getVKDevice(), &fenceCreateInfo, nullptr, &_submitFence);
        _commandPool = _context.createCommandPool(_context.getQueueIndex(vkb::QueueType::graphics), 0);

        const VkCommandBufferAllocateInfo cmdBufAllocateInfo = vk::commandBufferAllocateInfo(_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
        VK_CHECK(vkAllocateCommandBuffers(_context.getVKDevice(), &cmdBufAllocateInfo, &_commandBuffer));
    }

    VKImmediateCmdContext::~VKImmediateCmdContext()
    {
        vkDestroyCommandPool(_context.getVKDevice(), _commandPool, nullptr);
        vkDestroyFence(_context.getVKDevice(), _submitFence, nullptr);
    }

    void VKImmediateCmdContext::flushCommandBuffer(std::function<void(VkCommandBuffer cmd)>&& function) {
        UniqueLock<Mutex> w_lock(_submitLock);

        // Begin the command buffer recording.
        // We will use this command buffer exactly once before resetting, so we tell Vulkan that
        const VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VkCommandBuffer cmd = _commandBuffer;
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        // Execute the function
        function(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkSubmitInfo submitInfo = vk::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        _context.submitToQueueAndWait(vkb::QueueType::graphics, submitInfo, _submitFence);

        // Reset the command buffers inside the command pool
        vkResetCommandPool(_context.getVKDevice(), _commandPool, 0);
    }

    void VK_API::RegisterCustomAPIDelete(DELEGATE<void, VkDevice>&& cbk, const bool isResourceTransient) {
        if (isResourceTransient) {
            s_transientDeleteQueue.push(std::move(cbk));
        } else {
            s_deviceDeleteQueue.push(std::move(cbk));
        }
    }

    void VK_API::RegisterTransferRequest(const VKTransferQueue::TransferRequest& request) {
        if (request._immediate) {
            VK_API::GetStateTracker()->_cmdContext->flushCommandBuffer([&request](VkCommandBuffer cmd) {
                if (request.srcBuffer != VK_NULL_HANDLE) {
                    VkBufferCopy copy;
                    copy.dstOffset = request.dstOffset;
                    copy.srcOffset = request.srcOffset;
                    copy.size = request.size;
                    vkCmdCopyBuffer(cmd, request.srcBuffer, request.dstBuffer, 1, &copy);
                }

                VkBufferMemoryBarrier2 memoryBarrier = vk::bufferMemoryBarrier2();
                memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                memoryBarrier.dstStageMask = request.dstStageMask;
                memoryBarrier.dstAccessMask = request.dstAccessMask;
                memoryBarrier.offset = request.dstOffset;
                memoryBarrier.size = request.size;
                memoryBarrier.buffer = request.dstBuffer;

                VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                dependencyInfo.bufferMemoryBarrierCount = 1;
                dependencyInfo.pBufferMemoryBarriers = &memoryBarrier;

                //ToDo: batch these up! -Ionut
                vkCmdPipelineBarrier2(cmd, &dependencyInfo);
            });
        } else {
            UniqueLock<Mutex> w_lock(s_transferQueue._lock);
            s_transferQueue._requests.push_back(request);
        }
    }

    VK_API::VK_API(GFXDevice& context) noexcept
        : _context(context)
    {
    }

    VkCommandBuffer VK_API::getCurrentCommandBuffer() const {
        return _commandBuffers[_currentFrameIndex];
    }

    void VK_API::idle([[maybe_unused]] const bool fast) noexcept {
        vkShaderProgram::Idle(_context.context());
    }

    bool VK_API::beginFrame(DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        const auto& windowDimensions = window.getDrawableSize();
        const VkExtent2D windowExtents{ windowDimensions.width, windowDimensions.height };

        if (_windowExtents.width != windowExtents.width || _windowExtents.height != windowExtents.height) {
            recreateSwapChain(window);
        }

        const VkResult result = _swapChain->beginFrame();
        if (result != VK_SUCCESS) {
            if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) {
                Console::errorfn("Detected Vulkan error: %s\n", VKErrorString(result).c_str());
                DIVIDE_UNEXPECTED_CALL();
            }
            recreateSwapChain(window);
            _skipEndFrame = true;
            return false;
        }

        //naming it cmd for shorter writing
        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();

        //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
        VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

        //make a clear-color from frame number. This will flash with a 120*pi frame period.
        VkClearValue clearValue{};
        clearValue.color = { { 0.0f, 0.0f, abs(sin(GFXDevice::FrameCount() / 120.f)), 1.0f } };

        //start the main renderpass.
        //We will use the clear color from above, and the framebuffer of the index the swapchain gave us
        VkRenderPassBeginInfo rpInfo = vk::renderPassBeginInfo();

        rpInfo.renderPass = _swapChain->getRenderPass();
        rpInfo.renderArea.offset.x = 0;
        rpInfo.renderArea.offset.y = 0;
        rpInfo.renderArea.extent = windowExtents;
        rpInfo.framebuffer = _swapChain->getCurrentFrameBuffer();

        //connect clear values
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmdBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        //once we start adding rendering commands, they will go here
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline._pipeline);

        // Set dynamic state to default
        GetStateTracker()->_dynamicState = {};
        setViewport({ 0, 0, windowDimensions.width, windowDimensions.height });
        setScissor({ 0, 0, windowDimensions.width, windowDimensions.height });
        { //Draw a triangle;
            GetStateTracker()->_activeTopology = PrimitiveTopology::TRIANGLES;
            draw({}, cmdBuffer);
        }

        return true;
    }

    void VK_API::endFrame(DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        if (_skipEndFrame) {
            _skipEndFrame = false;
            return;
        }

        VkCommandBuffer cmd = getCurrentCommandBuffer();

        //finalize the render pass
        vkCmdEndRenderPass(cmd);
        //finalize the command buffer (we can no longer add commands, but it can now be executed)
        VK_CHECK(vkEndCommandBuffer(cmd));

        const VkResult result = _swapChain->endFrame(vkb::QueueType::graphics, cmd);
        
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                recreateSwapChain(window);
            } else {
                Console::errorfn("Detected Vulkan error: %s\n", VKErrorString(result).c_str());
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        _currentFrameIndex = ++_currentFrameIndex % VKSwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    ErrorCode VK_API::initRenderingAPI([[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config) noexcept {
        s_stateTracker = eastl::make_unique<VKStateTracker>();

        s_transientDeleteQueue.flags(s_transientDeleteQueue.flags() | to_base(VKDeletionQueue::Flags::TREAT_AS_TRANSIENT));

        const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();

        auto systemInfoRet = vkb::SystemInfo::get_system_info();
        if (!systemInfoRet) {
            Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), systemInfoRet.error().message().c_str());
            return ErrorCode::VK_OLD_HARDWARE;
        }

        //make the Vulkan instance, with basic debug features
        vkb::InstanceBuilder builder{};
        builder.set_app_name(window.title())
               .set_engine_name(Config::ENGINE_NAME)
               .set_engine_version(Config::ENGINE_VERSION_MAJOR, Config::ENGINE_VERSION_MINOR, Config::ENGINE_VERSION_PATCH)
               .request_validation_layers(Config::ENABLE_GPU_VALIDATION)
               .require_api_version(1, Config::DESIRED_VULKAN_MINOR_VERSION, 0)
               .set_debug_callback(divide_debug_callback)
               .set_debug_callback_user_data_pointer(this);

        auto systemInfo = systemInfoRet.value();
        if (Config::ENABLE_GPU_VALIDATION && systemInfo.is_extension_available(VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
            builder.enable_extension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
            s_hasDebugMarkerSupport = true;
        } else {
            s_hasDebugMarkerSupport = false;
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            if (systemInfo.validation_layers_available) {
                builder.enable_validation_layers();
            }
        }

        auto instanceRet = builder.build();
        if (!instanceRet) {
            Console::errorfn(Locale::Get(_ID("ERROR_VK_INIT")), instanceRet.error().message().c_str());
            return ErrorCode::VK_OLD_HARDWARE;
        }

        _vkbInstance = instanceRet.value();

        //store the debug messenger
        _debugMessenger = _vkbInstance.debug_messenger;

        // get the surface of the window we opened with SDL
        SDL_Vulkan_CreateSurface(window.getRawWindow(), _vkbInstance.instance, &_surface);
        if (_surface == nullptr) {
            return ErrorCode::VK_SURFACE_CREATE;
        }

        _device = eastl::make_unique<VKDevice>(*this, _vkbInstance, _surface);
        if (_device->getVKDevice() == nullptr) {
            return ErrorCode::VK_DEVICE_CREATE_FAILED;
        }
        VKUtil::fillEnumTables(_device->getVKDevice());

        if (_device->getQueueIndex(vkb::QueueType::graphics) == INVALID_VK_QUEUE_INDEX) {
            return ErrorCode::VK_NO_GRAHPICS_QUEUE;
        }

        _swapChain = eastl::make_unique<VKSwapChain>(*this, *_device, window);
        VK_API::s_stateTracker->_device = _device.get();

        _cmdContext = eastl::make_unique<VKImmediateCmdContext>(*_device);
        VK_API::s_stateTracker->_cmdContext = _cmdContext.get();

        recreateSwapChain(window);

        DIVIDE_ASSERT(Config::MINIMUM_VULKAN_MINOR_VERSION > 2);

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = _device->getPhysicalDevice();
        allocatorInfo.device = _device->getDevice();
        allocatorInfo.instance = _vkbInstance.instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        
        vmaCreateAllocator(&allocatorInfo, &_allocator);
        GetStateTracker()->_allocatorInstance._allocator = &_allocator;

        _commandBuffers.resize(VKSwapChain::MAX_FRAMES_IN_FLIGHT);

        //allocate the default command buffer that we will use for rendering
        const VkCommandBufferAllocateInfo cmdAllocInfo = vk::commandBufferAllocateInfo(_device->graphicsCommandPool(), VK_COMMAND_BUFFER_LEVEL_PRIMARY, VKSwapChain::MAX_FRAMES_IN_FLIGHT);
        VK_CHECK(vkAllocateCommandBuffers(_device->getVKDevice(), &cmdAllocInfo, _commandBuffers.data()));

        return ErrorCode::NO_ERR;
    }

    void VK_API::recreateSwapChain(const DisplayWindow& window) {
        while (window.minimized()) {
            idle(false);
            SDLEventManager::pollEvents();
        }

        vkDeviceWaitIdle(_device->getVKDevice());
        const ErrorCode err = _swapChain->create(BitCompare(window.flags(), WindowFlags::VSYNC),
                                                 _context.context().config().runtime.adaptiveSync,
                                                 _surface);

        DIVIDE_ASSERT(err == ErrorCode::NO_ERR);

        const auto& windowDimensions = window.getDrawableSize();
        _windowExtents = VkExtent2D{ windowDimensions.width, windowDimensions.height };

        initPipelines();
    }

    void VK_API::initPipelines() {

        s_deviceDeleteQueue.flush(_device->getVKDevice());

        VkShaderModule triangleFragShader;
        if (!loadShaderModule((Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::g_SPIRVShaderLoc + "triangle.frag.spv").c_str(), &triangleFragShader)) {
            Console::errorfn("Error when building the triangle fragment shader module");
        } else {
            Console::printfn("Triangle fragment shader successfully loaded");
        }

        VkShaderModule triangleVertexShader;
        if (!loadShaderModule((Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::g_SPIRVShaderLoc + "triangle.vert.spv").c_str(), &triangleVertexShader)) {
            Console::errorfn("Error when building the triangle vertex shader module");
        } else {
            Console::printfn("Triangle vertex shader successfully loaded");
        }

        //build the pipeline layout that controls the inputs/outputs of the shader
        //we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
        const VkPipelineLayoutCreateInfo pipeline_layout_info = vk::pipelineLayoutCreateInfo(0u);
        VK_CHECK(vkCreatePipelineLayout(_device->getVKDevice(), &pipeline_layout_info, nullptr, &_trianglePipeline._layout));

        //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
        PipelineBuilder pipelineBuilder;

        pipelineBuilder._shaderStages.push_back(vk::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));

        pipelineBuilder._shaderStages.push_back(vk::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

        //vertex input controls how to read vertices from vertex buffers. We aren't using it yet
        pipelineBuilder._vertexInputInfo = vk::pipelineVertexInputStateCreateInfo();

        //input assembly is the configuration for drawing triangle lists, strips, or individual points.
        //we are just going to draw triangle list
        pipelineBuilder._inputAssembly = vk::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0u, VK_FALSE);

        //build viewport and scissor from the swapchain extents
        pipelineBuilder._viewport.x = 0.0f;
        pipelineBuilder._viewport.y = 0.0f;
        pipelineBuilder._viewport.width = (float)_windowExtents.width;
        pipelineBuilder._viewport.height = (float)_windowExtents.height;
        pipelineBuilder._viewport.minDepth = 0.0f;
        pipelineBuilder._viewport.maxDepth = 1.0f;

        pipelineBuilder._scissor.offset = { 0, 0 };
        pipelineBuilder._scissor.extent = _windowExtents;

        //configure the rasterizer to draw filled triangles
        pipelineBuilder._rasterizer = vk::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        //we don't use multisampling, so just run the default one
        pipelineBuilder._multisampling = vk::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
        pipelineBuilder._multisampling.minSampleShading = 1.f;
        pipelineBuilder._depthStencil = vk::pipelineDepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL);
        pipelineBuilder._tessellation = vk::pipelineTessellationStateCreateInfo(1);
        //a single blend attachment with no blending and writing to RGBA
        pipelineBuilder._colorBlendAttachment = vk::pipelineColorBlendAttachmentState(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_FALSE);

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = _trianglePipeline._layout;

        //finally build the pipeline
        _trianglePipeline._pipeline = pipelineBuilder.build_pipeline(_device->getVKDevice(), _swapChain->getRenderPass());

        VK_API::RegisterCustomAPIDelete([pipeline = _trianglePipeline._pipeline, layout = _trianglePipeline._layout](VkDevice device) {
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, layout, nullptr);
        }, false);

        vkDestroyShaderModule(_device->getVKDevice(), triangleVertexShader, nullptr);
        vkDestroyShaderModule(_device->getVKDevice(), triangleFragShader, nullptr);
    }

    void VK_API::closeRenderingAPI() {

        vkShaderProgram::DestroyStaticData();

        if (_device != nullptr) {
            vkDeviceWaitIdle(_device->getVKDevice());
            s_transientDeleteQueue.flush(_device->getVKDevice());
            s_deviceDeleteQueue.flush(_device->getVKDevice());
            vmaDestroyAllocator(_allocator);
            _commandBuffers.clear();
            _cmdContext.reset();
            _swapChain.reset();
            _device.reset();
        }

        s_stateTracker.reset();

        if (_vkbInstance.instance != nullptr) {
            vkDestroySurfaceKHR(_vkbInstance.instance, _surface, nullptr);
        }
        vkb::destroy_instance(_vkbInstance);
        _vkbInstance = {};
    }

    bool VK_API::loadShaderModule(const char* filePath, VkShaderModule* outShaderModule) {
        //open the file. With cursor at the end
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            return false;
        }

        //find what the size of the file is by looking up the location of the cursor
        //because the cursor is at the end, it gives the size directly in bytes
        size_t fileSize = (size_t)file.tellg();

        //spirv expects the buffer to be on uint32, so make sure to reserve an int vector big enough for the entire file
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        //put file cursor at beginning
        file.seekg(0);

        //load the entire file into the buffer
        file.read((char*)buffer.data(), fileSize);

        //now that the file is loaded into the buffer, we can close it
        file.close();


        //create a new shader module, using the buffer we loaded
        //codeSize has to be in bytes, so multiply the ints in the buffer by size of int to know the real size of the buffer
        const VkShaderModuleCreateInfo createInfo = vk::shaderModuleCreateInfo(buffer.size() * sizeof(uint32_t), buffer.data());

        //check that the creation goes well.
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(_device->getVKDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            return false;
        }
        *outShaderModule = shaderModule;
        return true;
    }

    void VK_API::drawText(const TextElementBatch& batch) {
        OPTICK_EVENT();

        BlendingSettings textBlend{};
        textBlend.blendSrc(BlendProperty::SRC_ALPHA);
        textBlend.blendDest(BlendProperty::INV_SRC_ALPHA);
        textBlend.blendOp(BlendOperation::ADD);
        textBlend.blendSrcAlpha(BlendProperty::ONE);
        textBlend.blendDestAlpha(BlendProperty::ZERO);
        textBlend.blendOpAlpha(BlendOperation::COUNT);
        textBlend.enabled(true);

        [[maybe_unused]] const I32 width = _context.renderingResolution().width;
        [[maybe_unused]] const I32 height = _context.renderingResolution().height;

        size_t drawCount = 0;
        size_t previousStyle = 0;

        for (const TextElement& entry : batch.data())
        {
            if (previousStyle != entry.textLabelStyleHash()) {
                previousStyle = entry.textLabelStyleHash();
            }

            const TextElement::TextType& text = entry.text();
            const size_t lineCount = text.size();
            for (size_t i = 0; i < lineCount; ++i) {
            }
            drawCount += lineCount;

            // Register each label rendered as a draw call
            _context.registerDrawCalls(to_U32(drawCount));
        }
    }

    bool VK_API::draw(const GenericDrawCommand& cmd, VkCommandBuffer& cmdBuffer) const {
        OPTICK_EVENT();

        if (cmd._sourceBuffer._id == 0) {
            U32 indexCount = 0u;
            switch (VK_API::GetStateTracker()->_activeTopology) {
                case PrimitiveTopology::COMPUTE:
                case PrimitiveTopology::COUNT     : DIVIDE_UNEXPECTED_CALL();         break;
                case PrimitiveTopology::TRIANGLES : indexCount = cmd._drawCount * 3;  break;
                case PrimitiveTopology::POINTS    : indexCount = cmd._drawCount * 1;  break;
                default                           : indexCount = cmd._cmd.indexCount; break;
            }

            vkCmdDraw(cmdBuffer, indexCount, cmd._cmd.primCount, cmd._cmd.firstIndex, 0u);

        } else {
            // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
            static VertexDataInterface::Handle s_lastID = { U16_MAX, 0u };
            static VertexDataInterface* s_lastBuffer = nullptr;

            if (s_lastID != cmd._sourceBuffer) {
                s_lastID = cmd._sourceBuffer;
                s_lastBuffer = VertexDataInterface::s_VDIPool.find(s_lastID);
            }

            DIVIDE_ASSERT(s_lastBuffer != nullptr);
            vkUserData userData{};
            userData._cmdBuffer = &cmdBuffer;

            s_lastBuffer->draw(cmd, &userData);
        }

        return true;
    }

    void VK_API::bindDynamicState(const RenderStateBlock& currentState, VkCommandBuffer& cmdBuffer) const {
        auto& stateTrackerDS = GetStateTracker()->_dynamicState;
        if (stateTrackerDS.stencilMask() != currentState.stencilMask()) {
            vkCmdSetStencilCompareMask(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState.stencilMask());
            stateTrackerDS.stencilMask(currentState.stencilMask());
        }
        if (stateTrackerDS.stencilWriteMask() != currentState.stencilWriteMask()) {
            vkCmdSetStencilWriteMask(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState.stencilWriteMask());
            stateTrackerDS.stencilWriteMask(currentState.stencilWriteMask());
        }
        if (stateTrackerDS.stencilRef() != currentState.stencilRef()) {
            vkCmdSetStencilReference(cmdBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, currentState.stencilRef());
            stateTrackerDS.stencilRef(currentState.stencilRef());
        }
        if (!COMPARE(stateTrackerDS.zUnits(), currentState.zUnits()) || !COMPARE(stateTrackerDS.zBias(), currentState.zBias())) {
            vkCmdSetDepthBias(cmdBuffer, currentState.zUnits(), 0.f, currentState.zBias());
            stateTrackerDS.zUnits(currentState.zUnits());
            stateTrackerDS.zBias(currentState.zBias());
        }
    }

    ShaderResult VK_API::bindPipeline(const Pipeline& pipeline, VkCommandBuffer& cmdBuffer) const {
        const PipelineDescriptor& pipelineDescriptor = pipeline.descriptor();
        ShaderProgram* program = ShaderProgram::FindShaderProgram(pipelineDescriptor._shaderProgramHandle);
        GetStateTracker()->_perDrawSet = nullptr;

        if (program == nullptr) {
            return ShaderResult::Failed;
        }
        GetStateTracker()->_perDrawSet = &program->descriptorSet();
        GetStateTracker()->_activeTopology = pipelineDescriptor._primitiveTopology;
        size_t stateBlockHash = pipelineDescriptor._stateHash == 0u ? _context.getDefaultStateBlock(false) : pipelineDescriptor._stateHash;
        if (stateBlockHash == 0) {
            stateBlockHash = RenderStateBlock::DefaultHash();
        }
        bool currentStateValid = false;
        const RenderStateBlock& currentState = RenderStateBlock::Get(stateBlockHash, currentStateValid);
        DIVIDE_ASSERT(currentStateValid, "GL_API error: Invalid state blocks detected on activation!");

        /*const vkShaderProgram& vkProgram = static_cast<vkShaderProgram&>(*program);
        const auto& shaderStages = vkProgram.shaderStages();

        //build the pipeline layout that controls the inputs/outputs of the shader
        //we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
        VkPipelineLayout tempPipelineLayout{VK_NULL_HANDLE};
        const VkPipelineLayoutCreateInfo pipeline_layout_info = vk::pipelineLayoutCreateInfo(0u);
        VK_CHECK(vkCreatePipelineLayout(_device->getVKDevice(), &pipeline_layout_info, nullptr, &tempPipelineLayout));

        //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
        PipelineBuilder pipelineBuilder;

        bool isGraphicsPipeline = false;
        for (const auto& stage : shaderStages) {
            pipelineBuilder._shaderStages.push_back(vk::pipelineShaderStageCreateInfo(stage->stageMask(), stage->handle()));
            isGraphicsPipeline = isGraphicsPipeline || stage->stageMask() != VK_SHADER_STAGE_COMPUTE_BIT;
        }

        const VertexInputDescription vertexDescription = getVertexDescription(pipelineDescriptor._vertexFormat);
        //connect the pipeline builder vertex input info to the one we get from Vertex
        pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
        pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = to_U32(vertexDescription.attributes.size());
        pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
        pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = to_U32(vertexDescription.bindings.size());

        //vertex input controls how to read vertices from vertex buffers. We aren't using it yet
        pipelineBuilder._vertexInputInfo = vk::pipelineVertexInputStateCreateInfo();

        //input assembly is the configuration for drawing triangle lists, strips, or individual points.
        //we are just going to draw triangle list
        pipelineBuilder._inputAssembly = vk::pipelineInputAssemblyStateCreateInfo(vkPrimitiveTypeTable[to_base(pipelineDescriptor._primitiveTopology)], 0u, pipelineDescriptor._primitiveRestartEnabled ? VK_TRUE : VK_FALSE);
        //configure the rasterizer to draw filled triangles
        pipelineBuilder._rasterizer = vk::pipelineRasterizationStateCreateInfo(
                                            vkFillModeTable[to_base(currentState.fillMode())],
                                            vkCullModeTable[to_base(currentState.cullMode())],
                                            currentState.frontFaceCCW()
                                                    ? VK_FRONT_FACE_CLOCKWISE
                                                    : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        //we don't use multisampling, so just run the default one
        VkSampleCountFlagBits msaaSampleFlags = VK_SAMPLE_COUNT_1_BIT;
        const U8 msaaSamples = GetStateTracker()->_activeMSAASamples;
        if (msaaSamples > 0u) {
            assert(isPowerOfTwo(msaaSamples));
            msaaSampleFlags = static_cast<VkSampleCountFlagBits>(msaaSamples);
        }

        pipelineBuilder._multisampling = vk::pipelineMultisampleStateCreateInfo(msaaSampleFlags);
        pipelineBuilder._multisampling.minSampleShading = 1.f;
        pipelineBuilder._multisampling.alphaToCoverageEnable = GetStateTracker()->_alphaToCoverage ? VK_TRUE : VK_FALSE;
        VkStencilOpState stencilOpState{};
        stencilOpState.failOp = vkStencilOpTable[to_base(currentState.stencilFailOp())];
        stencilOpState.passOp = vkStencilOpTable[to_base(currentState.stencilPassOp())];
        stencilOpState.depthFailOp = vkStencilOpTable[to_base(currentState.stencilZFailOp())];
        stencilOpState.compareOp = vkCompareFuncTable[to_base(currentState.stencilFunc())];
        
        pipelineBuilder._depthStencil = vk::pipelineDepthStencilStateCreateInfo(currentState.depthTestEnabled(), true, vkCompareFuncTable[to_base(currentState.zFunc())]);
        pipelineBuilder._depthStencil.stencilTestEnable = currentState.stencilEnable();
        pipelineBuilder._depthStencil.front = stencilOpState;
        pipelineBuilder._depthStencil.back = stencilOpState;
        pipelineBuilder._rasterizer.depthBiasEnable = !IS_ZERO(currentState.zBias());

        //a single blend attachment with no blending and writing to RGBA
        const P32 cWrite = currentState.colourWrite();
        pipelineBuilder._colorBlendAttachment = vk::pipelineColorBlendAttachmentState(
            (cWrite.b[0] == 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
            (cWrite.b[1] == 1 ? VK_COLOR_COMPONENT_G_BIT : 0) |
            (cWrite.b[2] == 1 ? VK_COLOR_COMPONENT_B_BIT : 0) |
            (cWrite.b[3] == 1 ? VK_COLOR_COMPONENT_A_BIT : 0),
            VK_FALSE);

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = tempPipelineLayout;
        pipelineBuilder._tessellation = vk::pipelineTessellationStateCreateInfo(currentState.tessControlPoints());

        VkPipeline pipeline = pipelineBuilder.build_pipeline(_device->getVKDevice(), _swapChain->getRenderPass());
        VK_API::RegisterCustomAPIDelete([pipeline, tempPipelineLayout](VkDevice device) {
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, tempPipelineLayout, nullptr);
        }, false);

        vkCmdBindPipeline(cmdBuffer, isGraphicsPipeline ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE, GetStateTracker()->_tempPipelines.back()._pipeline);
        */

        bindDynamicState(currentState, cmdBuffer);
        return ShaderResult::OK;
    }

    void VK_API::flushCommand(GFX::CommandBase* cmd) noexcept {
        OPTICK_EVENT();

        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();
        OPTICK_TAG("Type", to_base(cmd->Type()));

        OPTICK_EVENT(GFX::Names::commandType[to_base(cmd->Type())]);
        if (GFXDevice::IsSubmitCommand(cmd->Type()) && !s_transferQueue._requests.empty()) {
            UniqueLock<Mutex> w_lock(s_transferQueue._lock);
            // Check again
            if (!s_transferQueue._requests.empty()) {
                VK_API::GetStateTracker()->_cmdContext->flushCommandBuffer([&cmdBuffer](VkCommandBuffer cmd) {
                    while (!s_transferQueue._requests.empty()) {
                        auto& request = s_transferQueue._requests.front();
                        if (request.srcBuffer != VK_NULL_HANDLE) {
                            VkBufferCopy copy;
                            copy.dstOffset = request.dstOffset;
                            copy.srcOffset = request.srcOffset;
                            copy.size = request.size;
                            vkCmdCopyBuffer(cmd, request.srcBuffer, request.dstBuffer, 1, &copy);
                        }

                        VkBufferMemoryBarrier2 memoryBarrier = vk::bufferMemoryBarrier2();
                        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                        memoryBarrier.dstStageMask = request.dstStageMask;
                        memoryBarrier.dstAccessMask = request.dstAccessMask;
                        memoryBarrier.offset = request.dstOffset;
                        memoryBarrier.size = request.size;
                        memoryBarrier.buffer = request.dstBuffer;

                        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                        dependencyInfo.bufferMemoryBarrierCount = 1;
                        dependencyInfo.pBufferMemoryBarriers = &memoryBarrier;

                        //ToDo: batch these up! -Ionut
                        vkCmdPipelineBarrier2(request.srcBuffer != VK_NULL_HANDLE ? cmd : cmdBuffer, &dependencyInfo);

                        s_transferQueue._requests.pop_front();
                    }
                });
            }
        }

        switch (cmd->Type()) {
            case GFX::CommandType::BEGIN_RENDER_PASS: {
                const GFX::BeginRenderPassCommand* crtCmd = cmd->As<GFX::BeginRenderPassCommand>();
                PushDebugMessage(cmdBuffer, crtCmd->_name.c_str());
                vkRenderTarget* rt = static_cast<vkRenderTarget*>(_context.renderTargetPool().getRenderTarget(crtCmd->_target));
                GetStateTracker()->_alphaToCoverage = crtCmd->_descriptor._alphaToCoverage;
                GetStateTracker()->_activeMSAASamples = rt->getSampleCount();
            }break;
            case GFX::CommandType::END_RENDER_PASS: {
                PopDebugMessage(cmdBuffer);
                GetStateTracker()->_alphaToCoverage = false;
                GetStateTracker()->_activeMSAASamples = _context.context().config().rendering.MSAASamples;
            }break;
            case GFX::CommandType::BEGIN_RENDER_SUB_PASS: {
            }break;
            case GFX::CommandType::END_RENDER_SUB_PASS: {
            }break;
            case GFX::CommandType::BEGIN_GPU_QUERY: {
            }break;
            case GFX::CommandType::END_GPU_QUERY: {
            }break;
            case GFX::CommandType::COPY_TEXTURE: {
            }break;
            case GFX::CommandType::BIND_SHADER_RESOURCES: {
            }break;
            case GFX::CommandType::BIND_PIPELINE: {
                const Pipeline* pipeline = cmd->As<GFX::BindPipelineCommand>()->_pipeline;
                assert(pipeline != nullptr);
                if (bindPipeline(*pipeline, cmdBuffer) == ShaderResult::Failed) {
                    Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INVALID_BIND")), pipeline->descriptor()._shaderProgramHandle);
                }
            } break;
            case GFX::CommandType::SEND_PUSH_CONSTANTS: {
            } break;
            case GFX::CommandType::SET_SCISSOR: {
                if (!setScissor(cmd->As<GFX::SetScissorCommand>()->_rect)) {
                    NOP();
                }
            }break;
            case GFX::CommandType::BEGIN_DEBUG_SCOPE: {
                 const GFX::BeginDebugScopeCommand* crtCmd = cmd->As<GFX::BeginDebugScopeCommand>();
                 PushDebugMessage(cmdBuffer, crtCmd->_scopeName.c_str());
            } break;
            case GFX::CommandType::END_DEBUG_SCOPE: {
                 PopDebugMessage(cmdBuffer);
            } break;
            case GFX::CommandType::ADD_DEBUG_MESSAGE: {
                const GFX::AddDebugMessageCommand* crtCmd = cmd->As<GFX::AddDebugMessageCommand>();
                InsertDebugMessage(cmdBuffer, crtCmd->_msg.c_str());
            }break;
            case GFX::CommandType::COMPUTE_MIPMAPS: {
                const GFX::ComputeMipMapsCommand* crtCmd = cmd->As<GFX::ComputeMipMapsCommand>();

                if (crtCmd->_layerRange.x == 0 && crtCmd->_layerRange.y == crtCmd->_texture->descriptor().layerCount()) {
                    OPTICK_EVENT("VK: In-place computation - Full");
                } else {
                    OPTICK_EVENT("VK: View - based computation");
                }
            }break;
            case GFX::CommandType::DRAW_TEXT: {
                const GFX::DrawTextCommand* crtCmd = cmd->As<GFX::DrawTextCommand>();
                drawText(crtCmd->_batch);
            }break;
            case GFX::CommandType::DRAW_COMMANDS : {
                const GFX::DrawCommand::CommandContainer& drawCommands = cmd->As<GFX::DrawCommand>()->_drawCommands;

                U32 drawCount = 0u;
                for (const GenericDrawCommand& currentDrawCommand : drawCommands) {
                    if (draw(currentDrawCommand, cmdBuffer)) {
                        drawCount += isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME) 
                                           ? 2 
                                           : isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY) ? 1 : 0;
                    }
                }
                _context.registerDrawCalls(drawCount);
            }break;
            case GFX::CommandType::DISPATCH_COMPUTE: {
            }break;
            case GFX::CommandType::SET_CLIPING_STATE: {
            } break;
            case GFX::CommandType::MEMORY_BARRIER: {
            } break;
            default: break;
        }
    }

    void VK_API::preFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void VK_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) noexcept {
        s_transientDeleteQueue.flush(_device->getDevice());
    }

    vec2<U16> VK_API::getDrawableSize(const DisplayWindow& window) const noexcept {
        int w = 1, h = 1;
        SDL_Vulkan_GetDrawableSize(window.getRawWindow(), &w, &h);
        return vec2<U16>(w, h);
    }

    U32 VK_API::getHandleFromCEGUITexture([[maybe_unused]] const CEGUI::Texture& textureIn) const noexcept {
        return 0u;
    }

    bool VK_API::setViewport(const Rect<I32>& newViewport) noexcept {
        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();
        const VkViewport targetViewport{to_F32(newViewport.offsetX),
                                        to_F32(newViewport.sizeY) - to_F32(newViewport.offsetY),
                                        to_F32(newViewport.sizeX),
                                        -to_F32(newViewport.sizeY),
                                        0.f,
                                        1.f};
        if (GetStateTracker()->_dynamicState._activeViewport != targetViewport) {
            vkCmdSetViewport(cmdBuffer, 0, 1, &targetViewport);
            GetStateTracker()->_dynamicState._activeViewport = targetViewport;
            return true;
        } 

        return false;
    }

    bool VK_API::setScissor(const Rect<I32>& newScissor) noexcept {
        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();

        const VkOffset2D offset{ std::max(0, newScissor.offsetX), std::max(0, newScissor.offsetY) };
        const VkExtent2D extent{ to_U32(newScissor.sizeX),to_U32(newScissor.sizeY) };
        const VkRect2D targetScissor{ offset, extent };
        if (GetStateTracker()->_dynamicState._activeScissor != targetScissor) {
            vkCmdSetScissor(cmdBuffer, 0, 1, &targetScissor);
            return true;
        }

        return false;
    }

    void VK_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) noexcept {
    }

    void VK_API::createSetLayout(const DescriptorSetUsage usage, const DescriptorSet& set) {
        constexpr U8 s_maxBindings = 32u;
        if (set.empty()) {
            return;
        }

        U8 index = 0u;
        std::array<VkDescriptorSetLayoutBinding, s_maxBindings> tempBindings{};

        for (const auto& binding : set) {
            VkDescriptorSetLayoutBinding& tempBinding = tempBindings[index++];
            assert(index < s_maxBindings);

            if (binding._shaderStageVisibility == to_base(ShaderStageVisibility::NONE)) {
                continue;
            } else if (binding._shaderStageVisibility == to_base(ShaderStageVisibility::ALL)) {
                tempBinding.stageFlags = VK_SHADER_STAGE_ALL;
            } else if (binding._shaderStageVisibility == to_base(ShaderStageVisibility::ALL_DRAW)) {
                tempBinding.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            } else if (binding._shaderStageVisibility == to_base(ShaderStageVisibility::COMPUTE)) {
                tempBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            } else {
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::VERTEX)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
                }
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::GEOMETRY)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
                }
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::TESS_CONTROL)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                }
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::TESS_EVAL)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                }
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::FRAGMENT)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
                if (BitCompare(binding._shaderStageVisibility, ShaderStageVisibility::COMPUTE)) {
                    tempBinding.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
                }
            }
            tempBinding.binding = binding._resource._slot;
            tempBinding.descriptorCount = 1u;
            tempBinding.descriptorType = VKUtil::vkDescriptorType(binding._type);
        }

        VkDescriptorSetLayoutCreateInfo setinfo = {};
        setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setinfo.pNext = nullptr;

        setinfo.bindingCount = index;
        setinfo.pBindings = tempBindings.data();
        //no flags
        setinfo.flags = 0;

        VkDescriptorSetLayout setLayout;
        vkCreateDescriptorSetLayout(_device->getVKDevice(), &setinfo, nullptr, &setLayout);

        // other code ....

        // add descriptor set layout to deletion queues
        RegisterCustomAPIDelete([setLayout](VkDevice device) {
            vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        }, false);
    }

    const VKStateTracker_uptr& VK_API::GetStateTracker() noexcept {
        DIVIDE_ASSERT(s_stateTracker != nullptr);

        return s_stateTracker;
    }

    void VK_API::InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message, [[maybe_unused]] const U32 id) {
        if (s_hasDebugMarkerSupport) {
            static F32 color[4] = { 0.0f, 1.0f, 0.0f, 1.f };

            VkDebugMarkerMarkerInfoEXT markerInfo = {};
            markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            memcpy(markerInfo.color, &color[0], sizeof(F32) * 4);
            markerInfo.pMarkerName = message;
            Debug::vkCmdDebugMarkerInsert(cmdBuffer, &markerInfo);
        }
    }

    void VK_API::PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message, const U32 id) {
        if (s_hasDebugMarkerSupport) {
            static F32 color[4] = {0.5f, 0.5f, 0.5f, 1.f};

            VkDebugMarkerMarkerInfoEXT markerInfo = {};
            markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            memcpy(markerInfo.color, &color[0], sizeof(F32) * 4);
            markerInfo.pMarkerName = "Set primary viewport";
            Debug::vkCmdDebugMarkerBegin(cmdBuffer, &markerInfo);
        }
        
        assert(GetStateTracker()->_debugScopeDepth < GetStateTracker()->_debugScope.size());
        GetStateTracker()->_debugScope[GetStateTracker()->_debugScopeDepth++] = { message, id };
    }

    void VK_API::PopDebugMessage(VkCommandBuffer cmdBuffer) {
        if (s_hasDebugMarkerSupport) {
            Debug::vkCmdDebugMarkerEnd(cmdBuffer);
        }

        GetStateTracker()->_debugScope[GetStateTracker()->_debugScopeDepth--] = { "", std::numeric_limits<U32>::max() };
    }

}; //namespace Divide