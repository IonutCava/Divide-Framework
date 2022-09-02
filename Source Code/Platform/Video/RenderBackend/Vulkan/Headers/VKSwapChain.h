/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _VK_SWAP_CHAIN_H_
#define _VK_SWAP_CHAIN_H_

#include "vkResources.h"

namespace Divide {
    class DisplayWindow;
    class VK_API;
    class VKDevice;
    class GFXDevice;

    class VKSwapChain final : NonCopyable, NonMovable {
    public:
        static constexpr U8 MAX_FRAMES_IN_FLIGHT = 3u;

    public:
        VKSwapChain(VK_API& context, const VKDevice& device, const DisplayWindow& window);
        ~VKSwapChain();

        ErrorCode create(bool vSync, bool adaptiveSync, VkSurfaceKHR targetSurface);
        void destroy();

        [[nodiscard]] VkResult beginFrame();
        [[nodiscard]] VkResult endFrame(vkb::QueueType queue, VkCommandBuffer& cmdBuffer);

        [[nodiscard]] vkb::Swapchain getSwapChain() const noexcept;
        [[nodiscard]] VkRenderPass   getRenderPass() const noexcept;
        [[nodiscard]] VkFramebuffer  getCurrentFrameBuffer() const noexcept;

    private:
        [[nodiscard]] ErrorCode createSwapChainInternal(bool vSync, bool adaptiveSync, VkExtent2D windowExtents, VkSurfaceKHR targetSurface);
        [[nodiscard]] ErrorCode createFramebuffersInternal(VkExtent2D windowExtents, VkSurfaceKHR targetSurface);

    private:
        VK_API& _context;
        const VKDevice& _device;
        const DisplayWindow& _window;

        vkb::Swapchain _swapChain{};
        VkRenderPass _renderPass{ nullptr };

        vector<VkSemaphore> _imageAvailableSemaphores;
        vector<VkSemaphore> _renderFinishedSemaphores;
        vector<VkFence> _inFlightFences;
        vector<VkFence> _imagesInFlight;

        vector<VkFramebuffer> _framebuffers;
        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;

        U32 _swapchainImageIndex{ 0u };
        U8 _currentFrameIdx{ 0u };
    };

    FWD_DECLARE_MANAGED_CLASS(VKSwapChain);
}; //namespace Divide
#endif //_VK_SWAP_CHAIN_H_
