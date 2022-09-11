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

    bool vkRenderTarget::create() {
        if (RenderTarget::create()) {
            _renderingInfo.renderArea.offset.x = 0;
            _renderingInfo.renderArea.offset.y = 0;
            _renderingInfo.renderArea.extent.width = getWidth();
            _renderingInfo.renderArea.extent.height = getHeight();
            _renderingInfo.layerCount = 1u;
            return true;
        }

        return false;
    }

    void vkRenderTarget::readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept {
    }

    void vkRenderTarget::blitFrom(RenderTarget* source, [[maybe_unused]] const RTBlitParams& params) noexcept {
    }

    const VkRenderingInfo& vkRenderTarget::getRenderingInfo(const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineCreateInfoOut) {

        pipelineCreateInfoOut = {};
        pipelineCreateInfoOut.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

        VkClearValue clearValue{};
        clearValue.color = {
            DefaultColours::DIVIDE_BLUE.r,
            DefaultColours::DIVIDE_BLUE.g,
            DefaultColours::DIVIDE_BLUE.b,
            DefaultColours::DIVIDE_BLUE.a
        };

        vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
        imageViewDescriptor._layers = { 0u, 1u };
        imageViewDescriptor._mipLevels = { 0u, 1u };

        U8 stagingIndex = 0u;
        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
            if (_attachmentsUsed[i]) {
                VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];

                vkTexture* vkTex = static_cast<vkTexture*>(getAttachment(RTAttachmentType::Colour, i)->texture().get());
                imageViewDescriptor._format = vkTex->vkFormat();
                imageViewDescriptor._type = vkTex->descriptor().texType();
                imageViewDescriptor._usage = ImageUsage::RT_COLOUR_ATTACHMENT;

                info.imageView = vkTex->getImageView(imageViewDescriptor);

                if (clearPolicy._clearColourDescriptors[i]._index != MAX_RT_COLOUR_ATTACHMENTS) {
                    info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    info.clearValue.color = {
                        clearPolicy._clearColourDescriptors[i]._colour.r,
                        clearPolicy._clearColourDescriptors[i]._colour.g,
                        clearPolicy._clearColourDescriptors[i]._colour.b,
                        clearPolicy._clearColourDescriptors[i]._colour.a
                    };
                } else {
                    info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    info.clearValue = {};
                }

                _stagingColourAttachmentInfo[stagingIndex] = info;
                _colourAttachmentFormats[stagingIndex] = vkTex->vkFormat();
                ++stagingIndex;
            }
        }

        pipelineCreateInfoOut.colorAttachmentCount = stagingIndex;
        pipelineCreateInfoOut.pColorAttachmentFormats = _colourAttachmentFormats.data();

        _renderingInfo.colorAttachmentCount = stagingIndex;
        _renderingInfo.pColorAttachments = _stagingColourAttachmentInfo.data();
        if (_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX]) {
            vkTexture* vkTex = static_cast<vkTexture*>(getAttachment(RTAttachmentType::Depth_Stencil, 0)->texture().get());
            pipelineCreateInfoOut.depthAttachmentFormat = vkTex->vkFormat();

            imageViewDescriptor._format = vkTex->vkFormat();
            imageViewDescriptor._type = vkTex->descriptor().texType();
            imageViewDescriptor._usage = ImageUsage::RT_DEPTH_ATTACHMENT;
            _depthAttachmentInfo.imageView = vkTex->getImageView(imageViewDescriptor);

            if (clearPolicy._clearDepth) {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                _depthAttachmentInfo.clearValue.depthStencil.depth = clearPolicy._clearDepthValue;
            } else {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                _depthAttachmentInfo.clearValue.depthStencil.depth = 1.f;
            }

            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }

        return _renderingInfo;
    }
}; //namespace Divide