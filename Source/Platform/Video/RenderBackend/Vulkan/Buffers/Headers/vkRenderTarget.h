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
#ifndef VK_FRAME_BUFFER_H
#define VK_FRAME_BUFFER_H

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

#include <Vulkan/vulkan_core.h>

namespace Divide
{
    namespace Attorney
    {
        class VKAPIRenderTarget;
    }

    class GFXDevice;
    class vkTexture;

    class vkRenderTarget final : public RenderTarget
    {
        friend class Attorney::VKAPIRenderTarget;

    public:
        vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor);
        ~vkRenderTarget() = default;

        [[nodiscard]] bool create() override;

        PROPERTY_R_IW(VkRenderingInfo, renderingInfo);

    private:
        void begin(VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut);
        void end(VkCommandBuffer cmdBuffer, const RTTransitionMask& mask );
        void blitFrom( VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept;
        void transitionAttachments( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTTransitionMask& transitionMask, bool toWrite );

    private:
        std::array<VkRenderingAttachmentInfo, to_base(RTColourAttachmentSlot::COUNT)> _colourAttachmentInfo{};
        VkRenderingAttachmentInfo _depthAttachmentInfo{};
        
        std::array<VkFormat, to_base( RTColourAttachmentSlot::COUNT )> _colourAttachmentFormats{};

        std::array<VkRenderingAttachmentInfo, to_base( RTColourAttachmentSlot::COUNT )> _stagingColourAttachmentInfo{};
        RTDrawDescriptor _previousPolicy;

        std::array<VkImageSubresourceRange, RT_MAX_ATTACHMENT_COUNT> _subresourceRange{};

        bool _keptMSAAData{false};
    };

    namespace Attorney
    {
        class VKAPIRenderTarget
        {
            static void begin(vkRenderTarget& rt, VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut)
            {
                rt.begin(cmdBuffer, descriptor, clearPolicy, pipelineRenderingCreateInfoOut);
            }
            static void end(vkRenderTarget& rt, VkCommandBuffer cmdBuffer, const RTTransitionMask& mask )
            {
                rt.end(cmdBuffer, mask);
            }
            static void blitFrom( vkRenderTarget& rt, VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept
            {
                rt.blitFrom(cmdBuffer, source, params );
            }
            friend class VK_API;
        };
    };  // namespace Attorney
} //namespace Divide

#endif //VK_FRAME_BUFFER_H