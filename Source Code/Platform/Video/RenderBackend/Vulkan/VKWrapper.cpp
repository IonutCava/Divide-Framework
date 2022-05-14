#include "stdafx.h"

#include "Headers/VKWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Buffers/Headers/vkFramebuffer.h"
#include "Buffers/Headers/vkShaderBuffer.h"
#include "Buffers/Headers/vkGenericVertexData.h"

#include "Shaders/Headers/vkShaderProgram.h"

#include "Textures/Headers/vkTexture.h"

#include <sdl/include/SDL_vulkan.h>

#define VMA_HEAVY_ASSERT(expr) DIVIDE_ASSERT(expr)

#define VMA_DEBUG_LOG(format, ...) do { \
        Console::printfn(format, __VA_ARGS__); \
    } while(false)


#ifdef _MSVC_LANG

#pragma warning(push, 4)
#pragma warning(disable: 4127) // conditional expression is constant
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4189) // local variable is initialized but not referenced
#pragma warning(disable: 4324) // structure was padded due to alignment specifier

#endif  // #ifdef _MSVC_LANG

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare" // comparison of unsigned expression < 0 is always false
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef _MSVC_LANG
#pragma warning(pop)
#endif

// ref (mostly everything): https://vkguide.dev/
namespace vkInit{
    VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule) {

        VkPipelineShaderStageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.pNext = nullptr;

        //shader stage
        info.stage = stage;
        //module containing the code for this shader stage
        info.module = shaderModule;
        //the entry point of the shader
        info.pName = "main";
        return info;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info() {
        VkPipelineVertexInputStateCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        info.pNext = nullptr;

        //no vertex bindings or attributes
        info.vertexBindingDescriptionCount = 0;
        info.vertexAttributeDescriptionCount = 0;
        return info;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info(VkPrimitiveTopology topology) {
        VkPipelineInputAssemblyStateCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        info.pNext = nullptr;

        info.topology = topology;
        //we are not going to use primitive restart on the entire tutorial so leave it on false
        info.primitiveRestartEnable = VK_FALSE;
        return info;
    }

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info(VkPolygonMode polygonMode)
    {
        VkPipelineRasterizationStateCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        info.pNext = nullptr;

        info.depthClampEnable = VK_FALSE;
        //discards all primitives before the rasterization stage if enabled which we don't want
        info.rasterizerDiscardEnable = VK_FALSE;

        info.polygonMode = polygonMode;
        info.lineWidth = 1.0f;
        //no backface cull
        info.cullMode = VK_CULL_MODE_NONE;
        info.frontFace = VK_FRONT_FACE_CLOCKWISE;
        //no depth bias
        info.depthBiasEnable = VK_FALSE;
        info.depthBiasConstantFactor = 0.0f;
        info.depthBiasClamp = 0.0f;
        info.depthBiasSlopeFactor = 0.0f;

        return info;
    }

    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info()
    {
        VkPipelineMultisampleStateCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        info.pNext = nullptr;

        info.sampleShadingEnable = VK_FALSE;
        //multisampling defaulted to no multisampling (1 sample per pixel)
        info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        info.minSampleShading = 1.0f;
        info.pSampleMask = nullptr;
        info.alphaToCoverageEnable = VK_FALSE;
        info.alphaToOneEnable = VK_FALSE;
        return info;
    }

    VkPipelineColorBlendAttachmentState color_blend_attachment_state() {
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        return colorBlendAttachment;
    }

    VkPipelineLayoutCreateInfo pipeline_layout_create_info() {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.pNext = nullptr;

        //empty defaults
        info.flags = 0;
        info.setLayoutCount = 0;
        info.pSetLayouts = nullptr;
        info.pushConstantRangeCount = 0;
        info.pPushConstantRanges = nullptr;
        return info;
    }

}; //namespace vkInit

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
        Divide::DIVIDE_UNEXPECTED_CALL();
        return VK_FALSE; // Applications must return false here
    }
}

namespace Divide {
    VK_API::IMPrimitivePool VK_API::s_IMPrimitivePool{};
    bool VK_API::s_hasDebugMarkerSupport = false;
    eastl::unique_ptr<VKStateTracker> VK_API::s_stateTracker = nullptr;

    VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
        //make viewport state from our stored viewport and scissor.
        //at the moment we won't support multiple viewports or scissors
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.pNext = nullptr;

        viewportState.viewportCount = 1;
        viewportState.pViewports = &_viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &_scissor;

        //setup dummy color blending. We aren't using transparent objects yet
        //the blending is just "no blend", but we do write to the color attachment
        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.pNext = nullptr;

        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &_colorBlendAttachment;

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            //VK_DYNAMIC_STATE_DEPTH_BIAS,
            //VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            //VK_DYNAMIC_STATE_CULL_MODE,
            //VK_DYNAMIC_STATE_FRONT_FACE,
            //VK_DYNAMIC_STATE_LINE_WIDTH
        };
        constexpr U32 stateCount = to_U32(std::size(dynamicStates));
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = stateCount;
        dynamicState.pDynamicStates = dynamicStates;

        //build the actual pipeline
        //we now use all of the info structs we have been writing into into this one to create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = nullptr;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.stageCount = to_U32(_shaderStages.size());
        pipelineInfo.pStages = _shaderStages.data();
        pipelineInfo.pVertexInputState = &_vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &_inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &_rasterizer;
        pipelineInfo.pMultisampleState = &_multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = _pipelineLayout;
        pipelineInfo.renderPass = pass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        //it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
        VkPipeline newPipeline;
        if (vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
            Console::errorfn("failed to create pipeline");
            return VK_NULL_HANDLE; // failed to create graphics pipeline
        }
        else
        {
            return newPipeline;
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
    }

    bool VK_API::beginFrame(DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        const auto& windowDimensions = window.getDrawableSize();
        const VkExtent2D windowExtents{ windowDimensions.width, windowDimensions.height };

        if (_windowExtents.width != windowExtents.width || _windowExtents.height != windowExtents.height) {
            recreateSwapChain(window);
        }

        const VkResult result = _swapChain->beginFrame();

        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                recreateSwapChain(window);
                _skipEndFrame = true;
                return false;
            } else {
                Console::errorfn("Detected Vulkan error: %s\n", VKErrorString(result).c_str());
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        //naming it cmd for shorter writing
        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();

        //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
        VkCommandBufferBeginInfo cmdBeginInfo = {};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.pNext = nullptr;
        cmdBeginInfo.pInheritanceInfo = nullptr;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo));

        //make a clear-color from frame number. This will flash with a 120*pi frame period.
        VkClearValue clearValue{};
        clearValue.color = { { 0.0f, 0.0f, abs(sin(GFXDevice::FrameCount() / 120.f)), 1.0f } };

        //start the main renderpass.
        //We will use the clear color from above, and the framebuffer of the index the swapchain gave us
        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.pNext = nullptr;

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

        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
        // Set dynamic state to default
        if (setViewport({ 0, 0, windowDimensions.width, windowDimensions.height })) {
            const VkOffset2D offset{ 0, 0 };
            const VkExtent2D extent{ to_U32(windowDimensions.width),to_U32(windowDimensions.height) };
            const VkRect2D scissor{ offset, extent };
            vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

            vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
        }

        return true;
    }

    void VK_API::endFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        if (_skipEndFrame) {
            _skipEndFrame = false;
            return;
        }

        //naming it cmd for shorter writing
        VkCommandBuffer cmd = getCurrentCommandBuffer();

        //finalize the render pass
        vkCmdEndRenderPass(cmd);
        //finalize the command buffer (we can no longer add commands, but it can now be executed)
        VK_CHECK(vkEndCommandBuffer(cmd));

        const VkResult result = _swapChain->endFrame(_graphicsQueue._queue, cmd);
        
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
               .require_api_version(1, Config::MINIMUM_VULKAN_MINOR_VERSION, 0)
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

        _device = eastl::make_unique<VKDevice>(_vkbInstance, _surface);
        if (_device->getVKDevice() == nullptr) {
            return ErrorCode::VK_DEVICE_CREATE_FAILED;
        }
        VKUtil::fillEnumTables(_device->getVKDevice());

        _graphicsQueue = _device->getQueue(vkb::QueueType::graphics);
        if (_graphicsQueue._queue == nullptr) {
            return ErrorCode::VK_NO_GRAHPICS_QUEUE;
        }

        _computeQueue = _device->getQueue(vkb::QueueType::compute);
        _transferQueue = _device->getQueue(vkb::QueueType::transfer);
        _swapChain = eastl::make_unique<VKSwapChain>(*_device, window);

        recreateSwapChain(window);

        //create a command pool for commands submitted to the graphics queue.
        //we also want the pool to allow for resetting of individual command buffers
        VkCommandPoolCreateInfo commandPoolInfo = {};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VK_CHECK(vkCreateCommandPool(_device->getVKDevice(), &commandPoolInfo, nullptr, &_commandPool));

        _commandBuffers.resize(VKSwapChain::MAX_FRAMES_IN_FLIGHT);

        //allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = _commandPool;
        cmdAllocInfo.commandBufferCount = VKSwapChain::MAX_FRAMES_IN_FLIGHT;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(_device->getVKDevice(), &cmdAllocInfo, _commandBuffers.data()));

        return ErrorCode::NO_ERR;
    }

    void VK_API::recreateSwapChain(const DisplayWindow& window) {
        while (window.minimized()) {
            idle(false);
            SDLEventManager::pollEvents();
        }

        const ErrorCode err = _swapChain->create(BitCompare(window.flags(), WindowFlags::VSYNC),
                                                 _context.context().config().runtime.adaptiveSync,
                                                 _surface);

        DIVIDE_ASSERT(err == ErrorCode::NO_ERR);

        const auto& windowDimensions = window.getDrawableSize();
        _windowExtents = VkExtent2D{ windowDimensions.width, windowDimensions.height };

        initPipelines();
    }

    void VK_API::initPipelines() {

        destroyPipelines();

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
        VkPipelineLayoutCreateInfo pipeline_layout_info = vkInit::pipeline_layout_create_info();

        VK_CHECK(vkCreatePipelineLayout(_device->getVKDevice(), &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

        //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
        PipelineBuilder pipelineBuilder;

        pipelineBuilder._shaderStages.push_back(vkInit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));

        pipelineBuilder._shaderStages.push_back(vkInit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

        //vertex input controls how to read vertices from vertex buffers. We aren't using it yet
        pipelineBuilder._vertexInputInfo = vkInit::vertex_input_state_create_info();

        //input assembly is the configuration for drawing triangle lists, strips, or individual points.
        //we are just going to draw triangle list
        pipelineBuilder._inputAssembly = vkInit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

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
        pipelineBuilder._rasterizer = vkInit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

        //we don't use multisampling, so just run the default one
        pipelineBuilder._multisampling = vkInit::multisampling_state_create_info();

        //a single blend attachment with no blending and writing to RGBA
        pipelineBuilder._colorBlendAttachment = vkInit::color_blend_attachment_state();

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

        //finally build the pipeline
        _trianglePipeline = pipelineBuilder.build_pipeline(_device->getVKDevice(), _swapChain->getRenderPass());

        vkDestroyShaderModule(_device->getVKDevice(), triangleVertexShader, nullptr);
        vkDestroyShaderModule(_device->getVKDevice(), triangleFragShader, nullptr);
    }

    void VK_API::destroyPipelines() {
        if (_trianglePipeline != VK_NULL_HANDLE) {
            _device->waitIdle();
            vkDestroyPipeline(_device->getVKDevice(), _trianglePipeline, nullptr);
            vkDestroyPipelineLayout(_device->getVKDevice(), _trianglePipelineLayout, nullptr);
        }
        _trianglePipeline = VK_NULL_HANDLE;
        _trianglePipelineLayout = VK_NULL_HANDLE;
    }

    void VK_API::closeRenderingAPI() {
        s_stateTracker.reset();

        if (_device != nullptr) {
            destroyPipelines();
            if (_commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(_device->getVKDevice(), _commandPool, nullptr);
            }
            _commandBuffers.clear();
            _swapChain.reset();
            _device.reset();
        }
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
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.pNext = nullptr;

        //codeSize has to be in bytes, so multiply the ints in the buffer by size of int to know the real size of the buffer
        createInfo.codeSize = buffer.size() * sizeof(uint32_t);
        createInfo.pCode = buffer.data();

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

    void VK_API::drawIMGUI(const ImDrawData* data, I64 windowGUID) {
        static I32 s_maxCommandCount = 8u;

        OPTICK_EVENT();

        assert(data != nullptr);
        if (data->Valid) {
            s_maxCommandCount = std::max(s_maxCommandCount, data->CmdListsCount);

            GenericVertexData::IndexBuffer idxBuffer;
            idxBuffer.smallIndices = sizeof(ImDrawIdx) == sizeof(U16);
            idxBuffer.dynamic = true;

            GenericDrawCommand cmd = {};

            GenericVertexData* buffer = _context.getOrCreateIMGUIBuffer(windowGUID, s_maxCommandCount, 1 << 16);
            assert(buffer != nullptr);
            for (I32 n = 0; n < data->CmdListsCount; ++n) {

                const ImDrawList* cmd_list = data->CmdLists[n];

                idxBuffer.count = to_U32(cmd_list->IdxBuffer.Size);
                idxBuffer.data = cmd_list->IdxBuffer.Data;

                buffer->incQueue();
                buffer->updateBuffer(0u, 0u, to_U32(cmd_list->VtxBuffer.size()), cmd_list->VtxBuffer.Data);
                buffer->setIndexBuffer(idxBuffer);

                for (const ImDrawCmd& pcmd : cmd_list->CmdBuffer) {

                    if (pcmd.UserCallback) {
                        // User callback (registered via ImDrawList::AddCallback)
                        pcmd.UserCallback(cmd_list, &pcmd);
                    } else {
                        Rect<I32> clip_rect = {
                            pcmd.ClipRect.x - data->DisplayPos.x,
                            pcmd.ClipRect.y - data->DisplayPos.y,
                            pcmd.ClipRect.z - data->DisplayPos.x,
                            pcmd.ClipRect.w - data->DisplayPos.y
                        };

                        /*const Rect<I32>& viewport = stateTracker->_activeViewport;
                        if (clip_rect.x < viewport.z &&
                            clip_rect.y < viewport.w &&
                            clip_rect.z >= 0 &&
                            clip_rect.w >= 0)
                        {
                            const I32 tempW = clip_rect.w;
                            clip_rect.z -= clip_rect.x;
                            clip_rect.w -= clip_rect.y;
                            clip_rect.y  = viewport.w - tempW;

                            stateTracker->setScissor(clip_rect);
                            if (stateTracker->bindTexture(to_U8(TextureUsage::UNIT0),
                                                          TextureType::TEXTURE_2D,
                                                          static_cast<GLuint>(reinterpret_cast<intptr_t>(pcmd.TextureId))) == GLStateTracker::BindResult::FAILED) {
                                DIVIDE_UNEXPECTED_CALL();
                            }

                            cmd._cmd.indexCount = pcmd.ElemCount;
                            cmd._cmd.firstIndex = pcmd.IdxOffset;
                            buffer->draw(cmd);
                         }*/
                    }
                }
            }
        }
    }

    bool VK_API::draw(const GenericDrawCommand& cmd) const {
        OPTICK_EVENT();

        if (cmd._sourceBuffer._id == 0) {

        } else {
            // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
            static VertexDataInterface::Handle s_lastID = { U16_MAX, 0u };
            static VertexDataInterface* s_lastBuffer = nullptr;

            if (s_lastID != cmd._sourceBuffer) {
                s_lastID = cmd._sourceBuffer;
                s_lastBuffer = VertexDataInterface::s_VDIPool.find(s_lastID);
            }

            DIVIDE_ASSERT(s_lastBuffer != nullptr);
            s_lastBuffer->draw(cmd);
        }

        return true;
    }

    const PerformanceMetrics& VK_API::getPerformanceMetrics() const noexcept {
        static PerformanceMetrics perf;
        return perf;
    }

    void VK_API::flushCommand(GFX::CommandBase* cmd) noexcept {
        OPTICK_EVENT();

        VkCommandBuffer cmdBuffer = getCurrentCommandBuffer();
        OPTICK_TAG("Type", to_base(cmd->Type()));


        switch (cmd->Type()) {
            case GFX::CommandType::BEGIN_RENDER_PASS: {
                OPTICK_EVENT("BEGIN_RENDER_PASS");

                const GFX::BeginRenderPassCommand* crtCmd = cmd->As<GFX::BeginRenderPassCommand>();
                PushDebugMessage(cmdBuffer, crtCmd->_name.c_str());
            }break;
            case GFX::CommandType::END_RENDER_PASS: {
                OPTICK_EVENT("END_RENDER_PASS");
                PopDebugMessage(cmdBuffer);
            }break;
            case GFX::CommandType::BEGIN_RENDER_SUB_PASS: {
                OPTICK_EVENT("BEGIN_RENDER_SUB_PASS");
            }break;
            case GFX::CommandType::END_RENDER_SUB_PASS: {
                OPTICK_EVENT("END_RENDER_SUB_PASS");
            }break;
            case GFX::CommandType::COPY_TEXTURE: {
                OPTICK_EVENT("COPY_TEXTURE");
            }break;
            case GFX::CommandType::BIND_DESCRIPTOR_SETS: {
                OPTICK_EVENT("BIND_DESCRIPTOR_SETS");
            }break;
            case GFX::CommandType::BIND_PIPELINE: {
                OPTICK_EVENT("BIND_PIPELINE");
            } break;
            case GFX::CommandType::SEND_PUSH_CONSTANTS: {
                OPTICK_EVENT("SEND_PUSH_CONSTANTS");
            } break;
            case GFX::CommandType::SET_SCISSOR: {
                OPTICK_EVENT("SET_SCISSOR");

                const Rect<I32>& rect = cmd->As<GFX::SetScissorCommand>()->_rect;

                const VkOffset2D offset{ rect.offsetX, rect.offsetY };
                const VkExtent2D extent{ to_U32(rect.sizeX),to_U32(rect.sizeY) };
                const VkRect2D scissor{offset, extent};
                vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

            }break;
            case GFX::CommandType::SET_TEXTURE_RESIDENCY: {
                OPTICK_EVENT("SET_TEXTURE_RESIDENCY");
            }break;
            case GFX::CommandType::BEGIN_DEBUG_SCOPE: {
                OPTICK_EVENT("BEGIN_DEBUG_SCOPE");
                 const GFX::BeginDebugScopeCommand* crtCmd = cmd->As<GFX::BeginDebugScopeCommand>();
                 PushDebugMessage(cmdBuffer, crtCmd->_scopeName.c_str());
            } break;
            case GFX::CommandType::END_DEBUG_SCOPE: {
                OPTICK_EVENT("END_DEBUG_SCOPE");

                 PopDebugMessage(cmdBuffer);
            } break;
            case GFX::CommandType::ADD_DEBUG_MESSAGE: {
                OPTICK_EVENT("ADD_DEBUG_MESSAGE");
                const GFX::AddDebugMessageCommand* crtCmd = cmd->As<GFX::AddDebugMessageCommand>();
                InsertDebugMessage(cmdBuffer, crtCmd->_msg.c_str());
            }break;
            case GFX::CommandType::COMPUTE_MIPMAPS: {
                OPTICK_EVENT("COMPUTE_MIPMAPS");

                const GFX::ComputeMipMapsCommand* crtCmd = cmd->As<GFX::ComputeMipMapsCommand>();

                if (crtCmd->_layerRange.x == 0 && crtCmd->_layerRange.y == crtCmd->_texture->descriptor().layerCount()) {
                    OPTICK_EVENT("VK: In-place computation - Full");
                } else {
                    OPTICK_EVENT("VK: View - based computation");
                }
            }break;
            case GFX::CommandType::DRAW_TEXT: {
                OPTICK_EVENT("DRAW_TEXT");
                const GFX::DrawTextCommand* crtCmd = cmd->As<GFX::DrawTextCommand>();
                drawText(crtCmd->_batch);
            }break;
            case GFX::CommandType::DRAW_IMGUI: {
                OPTICK_EVENT("DRAW_IMGUI");
                const GFX::DrawIMGUICommand* crtCmd = cmd->As<GFX::DrawIMGUICommand>();
                drawIMGUI(crtCmd->_data, crtCmd->_windowGUID);
            }break;
            case GFX::CommandType::DRAW_COMMANDS : {
                OPTICK_EVENT("DRAW_COMMANDS");
                const GFX::DrawCommand::CommandContainer& drawCommands = cmd->As<GFX::DrawCommand>()->_drawCommands;

                U32 drawCount = 0u;
                for (const GenericDrawCommand& currentDrawCommand : drawCommands) {
                    if (draw(currentDrawCommand)) {
                        drawCount += isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME) 
                                           ? 2 
                                           : isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY) ? 1 : 0;
                    }
                }
                _context.registerDrawCalls(drawCount);
            }break;
            case GFX::CommandType::DISPATCH_COMPUTE: {
                OPTICK_EVENT("DISPATCH_COMPUTE");
            }break;
            case GFX::CommandType::SET_CLIPING_STATE: {
                OPTICK_EVENT("SET_CLIPING_STATE");
            } break;
            case GFX::CommandType::MEMORY_BARRIER: {
                OPTICK_EVENT("MEMORY_BARRIER");
            } break;
            default: break;
        }
    }

    void VK_API::preFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) {
    }

    void VK_API::postFlushCommandBuffer([[maybe_unused]] const GFX::CommandBuffer& commandBuffer) noexcept {
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
        const VkViewport viewport{to_F32(newViewport.offsetX),
                                  to_F32(newViewport.sizeY) - to_F32(newViewport.offsetY),
                                  to_F32(newViewport.sizeX),
                                  -to_F32(newViewport.sizeY),
                                  0.f,
                                  1.f};
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
        return true;
    }

    void VK_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) noexcept {
    }

    VKStateTracker* VK_API::GetStateTracker() noexcept {
        DIVIDE_ASSERT(s_stateTracker != nullptr);

        return s_stateTracker.get();
    }

    void VK_API::InsertDebugMessage(VkCommandBuffer cmdBuffer, const char* message) {
        if (s_hasDebugMarkerSupport) {
            static F32 color[4] = { 0.0f, 1.0f, 0.0f, 1.f };

            VkDebugMarkerMarkerInfoEXT markerInfo = {};
            markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            memcpy(markerInfo.color, &color[0], sizeof(F32) * 4);
            markerInfo.pMarkerName = message;
            Debug::vkCmdDebugMarkerInsert(cmdBuffer, &markerInfo);
        }
    }

    void VK_API::PushDebugMessage(VkCommandBuffer cmdBuffer, const char* message) {
        if (s_hasDebugMarkerSupport) {
            static F32 color[4] = {0.5f, 0.5f, 0.5f, 1.f};

            VkDebugMarkerMarkerInfoEXT markerInfo = {};
            markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            memcpy(markerInfo.color, &color[0], sizeof(F32) * 4);
            markerInfo.pMarkerName = "Set primary viewport";
            Debug::vkCmdDebugMarkerBegin(cmdBuffer, &markerInfo);
        }
        
        assert(GetStateTracker()->_debugScopeDepth < GetStateTracker()->_debugScope.size());
        GetStateTracker()->_debugScope[GetStateTracker()->_debugScopeDepth++] = message;
    }

    void VK_API::PopDebugMessage(VkCommandBuffer cmdBuffer) {
        if (s_hasDebugMarkerSupport) {
            Debug::vkCmdDebugMarkerEnd(cmdBuffer);
        }

        GetStateTracker()->_debugScope[GetStateTracker()->_debugScopeDepth--] = "";
    }

    IMPrimitive* VK_API::NewIMP(Mutex& lock, GFXDevice& parent) {
        ScopedLock<Mutex> w_lock(lock);
        return s_IMPrimitivePool.newElement(parent);
    }

    bool VK_API::DestroyIMP(Mutex& lock, IMPrimitive*& primitive) {
        if (primitive != nullptr) {
            ScopedLock<Mutex> w_lock(lock);
            s_IMPrimitivePool.deleteElement(static_cast<vkIMPrimitive*>(primitive));
            primitive = nullptr;
            return true;
        }

        return false;
    }

}; //namespace Divide