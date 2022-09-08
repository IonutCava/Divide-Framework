#include "stdafx.h"

#include "Headers/vkFramebuffer.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

namespace Divide {
    vkRenderTarget::vkRenderTarget(GFXDevice& context, const RenderTargetDescriptor& descriptor)
        : RenderTarget(context, descriptor)
    {
        renderPassBeginInfo(vk::renderPassBeginInfo());
    }

    vkRenderTarget::~vkRenderTarget()
    {
        destroy();
    }

    void vkRenderTarget::destroy() {
        if (_renderPass != VK_NULL_HANDLE) {
            VK_API::RegisterCustomAPIDelete([renderPass = _renderPass](VkDevice device) {
                vkDestroyRenderPass(device, renderPass, nullptr);
            }, true);
            _renderPass = VK_NULL_HANDLE;
        }
        if (_framebuffer != VK_NULL_HANDLE) {
            VK_API::RegisterCustomAPIDelete([framebuffer = _framebuffer](VkDevice device) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }, true);
            _framebuffer = VK_NULL_HANDLE;
        }
    }

    bool vkRenderTarget::create() {
        if (!RenderTarget::create()) {
            return false;
        }

        destroy();

        std::array<VkAttachmentDescription, RT_MAX_COLOUR_ATTACHMENTS + 1> attachment_desc = {};
        std::array<VkImageView, RT_MAX_COLOUR_ATTACHMENTS + 1> attachment_views = {};
        
        std::array<VkAttachmentReference, RT_MAX_COLOUR_ATTACHMENTS> colour_attachment_ref = {};
        VkAttachmentReference depth_attachment_ref = {};

        U32 maxLayers = 0u;

        U8 colourAttachmentCount = 0u;
        for (U8 i = 0u; i < RT_MAX_COLOUR_ATTACHMENTS; ++i) {
            if (RenderTarget::initAttachment(RTAttachmentType::Colour, i)) {
                vkTexture* vkTex = static_cast<vkTexture*>(getAttachment(RTAttachmentType::Colour, i)->texture().get());
                maxLayers = std::max(maxLayers, vkTex->numLayers());

                vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
                imageViewDescriptor._format = vkTex->vkFormat();
                imageViewDescriptor._layers = { 0u, vkTex->numLayers() };
                imageViewDescriptor._mipLevels = { 0u, 1u };
                imageViewDescriptor._type = vkTex->descriptor().texType();
                imageViewDescriptor._usage = ImageUsage::RT_COLOUR_ATTACHMENT;
                attachment_views[colourAttachmentCount] = vkTex->getImageView(imageViewDescriptor);

                VkAttachmentDescription& attachmentDesc = attachment_desc[colourAttachmentCount];
                attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                attachmentDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                attachmentDesc.format = vkTex->vkFormat();
                attachmentDesc.samples = vkTex->sampleFlagBits();

                //attachment number will index into the pAttachments array in the parent renderpass itself
                colour_attachment_ref[colourAttachmentCount].attachment = colourAttachmentCount;
                colour_attachment_ref[colourAttachmentCount].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                ++colourAttachmentCount;
            }
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = colourAttachmentCount;
        subpass.pColorAttachments = colour_attachment_ref.data();

        VkRenderPassCreateInfo render_pass_info = vk::renderPassCreateInfo();
        render_pass_info.attachmentCount = colourAttachmentCount;

        if (RenderTarget::initAttachment(RTAttachmentType::Depth_Stencil, 0)) {
            render_pass_info.attachmentCount += 1;

            RTAttachment* attachment = getAttachment(RTAttachmentType::Depth_Stencil, 0);
            auto& tex = attachment->texture();
            maxLayers = std::max(maxLayers, tex->numLayers());

            vkTexture* vkTex = static_cast<vkTexture*>(tex.get());

            vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
            imageViewDescriptor._format = vkTex->vkFormat();
            imageViewDescriptor._layers = { 0u, vkTex->numLayers() };
            imageViewDescriptor._mipLevels = { 0u, 1u };
            imageViewDescriptor._type = vkTex->descriptor().texType();
            imageViewDescriptor._usage = ImageUsage::RT_DEPTH_ATTACHMENT;
            attachment_views[colourAttachmentCount] = vkTex->getImageView(imageViewDescriptor);

            VkAttachmentDescription& attachmentDesc = attachment_desc[colourAttachmentCount];
            attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachmentDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachmentDesc.format = vkTex->vkFormat();
            attachmentDesc.samples = vkTex->sampleFlagBits();

            //attachment number will index into the pAttachments array in the parent renderpass itself
            depth_attachment_ref.attachment = colourAttachmentCount;
            depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            subpass.pDepthStencilAttachment = &depth_attachment_ref;
        }

        VkSubpassDependency subpass_dependecy{};
        subpass_dependecy.srcSubpass = 0u;
        subpass_dependecy.dstSubpass = 0u;
        subpass_dependecy.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        subpass_dependecy.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        subpass_dependecy.srcStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        subpass_dependecy.dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

        render_pass_info.pAttachments = attachment_desc.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &subpass_dependecy;

        VK_CHECK(vkCreateRenderPass(VK_API::GetStateTracker()->_device->getVKDevice(), &render_pass_info, nullptr, &_renderPass));

        //create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
        VkFramebufferCreateInfo fb_info = vk::framebufferCreateInfo();

        fb_info.renderPass = _renderPass;
        fb_info.attachmentCount = render_pass_info.attachmentCount;
        fb_info.pAttachments = attachment_views.data();
        fb_info.width = getWidth();
        fb_info.height = getHeight();
        fb_info.layers = maxLayers;
        VK_CHECK(vkCreateFramebuffer(VK_API::GetStateTracker()->_device->getVKDevice(), &fb_info, nullptr, &_framebuffer));

        return true;
    }

    void vkRenderTarget::clear([[maybe_unused]] const RTClearDescriptor& descriptor) noexcept {
    }

    void vkRenderTarget::setDefaultState([[maybe_unused]] const RTDrawDescriptor& drawPolicy) noexcept {
    }

    void vkRenderTarget::readData([[maybe_unused]] const vec4<U16>& rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData) const noexcept {
    }

    void vkRenderTarget::blitFrom([[maybe_unused]] const RTBlitParams& params) noexcept {
    
    }

    const VkRenderPassBeginInfo& vkRenderTarget::getRenderPassInfo() {
        const VkExtent2D targetExtents{ getWidth(), getHeight() };
        _renderPassBeginInfo.renderPass = renderPass();
        _renderPassBeginInfo.renderArea.offset.x = 0;
        _renderPassBeginInfo.renderArea.offset.y = 0;
        _renderPassBeginInfo.renderArea.extent.width = getWidth();
        _renderPassBeginInfo.renderArea.extent.height = getHeight();
        _renderPassBeginInfo.framebuffer = framebuffer();
        _renderPassBeginInfo.clearValueCount = 0u;
        _renderPassBeginInfo.pClearValues = nullptr;

        return _renderPassBeginInfo;
    }
}; //namespace Divide