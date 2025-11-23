

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
        vkb::destroy_swapchain(_swapChain);

        _swapChain.destroy_image_views(_swapchainImageViews);
        _swapchainImages.clear();
        _swapchainImageViews.clear();

        if ( _swapChain.swapchain != VK_NULL_HANDLE )
        {
            const VkDevice device = _device.getVKDevice();
            for (FrameData& frameData : _frames)
            {
                vkDestroySemaphore(device, frameData._presentSemaphore, nullptr);
                vkDestroyFence(device, frameData._renderFence._handle, nullptr);
            }
            _frames.clear();
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
        _frames.resize(_swapchainImages.size());
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

        _activeFrame = &_frames[GFXDevice::FrameCount() % _frames.size()];

        //wait until the GPU has finished rendering the last frame.
        {
            PROFILE_SCOPE( "Wait for fences", Profiler::Category::Graphics);
            VK_CHECK( vkWaitForFences( _device.getVKDevice(), 1, &_activeFrame->_renderFence._handle, VK_TRUE, U64_MAX ) );
        }
        {
            PROFILE_SCOPE( "Reset fences", Profiler::Category::Graphics );
            VK_CHECK( vkResetFences( _device.getVKDevice(), 1, &_activeFrame->_renderFence._handle ) );
        }

        //request image from the swapchain, one second timeout
        VkResult ret = VK_ERROR_UNKNOWN;
        {
            PROFILE_SCOPE( "Aquire Next Image", Profiler::Category::Graphics );
            ret = vkAcquireNextImageKHR(_device.getVKDevice(), _swapChain.swapchain, U64_MAX, _activeFrame->_presentSemaphore, nullptr, &_swapchainImageIndex);
        }

        if ( ret == VK_SUCCESS )
        {
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

        static fixed_vector<VkPipelineStageFlags, 8, true> s_waitStages;
        static VKSubmitSempahore::Container s_waitSempahores;

        s_waitSempahores.reset_lose_memory();
        s_waitSempahores.reserve(semaphores.size() + 1u);

        s_waitStages.reset_lose_memory();
        s_waitStages.reserve(semaphores.size() + 1u);

        s_waitSempahores.push_back( _activeFrame->_presentSemaphore );
        s_waitStages.push_back( VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT );

        for ( VkSemaphore s : semaphores )
        {
            s_waitSempahores.push_back(s);
            s_waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }

        VK_CHECK( vkEndCommandBuffer(_activeFrame->_commandBuffer ) );

        VkSemaphore imageRenderSemaphore = _renderSemaphores[_swapchainImageIndex];

        // prepare the submission to the queue.
        // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        // we will signal the _renderSemaphore, to signal that rendering has finished
       
        VkSubmitInfo submit = vk::submitInfo();
        submit.pWaitDstStageMask = s_waitStages.data();
        submit.waitSemaphoreCount = to_U32(s_waitSempahores.size());
        submit.pWaitSemaphores = s_waitSempahores.data();
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &imageRenderSemaphore;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &_activeFrame->_commandBuffer;

        // submit command buffer to the queue and execute it. _renderFence will now block until the graphic commands finish execution
        _device.submitToQueue(QueueType::GRAPHICS, submit, _activeFrame->_renderFence._handle);
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

        return _device.queuePresent( QueueType::GRAPHICS, presentInfo);
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

    bool VKSwapChain::getFrameData(FrameData*& dataOut) const noexcept
    {
        dataOut = _activeFrame;
        return dataOut != nullptr;
    }
}; //namespace Divide
