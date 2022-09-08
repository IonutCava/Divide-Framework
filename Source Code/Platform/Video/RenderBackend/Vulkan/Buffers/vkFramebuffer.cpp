#include "stdafx.h"

#include "Headers/vkFramebuffer.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

namespace Divide {
    vkRenderTarget::vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
        : RenderTarget(context, descriptor)
    {
        _renderingInfo = {};
        _renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

        for (auto& info : _colourAttachmentInfo) {
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }

        auto& info = _depthAttachmentInfo;
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    vkRenderTarget::~vkRenderTarget()
    {
    }

    void vkRenderTarget::clear([[maybe_unused]] const RTClearDescriptor& descriptor) noexcept {
    }

    void vkRenderTarget::setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) noexcept {
    }

    void vkRenderTarget::readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept {
    }

    void vkRenderTarget::blitFrom([[maybe_unused]] const RTBlitParams& params) noexcept {
    
    }

    const VkRenderingInfo& vkRenderTarget::getRenderingInfo(const RTDrawDescriptor& descriptor, VkPipelineRenderingCreateInfo& pipelineCreateInfoOut) {

        pipelineCreateInfoOut = {};
        pipelineCreateInfoOut.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

        VkClearValue clearValue{};
        clearValue.color = {
            DefaultColours::DIVIDE_BLUE.r,
            DefaultColours::DIVIDE_BLUE.g,
            DefaultColours::DIVIDE_BLUE.b,
            DefaultColours::DIVIDE_BLUE.a
        };

        U8 stagingIndex = 0u;
        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
            if (_attachmentsUsed[i]) {
                VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];

                vkTexture* vkTex = static_cast<vkTexture*>(getAttachment(RTAttachmentType::Colour, i)->texture().get());
                vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
                imageViewDescriptor._format = vkTex->vkFormat();
                imageViewDescriptor._layers = { 0u, 1u };
                imageViewDescriptor._mipLevels = { 0u, 1u };
                imageViewDescriptor._type = vkTex->descriptor().texType();
                imageViewDescriptor._usage = ImageUsage::RT_COLOUR_ATTACHMENT;
                info.imageView = vkTex->getImageView(imageViewDescriptor);
                info.clearValue = clearValue;

                _stagingColourAttachmentInfo[stagingIndex] = info;
                _colourAttachmentFormats[stagingIndex] = vkTex->vkFormat();
                ++stagingIndex;
            }
        }

        pipelineCreateInfoOut.colorAttachmentCount = stagingIndex;
        pipelineCreateInfoOut.pColorAttachmentFormats = _colourAttachmentFormats.data();

        const VkExtent2D targetExtents{ getWidth(), getHeight() };
        _renderingInfo.renderArea.offset.x = 0;
        _renderingInfo.renderArea.offset.y = 0;
        _renderingInfo.renderArea.extent.width = getWidth();
        _renderingInfo.renderArea.extent.height = getHeight();
        _renderingInfo.colorAttachmentCount = stagingIndex;
        _renderingInfo.pColorAttachments = _stagingColourAttachmentInfo.data();
        _renderingInfo.layerCount = 1u;
        if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX]) {
            vkTexture* vkTex = static_cast<vkTexture*>(getAttachment(RTAttachmentType::Depth_Stencil, 0)->texture().get());
            vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
            imageViewDescriptor._format = vkTex->vkFormat();
            imageViewDescriptor._layers = { 0u, 1u };
            imageViewDescriptor._mipLevels = { 0u, 1u };
            imageViewDescriptor._type = vkTex->descriptor().texType();
            imageViewDescriptor._usage = ImageUsage::RT_DEPTH_ATTACHMENT;
            _depthAttachmentInfo.imageView = vkTex->getImageView(imageViewDescriptor);
            _depthAttachmentInfo.clearValue.depthStencil.depth = 1.f;
            pipelineCreateInfoOut.depthAttachmentFormat = vkTex->vkFormat();
            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }

        return _renderingInfo;
    }
}; //namespace Divide