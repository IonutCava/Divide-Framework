#include "stdafx.h"

#include "Headers/VKSwapChain.h"
#include "Headers/VKDevice.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Headers/DisplayWindow.h"

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

        _swapChain.swapchain = VK_NULL_HANDLE;

        _swapChain.destroy_image_views(_swapchainImageViews);
        _swapchainImages.clear();
        _swapchainImageViews.clear();

        if (!_inFlightFences.empty())
        {
            const VkDevice device = _device.getVKDevice();
            for (U8 i = 0u; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
            {
                vkDestroySemaphore(device, _renderFinishedSemaphores[i], nullptr);
                vkDestroySemaphore(device, _imageAvailableSemaphores[i], nullptr);
                vkDestroyFence(device,     _inFlightFences[i],           nullptr);
            }
        }
        _imagesInFlight.clear();
    }

    ErrorCode VKSwapChain::create(const bool vSync, const bool adaptiveSync, VkSurfaceKHR targetSurface)
    {
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
            Console::errorfn( Locale::Get( _ID( "ERROR_VK_INIT" ) ), vkbSwapchain.error().message().c_str() );
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

        _imageAvailableSemaphores.resize( Config::MAX_FRAMES_IN_FLIGHT );
        _renderFinishedSemaphores.resize( Config::MAX_FRAMES_IN_FLIGHT );
        _inFlightFences.resize( Config::MAX_FRAMES_IN_FLIGHT );
        _commandBuffers.resize( Config::MAX_FRAMES_IN_FLIGHT );
        _imagesInFlight.resize( _swapchainImages.size(), VK_NULL_HANDLE );


        const VkSemaphoreCreateInfo semaphoreCreateInfo = vk::semaphoreCreateInfo();
        const VkFenceCreateInfo fenceCreateInfo = vk::fenceCreateInfo( VK_FENCE_CREATE_SIGNALED_BIT );

        const VkDevice device = _device.getVKDevice();
        for ( U8 i = 0u; i < Config::MAX_FRAMES_IN_FLIGHT; i++ )
        {
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &_imageAvailableSemaphores[i] ) );
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &_renderFinishedSemaphores[i] ) );
            VK_CHECK( vkCreateFence( device, &fenceCreateInfo, nullptr, &_inFlightFences[i] ) );
        }

        //allocate the default command buffer that we will use for rendering
        const VkCommandBufferAllocateInfo cmdAllocInfo = vk::commandBufferAllocateInfo( _device.getQueue( QueueType::GRAPHICS)._pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, Config::MAX_FRAMES_IN_FLIGHT );
        VK_CHECK( vkAllocateCommandBuffers( device, &cmdAllocInfo, _commandBuffers.data() ) );

        const auto& windowDimensions = _window.getDrawableSize();
        surfaceExtent( VkExtent2D{ windowDimensions.width, windowDimensions.height } );

        return ErrorCode::NO_ERR;
    }

    VkResult VKSwapChain::beginFrame()
    {
        //wait until the GPU has finished rendering the last frame.
        VK_CHECK(vkWaitForFences(_device.getVKDevice(), 1, &_inFlightFences[_currentFrameIdx], VK_TRUE, U64_MAX));

        //request image from the swapchain, one second timeout
        VkResult ret = vkAcquireNextImageKHR(_device.getVKDevice(), _swapChain.swapchain, U64_MAX, _imageAvailableSemaphores[_currentFrameIdx], nullptr, &_swapchainImageIndex);

        if ( ret == VK_SUCCESS )
        {
            //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
            VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo( VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT );
            VK_CHECK( vkBeginCommandBuffer( _commandBuffers[_currentFrameIdx], &cmdBeginInfo ) );
        }

        return ret;
    }

    VkResult VKSwapChain::endFrame( QueueType queue ) 
    {
        // prepare the submission to the queue.
        // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        // we will signal the _renderSemaphore, to signal that rendering has finished

        VK_CHECK( vkEndCommandBuffer( _commandBuffers[_currentFrameIdx] ) );

        if (_imagesInFlight[_swapchainImageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(_device.getVKDevice(), 1, &_imagesInFlight[_swapchainImageIndex], VK_TRUE, U64_MAX);
        }

        _imagesInFlight[_swapchainImageIndex] = _inFlightFences[_currentFrameIdx];

        constexpr VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submit = vk::submitInfo();
        submit.pWaitDstStageMask = waitStages;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &_imageAvailableSemaphores[_currentFrameIdx];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &_renderFinishedSemaphores[_currentFrameIdx];
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &_commandBuffers[_currentFrameIdx];

        VK_CHECK(vkResetFences(_device.getVKDevice(), 1, &_inFlightFences[_currentFrameIdx]));

        // submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
        _device.submitToQueue(queue, submit, _inFlightFences[_currentFrameIdx]);

        // this will put the image we just rendered into the visible window.
        // we want to wait on the _renderSemaphore for that, as it's necessary that drawing commands have finished before the image is displayed to the user

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pSwapchains = &_swapChain.swapchain;
        presentInfo.swapchainCount = 1;
        presentInfo.pWaitSemaphores = &_renderFinishedSemaphores[_currentFrameIdx];
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &_swapchainImageIndex;

        _currentFrameIdx = (_currentFrameIdx + 1) % Config::MAX_FRAMES_IN_FLIGHT;
        return _device.queuePresent(queue, presentInfo);
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

    VkCommandBuffer VKSwapChain::getCurrentCommandBuffer() const noexcept
    {
        return _commandBuffers[_currentFrameIdx];
    }

    VkFence VKSwapChain::getCurrentFence() const noexcept
    {
        return _inFlightFences[_currentFrameIdx];
    }
}; //namespace Divide
