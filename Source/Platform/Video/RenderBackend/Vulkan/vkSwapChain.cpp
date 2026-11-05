// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/vkSwapChain.h"
#include "Headers/vkDevice.h"

#include "Core/Headers/RenderSmokeTest.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

    VKSwapChain::VKSwapChain( VK_API& context, const VKDevice& device, const DisplayWindow& window, const bool smokeReadback )
        : _context(context)
        , _device(device)
        , _window(window)
        , _smokeReadback(smokeReadback)
    {
        const auto& windowDimensions = _window.getDrawableSize();
        surfaceExtent({ windowDimensions.width, windowDimensions.height } );
    }

    VKSwapChain::~VKSwapChain()
    {
        destroy();
    }
    
    void VKSwapChain::destroy() 
    {
        _activeFrame = nullptr;
        _swapChain.destroy_image_views(_swapchainImageViews);

        if ( _swapChain.swapchain != VK_NULL_HANDLE )
        {
            const VkDevice device = _device.getVKDevice();
            const VkCommandPool pool = _device.getQueue(QueueType::GRAPHICS)._pool;
            for (FrameData& frameData : _frames)
            {
                if ( frameData._commandBuffer != VK_NULL_HANDLE )
                {
                    vkFreeCommandBuffers(device, pool, 1u, &frameData._commandBuffer);
                }
                vkDestroySemaphore(device, frameData._presentSemaphore, nullptr);
                vkDestroyFence(device, frameData._renderFence._handle, nullptr);
            }
            _frames.clear();
            vkb::destroy_swapchain(_swapChain);
            _swapChain.swapchain = VK_NULL_HANDLE;

            for (VkSemaphore& semaphore : _renderSemaphores)
            {
                if ( semaphore != VK_NULL_HANDLE )
                {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }
            _renderSemaphores.clear();
        }
        _swapchainImages.clear();
        _swapchainImageViews.clear();
        _swapchainImagePresented.clear();
    }

    ErrorCode VKSwapChain::create(const bool vSync, const bool adaptiveSync, VkSurfaceKHR targetSurface)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        vkb::SwapchainBuilder swapchainBuilder{ _device.getPhysicalDevice(),
                                                _device.getDevice(),
                                                targetSurface,
                                                _device.getQueue(QueueType::GRAPHICS )._index,
                                                _device.getPresentQueueIndex()};

        if ( _swapChain.swapchain != VK_NULL_HANDLE )
        {
            swapchainBuilder.set_old_swapchain( _swapChain );
        }

        if ( _smokeReadback )
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            const VkResult capabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( _device.getVKPhysicalDevice(),
                                                                                           targetSurface, &capabilities );
            if ( capabilitiesResult == VK_ERROR_DEVICE_LOST )
            {
                ReportVulkanDeviceLost( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR" );
            }
            if ( capabilitiesResult != VK_SUCCESS )
            {
                char detail[128]{};
                std::snprintf( detail, sizeof( detail ), "Surface capability query failed: %d", to_I32( capabilitiesResult ) );
                RenderSmokeTest::Fail( "window_capture", detail, RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            if ( ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) == 0u )
            {
                RenderSmokeTest::Fail( "window_capture", "The main swapchain does not support transfer-source readback",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            swapchainBuilder.add_image_usage_flags( VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        }

        // adaptiveSync not supported yet
        DIVIDE_UNUSED(adaptiveSync);

        // Prefer sRGB formats so the hardware encodes our linear output on write (same as OpenGL's GL_FRAMEBUFFER_SRGB).
        // Note: vk-bootstrap's set_desired_format() inserts at the front of the list, so add everything as ordered fallbacks instead.
        // If only UNORM formats are available, isSRGB() reports false and the final screen pass encodes to sRGB in the shader.
        auto vkbSwapchain = swapchainBuilder.add_fallback_format( { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .set_desired_present_mode( vSync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR )
                                            .add_fallback_present_mode( VK_PRESENT_MODE_FIFO_KHR )
                                            .set_desired_extent( surfaceExtent().width, surfaceExtent().height )
                                            .build();


        if ( !vkbSwapchain )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), vkbSwapchain.error().message().c_str() );
            destroy();
            return ErrorCode::VK_OLD_HARDWARE;
        }

        destroy();

        _swapChain = vkbSwapchain.value();
        _swapchainImages = _swapChain.get_images().value();
        _swapchainImageViews = _swapChain.get_image_views().value();
        _swapchainImagePresented.assign(_swapchainImages.size(), 0u);
        _frames.resize(FrameSlotCount());
        _renderSemaphores.resize(_swapchainImages.size());

        if ( _swapChain.image_format == VK_FORMAT_UNDEFINED )
        {
            return ErrorCode::VK_SURFACE_CREATE;
        }

        const VkSemaphoreCreateInfo semaphoreCreateInfo = vk::semaphoreCreateInfo();
        const VkFenceCreateInfo fenceCreateInfo = vk::fenceCreateInfo( VK_FENCE_CREATE_SIGNALED_BIT );
        const VkCommandBufferAllocateInfo cmdAllocInfo = vk::commandBufferAllocateInfo( _device.getQueue( QueueType::GRAPHICS)._pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1u );

        const VkDevice device = _device.getVKDevice();
        const auto creationFailed = [this]( FrameData& frame, const VkResult result, const char* operation )
        {
            frame._frameError = result;
            if ( result == VK_ERROR_DEVICE_LOST && RenderSmokeTest::Enabled() )
            {
                ReportVulkanDeviceLost( operation, &_device );
            }
            Console::errorfn( "Vulkan swapchain initialization failed during {}: {} ({})",
                              operation, VKErrorString( result ).c_str(), to_I32( result ) );
            return ErrorCode::VK_SURFACE_CREATE;
        };

        for ( size_t i = 0u; i < _frames.size(); ++i )
        {
            FrameData& frame = _frames[i];
            VkSemaphore presentSemaphore = VK_NULL_HANDLE;
            VkResult result = vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &presentSemaphore );
            if ( result != VK_SUCCESS )
            {
                return creationFailed( frame, result, "vkCreateSemaphore (acquire)" );
            }
            frame._presentSemaphore = presentSemaphore;

            VkFence renderFence = VK_NULL_HANDLE;
            result = vkCreateFence( device, &fenceCreateInfo, nullptr, &renderFence );
            if ( result != VK_SUCCESS )
            {
                return creationFailed( frame, result, "vkCreateFence" );
            }
            frame._renderFence._handle = renderFence;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            result = vkAllocateCommandBuffers( device, &cmdAllocInfo, &commandBuffer );
            if ( result != VK_SUCCESS )
            {
                return creationFailed( frame, result, "vkAllocateCommandBuffers" );
            }
            frame._commandBuffer = commandBuffer;
            frame._renderFence._tag = to_U8( i );
        }

        for (VkSemaphore& semaphore : _renderSemaphores)
        {
            VkSemaphore renderSemaphore = VK_NULL_HANDLE;
            const VkResult result = vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &renderSemaphore );
            if ( result != VK_SUCCESS )
            {
                return creationFailed( _frames.front(), result, "vkCreateSemaphore (render finished)" );
            }
            semaphore = renderSemaphore;
        }

        const auto& windowDimensions = _window.getDrawableSize();
        surfaceExtent( VkExtent2D{ windowDimensions.width, windowDimensions.height } );

        return ErrorCode::NO_ERR;
    }

    bool VKSwapChain::ConvertSmokeReadback( const VkFormat format, const U32 width, const U32 height,
                                            const std::span<const Byte> source, const std::span<Byte> destination ) noexcept
    {
        if ( !SupportsSmokeReadback( format ) || width == 0u || height == 0u ||
             to_size( width ) > std::numeric_limits<size_t>::max() / 4u )
        {
            return false;
        }
        const size_t rowBytes = to_size( width ) * 4u;
        if ( to_size( height ) > std::numeric_limits<size_t>::max() / rowBytes )
        {
            return false;
        }
        const size_t requiredBytes = rowBytes * to_size( height );
        if ( source.size() < requiredBytes || destination.size() < requiredBytes )
        {
            return false;
        }

        const bool packed = format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                            format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        const bool swapRedBlue = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB ||
                                 format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        // The swapchain is top-down; ImageTools::SaveImage expects bottom-up RGBA.
        for ( size_t row = 0u; row < to_size( height ); ++row )
        {
            const Byte* input = source.data() + ( to_size( height ) - row - 1u ) * rowBytes;
            Byte* output = destination.data() + row * rowBytes;
            if ( !packed && !swapRedBlue )
            {
                std::memcpy( output, input, rowBytes );
                continue;
            }
            for ( size_t column = 0u; column < to_size( width ); ++column )
            {
                if ( packed )
                {
                    U32 pixel = 0u;
                    std::memcpy( &pixel, input, sizeof( pixel ) );
                    const auto channel = []( const U32 value ) { return Byte{ to_U8( ( value * 255u + 511u ) / 1023u ) }; };
                    const U32 low = pixel & 1023u;
                    const U32 high = ( pixel >> 20u ) & 1023u;
                    output[0] = channel( swapRedBlue ? high : low );
                    output[1] = channel( ( pixel >> 10u ) & 1023u );
                    output[2] = channel( swapRedBlue ? low : high );
                    output[3] = Byte{ to_U8( ( pixel >> 30u ) * 85u ) };
                }
                else
                {
                    output[0] = input[2];
                    output[1] = input[1];
                    output[2] = input[0];
                    output[3] = input[3];
                }
                input += 4u;
                output += 4u;
            }
        }
        return true;
    }

    VkImageMemoryBarrier2 VKSwapChain::SmokePresentBarrier( const VkImage image, const bool capture ) noexcept
    {
        VkImageMemoryBarrier2 barrier = vk::imageMemoryBarrier2();
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = capture ? VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = capture ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_NONE;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        return barrier;
    }

    VkResult VKSwapChain::waitForFrameSlot( const U64 frameNumber ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        return WaitForFrameSlot( { _frames.data(), _frames.size() }, frameNumber, _device.getVKDevice(),
                                 []( const VkDevice device, const VkFence fence )
        {
            PROFILE_SCOPE( "Wait for fences", Profiler::Category::Graphics );
            return vkWaitForFences( device, 1u, &fence, VK_TRUE, U64_MAX );
        } );
    }

    VkResult VKSwapChain::beginFrame()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _activeFrame = nullptr;
        if ( _frames.empty() || _swapChain.swapchain == VK_NULL_HANDLE )
        {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }

        const U64 frameNumber = GFXDevice::FrameCount();
        const VkResult waitResult = waitForFrameSlot( frameNumber );
        if ( waitResult != VK_SUCCESS )
        {
            return waitResult;
        }

        FrameData& frame = _frames[FrameSlotIndex( frameNumber )];
        if ( frame._presentSemaphore == VK_NULL_HANDLE || frame._commandBuffer == VK_NULL_HANDLE )
        {
            frame._frameError = VK_ERROR_INITIALIZATION_FAILED;
            return frame._frameError;
        }

        _activeFrame = &frame;
        // Request an image from the swapchain, waiting until one is available.
        VkResult ret = VK_ERROR_UNKNOWN;
        {
            PROFILE_SCOPE( "Acquire Next Image", Profiler::Category::Graphics );
            ret = vkAcquireNextImageKHR(_device.getVKDevice(), _swapChain.swapchain, U64_MAX, _activeFrame->_presentSemaphore, nullptr, &_swapchainImageIndex);
        }

        if ( ret == VK_SUCCESS || ret == VK_SUBOPTIMAL_KHR )
        {
            if ( _swapchainImageIndex >= _swapchainImages.size() ||
                 _swapchainImageIndex >= _swapchainImageViews.size() ||
                 _swapchainImageIndex >= _swapchainImagePresented.size() ||
                 _swapchainImageIndex >= _renderSemaphores.size() ||
                 _swapchainImages[_swapchainImageIndex] == VK_NULL_HANDLE ||
                 _swapchainImageViews[_swapchainImageIndex] == VK_NULL_HANDLE ||
                 _renderSemaphores[_swapchainImageIndex] == VK_NULL_HANDLE )
            {
                frame._frameError = VK_ERROR_INITIALIZATION_FAILED;
                _activeFrame = nullptr;
                return frame._frameError;
            }

            _currentImageRenderedThisFrame = false;
            PROFILE_SCOPE( "Begin Command Buffer", Profiler::Category::Graphics );
            //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
            VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo( VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT );
            const VkResult beginResult = vkBeginCommandBuffer( frame._commandBuffer, &cmdBeginInfo );
            if ( beginResult != VK_SUCCESS )
            {
                frame._frameError = beginResult;
                _activeFrame = nullptr;
                return beginResult;
            }
        }
        else
        {
            if ( ret != VK_ERROR_OUT_OF_DATE_KHR )
            {
                frame._frameError = ret;
            }
            _activeFrame = nullptr;
        }

        return ret;
    }

    VkResult VKSwapChain::endFrame( const VKSubmitSempahore::Container& semaphores )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _activeFrame == nullptr )
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        fixed_vector<VkPipelineStageFlags, 8, true> waitStages;
        VKSubmitSempahore::Container waitSemaphores;

        waitSemaphores.reserve(semaphores.size() + 1u);
        waitStages.reserve(semaphores.size() + 1u);

        waitSemaphores.push_back( _activeFrame->_presentSemaphore );
        waitStages.push_back( VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT );

        for ( VkSemaphore s : semaphores )
        {
            waitSemaphores.push_back(s);
            waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }

        const VkResult endResult = vkEndCommandBuffer( _activeFrame->_commandBuffer );
        if ( endResult != VK_SUCCESS )
        {
            _activeFrame->_frameError = endResult;
            return endResult;
        }

        VkSemaphore imageRenderSemaphore = _renderSemaphores[_swapchainImageIndex];

        // prepare the submission to the queue.
        // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        // we will signal the _renderSemaphore, to signal that rendering has finished
       
        VkSubmitInfo submit = vk::submitInfo();
        submit.pWaitDstStageMask = waitStages.data();
        submit.waitSemaphoreCount = to_U32(waitSemaphores.size());
        submit.pWaitSemaphores = waitSemaphores.data();
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &imageRenderSemaphore;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &_activeFrame->_commandBuffer;

        // Reset only when submitting, so an abandoned frame leaves a signaled fence.
        const VkResult resetResult = vkResetFences( _device.getVKDevice(), 1u, &_activeFrame->_renderFence._handle );
        if ( resetResult != VK_SUCCESS )
        {
            _activeFrame->_frameError = resetResult;
            return resetResult;
        }

        // submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
        const VkResult submitResult = _device.submitToQueue(QueueType::GRAPHICS, submit, _activeFrame->_renderFence._handle);
        if ( submitResult != VK_SUCCESS )
        {
            _activeFrame->_frameError = submitResult;
            return submitResult;
        }
        _activeFrame->_submissionPending = true;

        PROFILE_VK_PRESENT( &_swapChain.swapchain );

        // this will put the image we just rendered into the visible window.
        // we want to wait on the _renderSemaphore for that, as it's necessary that drawing commands have finished before the image is displayed to the user
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pSwapchains = &_swapChain.swapchain;
        presentInfo.swapchainCount = 1;
        presentInfo.pWaitSemaphores = &imageRenderSemaphore;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &_swapchainImageIndex;

        const VkResult presentResult = _device.queuePresent( QueueType::GRAPHICS, presentInfo );
        if ( presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR )
        {
            markCurrentImagePresented();
        }
        else if ( presentResult != VK_ERROR_OUT_OF_DATE_KHR )
        {
            _activeFrame->_frameError = presentResult;
        }

        return presentResult;
    }

    vkb::Swapchain& VKSwapChain::getSwapChain() noexcept
    {
        return _swapChain;
    }

    VkImage VKSwapChain::getCurrentImage() const noexcept
    {
        return _swapchainImages[_swapchainImageIndex];
    }

    VkImageView VKSwapChain::getCurrentImageView() const noexcept
    {
        return _swapchainImageViews[_swapchainImageIndex];
    }

    bool VKSwapChain::isSRGB() const noexcept
    {
        switch ( _swapChain.image_format )
        {
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            case VK_FORMAT_B8G8R8_SRGB:
            case VK_FORMAT_R8G8B8_SRGB:
                return true;
            default:
                break;
        }

        return false;
    }

    bool VKSwapChain::currentImageWasPresented() const noexcept
    {
        return _swapchainImagePresented[_swapchainImageIndex] != 0u;
    }

    bool VKSwapChain::currentImageWasRenderedThisFrame() const noexcept
    {
        return _currentImageRenderedThisFrame;
    }

    void VKSwapChain::markCurrentImagePresented() noexcept
    {
        _swapchainImagePresented[_swapchainImageIndex] = 1u;
    }

    void VKSwapChain::markCurrentImageRenderedThisFrame() noexcept
    {
        _currentImageRenderedThisFrame = true;
    }

    bool VKSwapChain::getFrameData(FrameData*& dataOut) const noexcept
    {
        dataOut = _activeFrame;
        return dataOut != nullptr;
    }

    VkFence VKSwapChain::activeFrameFence() const noexcept
    {
        return _activeFrame != nullptr ? _activeFrame->_renderFence._handle : VK_NULL_HANDLE;
    }
}; //namespace Divide
