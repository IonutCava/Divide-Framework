// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/vkSwapChain.h"
#include "Headers/vkDevice.h"

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
        for ( size_t i = 0u; i < _frames.size(); ++i )
        {
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore ) );
            VK_CHECK( vkCreateFence( device,     &fenceCreateInfo,     nullptr, &_frames[i]._renderFence._handle ) );
            VK_CHECK( vkAllocateCommandBuffers( device, &cmdAllocInfo, &_frames[i]._commandBuffer ) );
            _frames[i]._renderFence._tag = i;
        }

        for (VkSemaphore& semaphore : _renderSemaphores)
        {
            VK_CHECK( vkCreateSemaphore( device, &semaphoreCreateInfo, nullptr, &semaphore) );
        }

        const auto& windowDimensions = _window.getDrawableSize();
        surfaceExtent( VkExtent2D{ windowDimensions.width, windowDimensions.height } );

        return ErrorCode::NO_ERR;
    }

    VkResult VKSwapChain::beginFrame()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _frames.empty() || _swapChain.swapchain == VK_NULL_HANDLE )
        {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }

        _activeFrame = &_frames[FrameSlotIndex( GFXDevice::FrameCount() )];

        //wait until the GPU has finished rendering the last frame.
        {
            PROFILE_SCOPE( "Wait for fences", Profiler::Category::Graphics);
            VK_CHECK( vkWaitForFences( _device.getVKDevice(), 1, &_activeFrame->_renderFence._handle, VK_TRUE, U64_MAX ) );
        }
        //request image from the swapchain, one second timeout
        VkResult ret = VK_ERROR_UNKNOWN;
        {
            PROFILE_SCOPE( "Aquire Next Image", Profiler::Category::Graphics );
            ret = vkAcquireNextImageKHR(_device.getVKDevice(), _swapChain.swapchain, U64_MAX, _activeFrame->_presentSemaphore, nullptr, &_swapchainImageIndex);
        }

        if ( ret == VK_SUCCESS || ret == VK_SUBOPTIMAL_KHR )
        {
            _currentImageRenderedThisFrame = false;
            PROFILE_SCOPE( "Begin Command Buffer", Profiler::Category::Graphics );
            //begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
            VkCommandBufferBeginInfo cmdBeginInfo = vk::commandBufferBeginInfo( VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT );
            VK_CHECK( vkBeginCommandBuffer(_activeFrame->_commandBuffer, &cmdBeginInfo ) );
        }

        return ret;
    }

    VkResult VKSwapChain::endFrame( const VKSubmitSempahore::Container& semaphores )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

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

        VK_CHECK( vkEndCommandBuffer(_activeFrame->_commandBuffer ) );

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
        VK_CHECK( vkResetFences( _device.getVKDevice(), 1, &_activeFrame->_renderFence._handle ) );
        // submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
        const VkResult submitResult = _device.submitToQueue(QueueType::GRAPHICS, submit, _activeFrame->_renderFence._handle);
        if ( submitResult != VK_SUCCESS )
        {
            return submitResult;
        }

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
