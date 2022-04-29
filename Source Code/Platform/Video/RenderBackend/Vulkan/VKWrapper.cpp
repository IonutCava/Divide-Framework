#include "stdafx.h"

#include "Headers/VKWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include <sdl/include/SDL_vulkan.h>

#include <vk-bootstrap/src/VkBootstrap.h>

#define VMA_HEAVY_ASSERT(expr) DIVIDE_ASSERT(expr)
//#define VMA_DEDICATED_ALLOCATION 0
//#define VMA_DEBUG_MARGIN 16
//#define VMA_DEBUG_DETECT_CORRUPTION 1
//#define VMA_DEBUG_MIN_BUFFER_IMAGE_GRANULARITY 256
//#define VMA_USE_STL_SHARED_MUTEX 0
//#define VMA_MEMORY_BUDGET 0
//#define VMA_STATS_STRING_ENABLED 0
//#define VMA_MAPPING_HYSTERESIS_ENABLED 0

//#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_VULKAN_VERSION 1002000 // Vulkan 1.2
//#define VMA_VULKAN_VERSION 1001000 // Vulkan 1.1
//#define VMA_VULKAN_VERSION 1000000 // Vulkan 1.0


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
    VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0)
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.pNext = nullptr;

        info.queueFamilyIndex = queueFamilyIndex;
        info.flags = flags;
        return info;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.pNext = nullptr;

        info.commandPool = pool;
        info.commandBufferCount = count;
        info.level = level;
        return info;
    }

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
    static bool s_FrameInFlight = false;

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

    //ref:  SaschaWillems / Vulkan / VulkanTools
    std::string errorString(VkResult errorCode)
    {
        switch (errorCode)
        {
#define STR(r) case VK_ ##r: return #r
            STR(NOT_READY);
            STR(TIMEOUT);
            STR(EVENT_SET);
            STR(EVENT_RESET);
            STR(INCOMPLETE);
            STR(ERROR_OUT_OF_HOST_MEMORY);
            STR(ERROR_OUT_OF_DEVICE_MEMORY);
            STR(ERROR_INITIALIZATION_FAILED);
            STR(ERROR_DEVICE_LOST);
            STR(ERROR_MEMORY_MAP_FAILED);
            STR(ERROR_LAYER_NOT_PRESENT);
            STR(ERROR_EXTENSION_NOT_PRESENT);
            STR(ERROR_FEATURE_NOT_PRESENT);
            STR(ERROR_INCOMPATIBLE_DRIVER);
            STR(ERROR_TOO_MANY_OBJECTS);
            STR(ERROR_FORMAT_NOT_SUPPORTED);
            STR(ERROR_SURFACE_LOST_KHR);
            STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
            STR(SUBOPTIMAL_KHR);
            STR(ERROR_OUT_OF_DATE_KHR);
            STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
            STR(ERROR_VALIDATION_FAILED_EXT);
            STR(ERROR_INVALID_SHADER_NV);
#undef STR
        default:
            return "UNKNOWN_ERROR";
        }
    }
}
#define VK_CHECK(x)                                                                     \
    do                                                                                  \
    {                                                                                   \
        VkResult err = x;                                                               \
        if (err)                                                                        \
        {                                                                               \
            Console::errorfn("Detected Vulkan error: %s\n", errorString(err).c_str());  \
            DIVIDE_UNEXPECTED_CALL();                                                   \
        }                                                                               \
    } while (0)

namespace VKUtil {
    void fillEnumTables() {

    };
};
namespace Divide {
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

        //build the actual pipeline
        //we now use all of the info structs we have been writing into into this one to create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = nullptr;

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

    void VK_API::idle([[maybe_unused]] const bool fast) noexcept {
    }

