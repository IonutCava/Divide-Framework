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

#include "VKCommon.h"

namespace Divide {
    class DisplayWindow;
    class VKDevice;

    class VKSwapChain final : NonCopyable, NonMovable {
    public:
        VKSwapChain(const VKDevice& device, const DisplayWindow& window);
        ~VKSwapChain();

        ErrorCode create(VkSurfaceKHR targetSurface);
        void destroy();

        [[nodiscard]] VkResult beginFrame();
        [[nodiscard]] VkResult endFrame(VkQueue queue, VkCommandBuffer& cmdBuffer);

        [[nodiscard]] vkb::Swapchain getSwapChain() const noexcept;
        [[nodiscard]] VkRenderPass   getRenderPass() const noexcept;
        [[nodiscard]] VkFramebuffer  getCurrentFrameBuffer() const noexcept;

    private:
        [[nodiscard]] ErrorCode createSwapChainInternal(VkExtent2D windowExtents, VkSurfaceKHR targetSurface);
        [[nodiscard]] ErrorCode createFramebuffersInternal(VkExtent2D windowExtents, VkSurfaceKHR targetSurface);

        [[nodiscard]] VkResult acquireNextImage();

    private:
        const VKDevice& _device;
        const DisplayWindow& _window;

        vkb::Swapchain _swapChain{};
        VkRenderPass _renderPass{ nullptr };
        VkSemaphore _presentSemaphore{ nullptr }, _renderSemaphore{ nullptr };
        VkFence _renderFence{ nullptr };

        vector<VkFramebuffer> _framebuffers;
        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;

        U32 _swapchainImageIndex{ 0u };
    };
}; //namespace Divide
#endif //_VK_SWAP_CHAIN_H_
