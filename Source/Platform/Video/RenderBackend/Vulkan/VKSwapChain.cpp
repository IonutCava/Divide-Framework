

#include "Headers/VKSwapChain.h"
#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

    VKSwapChain::VKSwapChain(VK_API& context, const VKDevice& device, const DisplayWindow& window)
        : _context(context)
        , _device(device)
        , _window(window)
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
        vkb::destroy_swapchain(_swapChain);

        _swapChain.destroy_image_views(_swapchainImageViews);
        _swapchainImages.clear();
        _swapchainImageViews.clear();

        if ( _swapChain.swapchain != VK_NULL_HANDLE )
        {
            const VkDevice device = _device.getVKDevice();
            for (U8 i = 0u; i < Config::MAX_FRAMES_IN_FLIGHT; ++i)
            {
               
                vkDestroySemaphore(device, _frames[i]._presentSemaphore, nullptr);
                vkDestroySemaphore(device, _frames[i]._renderSemaphore,  nullptr);
                vkDestroyFence(device,     _frames[i]._renderFence._handle, nullptr);
                _frames[i]._renderFence._tag = U8_MAX;
            }
            _swapChain.swapchain = VK_NULL_HANDLE;
        }
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

        // adaptiveSync not supported yet
        DIVIDE_UNUSED(adaptiveSync);

        auto vkbSwapchain = swapchainBuilder.set_desired_format( { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .set_desired_format( { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .set_desired_format( { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .add_fallback_format( { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } )
                                            .set_desired_present_mode( vSync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR )
                                            .add_fallback_present_mode( VK_PRESENT_MODE_FIFO_KHR )
                                            .set_desired_extent( surfaceExtent().width, surfaceExtent().height )
                                            .build();


        if ( !vkbSwapchain )
        {
            Console::errorfn( LOCALE_STR( "ERROR_VK_INIT" ), vkbSwapchain.error().message().c_str() );
            return ErrorCode::VK_OLD_HARDWARE;
        }

        destroy();

        _swapChain = vkbSwapchain.value();
        _swapchainImages = _swapChain.get_images().value();
        _swapchainImageViews = _swapChain.get_image_views().value();

        if ( _swapChain.image_format == VK_FORMAT_UNDEFINED )
        {
            return ErrorCode::VK_SURFACE_CREATE;
        }


        const VkSemaphoreCreateInfo semaphoreCreateInfo = vk::semaphoreCreateInfo();
        const VkFenceCreateInfo fenceCreateInfo = vk::fenceCreateInfo( VK_FENCE_CREATE_SIGNALED_BIT );
        const VkCommandBufferAllocateInfo cmdAllocInfo = vk::commandBufferAllocateInfo( _device.getQueue( QueueType::GRAPHICS)._pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1u );

        const VkDevice device = _device.getVKDevice();
        for ( U8 i = 0u; i < Config::MAX_FRAMES_IN_FLIGHT; ++i )
        {
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore ) );
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore ) );
            VK_CHECK( vkCreateFence( device,     &fenceCreateInfo,     nullptr, &_frames[i]._renderFence._handle ) );
            VK_CHECK( vkAllocateCommandBuffers( device, &cmdAllocInfo, &_frames[i]._commandBuffer ) );
            _frames[i]._renderFence._tag = i;
        }

        const auto& windowDimensions = _window.getDrawableSize();
        surfaceExtent( VkExtent2D{ windowDimensions.width, windowDimensions.height } );

        return ErrorCode::NO_ERR;
    }

    VkResult VKSwapChain::beginFrame()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        FrameData& crtFrame = getFrameData();

        //wait until the GPU has finished rendering the last frame.
        {
            PROFILE_SCOPE( "Wait for fences", Profiler::Category::Graphics);
            VK_CHECK( vkWaitForFences( _device.getVKDevice(), 1, &crtFrame._renderFence._handle, VK_TRUE, U64_MAX ) );
        }
        {
            PROFILE_SCOPE( "Reset fences", Profiler::Category::Graphics );
            VK_CHECK( vkResetFences( _device.getVKDevice(), 1, &crtFrame._renderFence._handle ) );
        }

        //request image from the swapchain, one second timeout
        VkResult ret = VK_ERROR_UNKNOWN;
        {
            PROFILE_SCOPE( "Aquire Next Image", Profiler::Category::Graphics );
            ret = vkAcquireNextImageKHR(_device.getVKDevice(), _swapChain.swapchain, U64_MAX, crtFrame._presentSemaphore, nullptr, &_swapchainImageIndex);
        }

        if ( ret == VK_SUCCESS )
        {
            PROFILE_SCOPE( "Begin Command Buffer", Profiler::Category::Graphics );
            //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
            VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo( VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT );
            VK_CHECK( vkBeginCommandBuffer( crtFrame._commandBuffer, &cmdBeginInfo ) );
        }

        return ret;
    }

    VkResult VKSwapChain::endFrame( ) 
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        FrameData& crtFrame = getFrameData();
        VK_CHECK( vkEndCommandBuffer( crtFrame._commandBuffer ) );

        // prepare the submission to the queue.
        // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        // we will signal the _renderSemaphore, to signal that rendering has finished
        constexpr VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submit = vk::submitInfo();
        submit.pWaitDstStageMask = waitStages;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &crtFrame._presentSemaphore;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &crtFrame._renderSemaphore;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &crtFrame._commandBuffer;

        // submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
        _device.submitToQueue(QueueType::GRAPHICS, submit, crtFrame._renderFence._handle);
        PROFILE_VK_PRESENT( &_swapChain.swapchain );

        // this will put the image we just rendered into the visible window.
        // we want to wait on the _renderSemaphore for that, as it's necessary that drawing commands have finished before the image is displayed to the user
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pSwapchains = &_swapChain.swapchain;
        presentInfo.swapchainCount = 1;
        presentInfo.pWaitSemaphores = &crtFrame._renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &_swapchainImageIndex;

        const VkResult ret = _device.queuePresent( QueueType::GRAPHICS, presentInfo);

        return ret;
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

    FrameData& VKSwapChain::getFrameData() noexcept
    {
        return _frames[GFXDevice::FrameCount() % Config::MAX_FRAMES_IN_FLIGHT];
    }
}; //namespace Divide