    void VK_API::beginFrame(DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        assert(s_FrameInFlight == false);

        s_FrameInFlight = true;

        const auto& windowDimensions = window.getDrawableSize();
        const VkExtent2D windowExtents{ windowDimensions.width, windowDimensions.height };

        if (_windowExtents.width != windowExtents.width || _windowExtents.height != windowExtents.height) {
            recreateSwapChain(window);
            _skipEndFrame = true;
            return;
        }

        //wait until the GPU has finished rendering the last frame.
        VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, std::numeric_limits<uint64_t>::max()));
        VK_CHECK(vkResetFences(_device, 1, &_renderFence));
        //request image from the swapchain, one second timeout
        const VkResult result = vkAcquireNextImageKHR(_device, _swapchain, std::numeric_limits<uint64_t>::max(), _presentSemaphore, nullptr, &_swapchainImageIndex);
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                recreateSwapChain(window);
                _skipEndFrame = true;
                return;
            } else {
                Console::errorfn("Detected Vulkan error: %s\n", errorString(result).c_str());
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        //now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
        VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

        //naming it cmd for shorter writing
        VkCommandBuffer cmd = _mainCommandBuffer;

        //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
        VkCommandBufferBeginInfo cmdBeginInfo = {};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.pNext = nullptr;

        cmdBeginInfo.pInheritanceInfo = nullptr;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        //make a clear-color from frame number. This will flash with a 120*pi frame period.
        VkClearValue clearValue;
        float flash = abs(sin(GFXDevice::FrameCount() / 120.f));
        clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

        //start the main renderpass.
        //We will use the clear color from above, and the framebuffer of the index the swapchain gave us
        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.pNext = nullptr;

        rpInfo.renderPass = _renderPass;
        rpInfo.renderArea.offset.x = 0;
        rpInfo.renderArea.offset.y = 0;
        rpInfo.renderArea.extent = windowExtents;
        rpInfo.framebuffer = _framebuffers[_swapchainImageIndex];

        //connect clear values
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        //once we start adding rendering commands, they will go here

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);

    }

    void VK_API::endFrame([[maybe_unused]] DisplayWindow& window, [[maybe_unused]] bool global) noexcept {
        assert(s_FrameInFlight == true);
        s_FrameInFlight = false;

        if (_skipEndFrame) {
            _skipEndFrame = false;
            return;
        }

        //naming it cmd for shorter writing
        VkCommandBuffer cmd = _mainCommandBuffer;

        //finalize the render pass
        vkCmdEndRenderPass(cmd);
        //finalize the command buffer (we can no longer add commands, but it can now be executed)
        VK_CHECK(vkEndCommandBuffer(cmd));

        //prepare the submission to the queue.
        //we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        //we will signal the _renderSemaphore, to signal that rendering has finished

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.pNext = nullptr;

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        submit.pWaitDstStageMask = &waitStage;

        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &_presentSemaphore;

        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &_renderSemaphore;

        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;

        //submit command buffer to the queue and execute it.
        // _renderFence will now block until the graphic commands finish execution
        VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

        // this will put the image we just rendered into the visible window.
        // we want to wait on the _renderSemaphore for that,
        // as it's necessary that drawing commands have finished before the image is displayed to the user
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;

        presentInfo.pSwapchains = &_swapchain;
        presentInfo.swapchainCount = 1;

        presentInfo.pWaitSemaphores = &_renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;

        presentInfo.pImageIndices = &_swapchainImageIndex;

        VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
    }

    ErrorCode VK_API::initRenderingAPI([[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config) noexcept {
        vkb::InstanceBuilder builder;

        VKUtil::fillEnumTables();

        const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();

        //make the Vulkan instance, with basic debug features
        auto inst_ret = builder.set_app_name(window.title())
            .request_validation_layers(Config::ENABLE_GPU_VALIDATION)
            .require_api_version(1, 2, 0)
            .set_debug_callback(divide_debug_callback)
            .set_debug_callback_user_data_pointer(this)
            .build();

        vkb::Instance vkb_inst = inst_ret.value();

        //store the instance
        _instance = vkb_inst.instance;
        //store the debug messenger
        _debugMessenger = vkb_inst.debug_messenger;

        // get the surface of the window we opened with SDL
        SDL_Vulkan_CreateSurface(window.getRawWindow(), _instance, &_surface);

        //use vkbootstrap to select a GPU.
        //We want a GPU that can write to the SDL surface and supports Vulkan 1.2
        vkb::PhysicalDeviceSelector selector{ vkb_inst };
        vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1, 2)
            .set_surface(_surface)
            .select()
            .value();

        //create the final Vulkan device
        vkb::DeviceBuilder deviceBuilder{ physicalDevice };

        vkb::Device vkbDevice = deviceBuilder.build().value();

        // Get the VkDevice handle used in the rest of a Vulkan application
        _device = vkbDevice.device;
        _chosenGPU = physicalDevice.physical_device;

        // use vkbootstrap to get a Graphics queue
        _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
        _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

        recreateSwapChain(window);
        initCommands();
        initSyncStructures();

        return ErrorCode::NO_ERR;
    }

    void VK_API::recreateSwapChain(const DisplayWindow& window) {
        cleanupSwapChain();

        initSwapChains(window);
        initDefaultRenderpass();
        initFramebuffers(window);
        initPipelines();
    }

    void VK_API::initSwapChains(const DisplayWindow& window) {
        const auto& windowDimensions = window.getDrawableSize();
        const VkExtent2D windowExtents{ windowDimensions.width, windowDimensions.height };

        vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

        vkb::Swapchain vkbSwapchain = swapchainBuilder
            .use_default_format_selection()
            //use vsync present mode
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(windowExtents.width, windowExtents.height)
            .build()
            .value();

        //store swapchain and its related images
        _swapchain = vkbSwapchain.swapchain;
        {
            auto images = vkbSwapchain.get_images().value();
            _swapchainImages.resize(0);
            _swapchainImages.reserve(images.size());
            for (const auto& image : images) {
                _swapchainImages.push_back(image);
            }
        }
        {
            auto imageViews = vkbSwapchain.get_image_views().value();
            _swapchainImageViews.resize(0);
            _swapchainImageViews.reserve(imageViews.size());
            for (const auto& imageView : imageViews) {
                _swapchainImageViews.push_back(imageView);
            }
        }
        _swapchainImageFormat = vkbSwapchain.image_format;
        _windowExtents = windowExtents;
    }

    void VK_API::initCommands() {
        //create a command pool for commands submitted to the graphics queue.
        //we also want the pool to allow for resetting of individual command buffers
        VkCommandPoolCreateInfo commandPoolInfo = vkInit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

        //allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkInit::command_buffer_allocate_info(_commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));
    }

    void VK_API::initDefaultRenderpass() {
        // the renderpass will use this color attachment.
        VkAttachmentDescription color_attachment = {};
        //the attachment will have the format needed by the swapchain
        color_attachment.format = _swapchainImageFormat;
        //1 sample, we won't be doing MSAA
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // we Clear when this attachment is loaded
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // we keep the attachment stored when the renderpass ends
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        //we don't care about stencil
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        //we don't know or care about the starting layout of the attachment
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        //after the renderpass ends, the image has to be on a layout ready for display
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref = {};
        //attachment number will index into the pAttachments array in the parent renderpass itself
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        //we are going to create 1 subpass, which is the minimum you can do
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

        //connect the color attachment to the info
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        //connect the subpass to the info
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;

        VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));
    }

    void VK_API::initFramebuffers(const DisplayWindow& window) {
        const auto& windowDimensions = window.getDrawableSize();
        const VkExtent2D windowExtents{ windowDimensions.width, windowDimensions.height };

        //create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = nullptr;

        fb_info.renderPass = _renderPass;
        fb_info.attachmentCount = 1;
        fb_info.width = windowExtents.width;
        fb_info.height = windowExtents.height;
        fb_info.layers = 1;

        //grab how many images we have in the swapchain
        const size_t swapchain_imagecount = _swapchainImages.size();
        _framebuffers.resize(swapchain_imagecount);

        //create framebuffers for each of the swapchain image views
        for (size_t i = 0; i < swapchain_imagecount; i++) {

            fb_info.pAttachments = &_swapchainImageViews[i];
            VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));
        }
    }

    void VK_API::initSyncStructures() {
        //create synchronization structures

        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.pNext = nullptr;

        //we want to create the fence with the Create Signaled flag, so we can wait on it before using it on a GPU command (for the first frame)
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

        //for the semaphores we don't need any flags
        VkSemaphoreCreateInfo semaphoreCreateInfo = {};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreCreateInfo.pNext = nullptr;
        semaphoreCreateInfo.flags = 0;

        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));
    }

    void VK_API::initPipelines() {
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

        VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

        //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
        PipelineBuilder pipelineBuilder;

        pipelineBuilder._shaderStages.push_back(
            vkInit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));

        pipelineBuilder._shaderStages.push_back(
            vkInit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));


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
        _trianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

        vkDestroyShaderModule(_device, triangleVertexShader, nullptr);
        vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    }

    void VK_API::cleanupSwapChain() {
        vkDeviceWaitIdle(_device);

        vkDestroyPipeline(_device, _trianglePipeline, nullptr);
        vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);

        if (_swapchain != nullptr) {
            vkDestroySwapchainKHR(_device, _swapchain, nullptr);
            _swapchain = nullptr;

            //destroy the main renderpass
            vkDestroyRenderPass(_device, _renderPass, nullptr);
            _renderPass = nullptr;

            //destroy swapchain resources
            for (size_t i = 0; i < _framebuffers.size(); i++) {
                vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
                vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
            }
            _framebuffers.clear();
            _swapchainImageViews.clear();
        }
    }

    void VK_API::closeRenderingAPI() {
        cleanupSwapChain();

        vkDestroySemaphore(_device, _presentSemaphore, nullptr);
        vkDestroySemaphore(_device, _renderSemaphore, nullptr);
        vkDestroyFence(_device, _renderFence, nullptr);

        vkDestroyCommandPool(_device, _commandPool, nullptr);

        vkDestroyDevice(_device, nullptr);
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);
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
        if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            return false;
        }
        *outShaderModule = shaderModule;
        return true;


    }

    const PerformanceMetrics& VK_API::getPerformanceMetrics() const noexcept {
        static PerformanceMetrics perf;
        return perf;
    }

    void VK_API::flushCommand([[maybe_unused]] const GFX::CommandBuffer::CommandEntry& entry, [[maybe_unused]] const GFX::CommandBuffer& commandBuffer) noexcept {
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

    bool VK_API::setViewport([[maybe_unused]] const Rect<I32>& newViewport) noexcept {
        return true;
    }

    void VK_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) noexcept {
    }

}; //namespace Divide