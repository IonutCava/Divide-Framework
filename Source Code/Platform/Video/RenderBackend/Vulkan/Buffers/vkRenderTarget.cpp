#include "stdafx.h"

#include "Headers/vkRenderTarget.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

namespace Divide {
    vkRenderTarget::vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
        : RenderTarget(context, descriptor)
    {
        _renderingInfo = {};
        _renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

        for (auto& info : _colourAttachmentInfo)
        {
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        auto& info = _depthAttachmentInfo;
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        for (VkImageMemoryBarrier2& barrier : _memBarriers)
        {
            barrier = vk::imageMemoryBarrier2();
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        }
    }

    bool vkRenderTarget::create()
    {
        if (RenderTarget::create())
        {
            _renderingInfo.renderArea.offset.x = 0;
            _renderingInfo.renderArea.offset.y = 0;
            _renderingInfo.renderArea.extent.width = getWidth();
            _renderingInfo.renderArea.extent.height = getHeight();
            _renderingInfo.layerCount = 1u;
            return true;
        }

        return false;
    }

    void vkRenderTarget::readData([[maybe_unused]] const vec4<U16> rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept
    {
    }

    void vkRenderTarget::blitFrom(RenderTarget* source, [[maybe_unused]] const RTBlitParams& params) noexcept
    {
    }

    void vkRenderTarget::begin(VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut)
    {
        _previousPolicy = descriptor;

        transitionAttachments(cmdBuffer, descriptor, true);

        pipelineRenderingCreateInfoOut = {};
        pipelineRenderingCreateInfoOut.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

        VkClearValue clearValue{};
        clearValue.color =
        {
            DefaultColours::DIVIDE_BLUE.r,
            DefaultColours::DIVIDE_BLUE.g,
            DefaultColours::DIVIDE_BLUE.b,
            DefaultColours::DIVIDE_BLUE.a
        };

        vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
        imageViewDescriptor._layers = { 0u, 1u };
        imageViewDescriptor._mipLevels = { 0u, 1u };

        U8 stagingIndex = 0u;
        for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if (_attachmentsUsed[i])
            {
                VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];

                vkTexture* vkTex = static_cast<vkTexture*>(_attachments[i]->texture().get());
                imageViewDescriptor._format = vkTex->vkFormat();
                imageViewDescriptor._type = vkTex->descriptor().texType();
                imageViewDescriptor._usage = vkTex->imageUsage();

                info.imageView = vkTex->getImageView(imageViewDescriptor);

                if (clearPolicy._clearColourDescriptors[i]._index != RTColourAttachmentSlot::COUNT )
                {
                    info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    info.clearValue.color = {
                        clearPolicy._clearColourDescriptors[i]._colour.r,
                        clearPolicy._clearColourDescriptors[i]._colour.g,
                        clearPolicy._clearColourDescriptors[i]._colour.b,
                        clearPolicy._clearColourDescriptors[i]._colour.a
                    };
                }
                else
                {
                    info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    info.clearValue = {};
                }

                _stagingColourAttachmentInfo[stagingIndex] = info;
                _colourAttachmentFormats[stagingIndex] = vkTex->vkFormat();
                ++stagingIndex;
            }
        }

        pipelineRenderingCreateInfoOut.colorAttachmentCount = stagingIndex;
        pipelineRenderingCreateInfoOut.pColorAttachmentFormats = _colourAttachmentFormats.data();

        _renderingInfo.colorAttachmentCount = stagingIndex;
        _renderingInfo.pColorAttachments = _stagingColourAttachmentInfo.data();

        if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX])
        {
            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];

            vkTexture* vkTex = static_cast<vkTexture*>(att->texture().get());
            pipelineRenderingCreateInfoOut.depthAttachmentFormat = vkTex->vkFormat();

            imageViewDescriptor._format = vkTex->vkFormat();
            imageViewDescriptor._type = vkTex->descriptor().texType();
            imageViewDescriptor._usage = vkTex->imageUsage();
            _depthAttachmentInfo.imageView = vkTex->getImageView(imageViewDescriptor);

            if (clearPolicy._clearDepth)
            {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                _depthAttachmentInfo.clearValue.depthStencil.depth = clearPolicy._clearDepthValue;
            }
            else
            {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                _depthAttachmentInfo.clearValue.depthStencil.depth = 1.f;
            }

            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }

        vkCmdBeginRendering(cmdBuffer, &_renderingInfo);
    }

    void vkRenderTarget::end(VkCommandBuffer cmdBuffer)
    {
        vkCmdEndRendering(cmdBuffer);
        transitionAttachments(cmdBuffer, _previousPolicy, false);
    }

    void vkRenderTarget::transitionAttachments(VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const bool toWrite)
    {
        const auto populateBarrier = [](VkImageMemoryBarrier2& memBarrier, const bool prepareForWrite, const bool isDepth, const bool hasStencil)
        {
            constexpr VkPipelineStageFlags2 PIPELINE_FRAGMENT_TEST_BITS = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            const     VkImageLayout         DEPTH_ATTACHMENT_LAYOUT = hasStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

            if ( prepareForWrite )
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.dstAccessMask = isDepth ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
                memBarrier.newLayout     = isDepth ? DEPTH_ATTACHMENT_LAYOUT : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                memBarrier.srcStageMask  = isDepth ? PIPELINE_FRAGMENT_TEST_BITS : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                memBarrier.dstStageMask  = isDepth ? PIPELINE_FRAGMENT_TEST_BITS : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            else
            {
                memBarrier.srcAccessMask = isDepth ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.oldLayout     = isDepth ? DEPTH_ATTACHMENT_LAYOUT : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                memBarrier.srcStageMask  = isDepth ? PIPELINE_FRAGMENT_TEST_BITS : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            }
        };

        U8 stagingIndex = 0u;
        for (U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT); ++i )
        {
            if (_attachmentsUsed[i])
            {
                const auto& att = _attachments[i];
                const bool prepareForWrite = IsEnabled( descriptor._drawMask, RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i) ) && toWrite;
                if (att->setImageUsage( prepareForWrite ? ImageUsage::RT_COLOUR_ATTACHMENT : ImageUsage::SHADER_SAMPLE ))
                {
                    VkImageMemoryBarrier2& memBarrier = _memBarriers[stagingIndex++];
                    populateBarrier( memBarrier, prepareForWrite, false, false);
                    memBarrier.subresourceRange.aspectMask = vkTexture::GetAspectFlags(att->texture()->descriptor());
                    memBarrier.image = static_cast<vkTexture*>(att->texture().get())->image()->_image;
                }
            }
        }

        if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX])
        {
            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];

            const bool prepareForWrite = IsEnabled( descriptor._drawMask, RTAttachmentType::DEPTH ) && toWrite;
            if (att->setImageUsage( prepareForWrite ? ImageUsage::RT_DEPTH_ATTACHMENT : ImageUsage::SHADER_SAMPLE))
            {
                VkImageMemoryBarrier2& memBarrier = _memBarriers[stagingIndex++];
                populateBarrier(memBarrier, prepareForWrite, true, att->descriptor()._type == RTAttachmentType::DEPTH_STENCIL);
                memBarrier.subresourceRange.aspectMask = vkTexture::GetAspectFlags(att->texture()->descriptor());
                memBarrier.image = static_cast<vkTexture*>(att->texture().get())->image()->_image;
            }
        }

        if (stagingIndex > 0u)
        {
            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = stagingIndex;
            dependencyInfo.pImageMemoryBarriers = _memBarriers.data();

            vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
        }
    }
}; //namespace Divide