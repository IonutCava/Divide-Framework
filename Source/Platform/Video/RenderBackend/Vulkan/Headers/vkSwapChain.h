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
#ifndef DVD_VK_SWAP_CHAIN_H_
#define DVD_VK_SWAP_CHAIN_H_

#include "vkResources.h"

namespace Divide {
    class DisplayWindow;
    class VK_API;
    class VKDevice;
    class GFXDevice;

    struct Fence
    {
        VkFence _handle;
        U8 _tag{U8_MAX};
    };

    struct FrameData
    {
        VkSemaphore _presentSemaphore{VK_NULL_HANDLE};
        VkCommandBuffer _commandBuffer{VK_NULL_HANDLE};
        Fence _renderFence;
    };

    class VKSwapChain final : NonCopyable, NonMovable {
    public:
        VKSwapChain(VK_API& context, const VKDevice& device, const DisplayWindow& window);
        ~VKSwapChain();

        ErrorCode create(bool vSync, bool adaptiveSync, VkSurfaceKHR targetSurface);
        void destroy();

        [[nodiscard]] VkResult beginFrame();
        [[nodiscard]] VkResult endFrame();

        [[nodiscard]] vkb::Swapchain&  getSwapChain() noexcept;
        [[nodiscard]] VkImage          getCurrentImage() const noexcept;
        [[nodiscard]] VkImageView      getCurrentImageView() const noexcept;

        [[nodiscard]] const FrameData& getFrameData() const noexcept;

        PROPERTY_R_IW(VkExtent2D, surfaceExtent);
        
    private:
        VK_API& _context;
        const VKDevice& _device;
        const DisplayWindow& _window;

        FrameData* _activeFrame{ nullptr };
        vector<FrameData> _frames;
        vkb::Swapchain _swapChain{};


        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;
        vector<VkSemaphore> _renderSemaphores;
        U32 _swapchainImageIndex{ 0u };
    };

    FWD_DECLARE_MANAGED_CLASS(VKSwapChain);
}; //namespace Divide

#endif //DVD_VK_SWAP_CHAIN_H_
