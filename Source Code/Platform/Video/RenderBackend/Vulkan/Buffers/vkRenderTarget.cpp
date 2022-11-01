#include "stdafx.h"

#include "Headers/vkRenderTarget.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

namespace Divide
{
    vkRenderTarget::vkRenderTarget( GFXDevice& context, const RenderTargetDescriptor& descriptor )
        : RenderTarget( context, descriptor )
    {
        _renderingInfo = {};
        _renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

        for ( auto& info : _colourAttachmentInfo )
        {
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        }

        auto& info = _depthAttachmentInfo;
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    }

    bool vkRenderTarget::create()
    {
        if ( RenderTarget::create() )
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

    void vkRenderTarget::readData( [[maybe_unused]] const vec4<U16> rect, [[maybe_unused]] GFXImageFormat imageFormat, [[maybe_unused]] GFXDataFormat dataType, [[maybe_unused]] std::pair<bufferPtr, size_t> outData ) const noexcept
    {
    }

    void vkRenderTarget::blitFrom( VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( source == nullptr || !IsValid( params ) )
        {
            return;
        }
        vkRenderTarget* input = source;
        vkRenderTarget* output = this;
        const vec2<U16> inputDim = input->_descriptor._resolution;
        const vec2<U16> outputDim = output->_descriptor._resolution;

        //bool blittedDepth = false;
        //bool readBufferDirty = false;
        //const bool depthMismatch = input->_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] != output->_attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX];

        // Multiple attachments, multiple layers, multiple everything ... what a mess ... -Ionut
        if ( IsValid( params._blitColours ) )
        {
            PROFILE_SCOPE( "Blit Colours", Profiler::Category::Graphics );
            /*vkCmdBlitImage(cmdBuffer,
                VkImage                                     srcImage,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VkImage                                     dstImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                uint32_t                                    regionCount,
                const VkImageBlit * pRegions,
                VkFilter                                    filter );*/
        }
    }

    void vkRenderTarget::transitionAttachments( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const bool toWrite )
    {
        static std::array<VkImageMemoryBarrier2, to_base( RTColourAttachmentSlot::COUNT ) + 1> memBarriers{};
        U8 memBarrierCount = 0u;

        const bool needLayeredColour = descriptor._writeLayers._depthLayer != INVALID_LAYER_INDEX;
        U16 targetColourLayer = needLayeredColour ? descriptor._writeLayers._depthLayer : 0u;

        bool needLayeredDepth = false;
        U16 targetDepthLayer = 0u;
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( descriptor._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX )
            {
                needLayeredDepth = true;
                targetDepthLayer = descriptor._writeLayers._colourLayers[i];
                break;
            }
        }

        VkImageMemoryBarrier2 memBarrier{};
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( _attachmentsUsed[i] && IsEnabled( descriptor._drawMask, RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i) ) )
            {
                vkTexture* vkTex = static_cast<vkTexture*>(_attachments[i]->texture().get());

                ImageView targetView = vkTex->getView();
                if ( descriptor._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX || needLayeredColour )
                {
                    targetColourLayer = descriptor._writeLayers._colourLayers[i] == INVALID_LAYER_INDEX ? targetColourLayer : descriptor._writeLayers._colourLayers[i];
                    targetView._subRange._layerRange = { targetColourLayer, 1u };
                }
                else if ( descriptor._mipWriteLevel != U16_MAX )
                {
                    targetView._subRange._mipLevels =  { descriptor._mipWriteLevel, 1u };
                }
                if ( IsCubeTexture( vkTex->descriptor().texType() ) )
                {
                    targetView._subRange._layerRange.offset /= 6u;
                }

                ImageUsage targetUsage = ImageUsage::UNDEFINED;
                if ( toWrite )
                {
                    _attachmentsPreviousUsage[i] = _attachments[i]->texture()->imageUsage( targetView._subRange );
                    targetUsage = ImageUsage::RT_COLOUR_ATTACHMENT;
                }
                else
                {
                    targetUsage = _attachmentsPreviousUsage[i];
                }

                if ( vkTex->transitionLayout( targetView._subRange, targetUsage, memBarrier ) )
                {
                    memBarriers[memBarrierCount++] = memBarrier;
                }
            }
        }

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && IsEnabled( descriptor._drawMask, RTAttachmentType::DEPTH ) )
        {
            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];
            vkTexture* vkTex = static_cast<vkTexture*>(att->texture().get());

            ImageView targetView = vkTex->getView();
            if ( descriptor._writeLayers._depthLayer != INVALID_LAYER_INDEX || needLayeredDepth )
            {
                targetDepthLayer = descriptor._writeLayers._depthLayer == INVALID_LAYER_INDEX ? targetDepthLayer : descriptor._writeLayers._depthLayer;
                targetView._subRange._layerRange = { targetDepthLayer, 1u };
            }
            else if ( descriptor._mipWriteLevel != U16_MAX )
            {
                targetView._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
            }
            if ( IsCubeTexture( vkTex->descriptor().texType() ) )
            {
                targetView._subRange._layerRange.offset /= 6u;
            }

            ImageUsage targetUsage = ImageUsage::UNDEFINED;
            if ( toWrite )
            {
                _attachmentsPreviousUsage[RT_DEPTH_ATTACHMENT_IDX] = _attachments[RT_DEPTH_ATTACHMENT_IDX]->texture()->imageUsage();
                targetUsage = att->descriptor()._type == RTAttachmentType::DEPTH_STENCIL ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT : ImageUsage::RT_DEPTH_ATTACHMENT;
            }
            else
            {
                targetUsage = _attachmentsPreviousUsage[RT_DEPTH_ATTACHMENT_IDX];
            }

            if ( vkTex->transitionLayout( targetView._subRange, targetUsage, memBarrier ) )
            {
                memBarriers[memBarrierCount++] = memBarrier;
            }
        }

        if ( memBarrierCount > 0u )
        {
            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = memBarrierCount;
            dependencyInfo.pImageMemoryBarriers = memBarriers.data();

            vkCmdPipelineBarrier2( cmdBuffer, &dependencyInfo );
        }
    }


    void vkRenderTarget::begin( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut )
    {
        _previousPolicy = descriptor;

        pipelineRenderingCreateInfoOut = {};
        pipelineRenderingCreateInfoOut.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

        const bool needLayeredColour = descriptor._writeLayers._depthLayer != INVALID_LAYER_INDEX;
        U16 targetColourLayer = needLayeredColour ? descriptor._writeLayers._depthLayer : 0u;

        bool needLayeredDepth = false;
        U16 targetDepthLayer = 0u;
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( descriptor._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX )
            {
                needLayeredDepth = true;
                targetDepthLayer = descriptor._writeLayers._colourLayers[i];
                break;
            }
        }

        vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
        imageViewDescriptor._subRange = {};

        U8 stagingIndex = 0u;
        VkImageMemoryBarrier2 memBarrier{};
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( _attachmentsUsed[i] && IsEnabled( descriptor._drawMask, RTAttachmentType::COLOUR, static_cast<RTColourAttachmentSlot>(i) ) )
            {
                vkTexture* vkTex = static_cast<vkTexture*>(_attachments[i]->texture().get());
                imageViewDescriptor._subRange = vkTex->getView()._subRange;
                if ( descriptor._writeLayers._colourLayers[i] != INVALID_LAYER_INDEX || needLayeredColour )
                {
                    targetColourLayer = descriptor._writeLayers._colourLayers[i] == INVALID_LAYER_INDEX ? targetColourLayer : descriptor._writeLayers._colourLayers[i];
                    imageViewDescriptor._subRange._layerRange = { targetColourLayer, 1u };
                }
                else if ( descriptor._mipWriteLevel != U16_MAX )
                {
                    imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                }

                imageViewDescriptor._format = vkTex->vkFormat();
                imageViewDescriptor._type = imageViewDescriptor._subRange._layerRange.count > 1u ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
                imageViewDescriptor._usage = ImageUsage::RT_COLOUR_ATTACHMENT;

                VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];
                info.imageView = vkTex->getImageView( imageViewDescriptor );
                _colourAttachmentFormats[stagingIndex] = vkTex->vkFormat();
                if ( clearPolicy._clearColourDescriptors[i]._index != RTColourAttachmentSlot::COUNT || !_attachmentsUsed[i] )
                {
                    info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
                    info.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
                    info.clearValue = {};
                }

                _stagingColourAttachmentInfo[stagingIndex] = info;
            }
            else
            {
                _colourAttachmentFormats[stagingIndex] = VK_FORMAT_UNDEFINED;
                _stagingColourAttachmentInfo[stagingIndex] = {};
                _stagingColourAttachmentInfo[stagingIndex].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            }

            ++stagingIndex;
        }

        pipelineRenderingCreateInfoOut.colorAttachmentCount = stagingIndex;
        pipelineRenderingCreateInfoOut.pColorAttachmentFormats = _colourAttachmentFormats.data();

        _renderingInfo.colorAttachmentCount = stagingIndex;
        _renderingInfo.pColorAttachments = _stagingColourAttachmentInfo.data();

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] && IsEnabled( descriptor._drawMask, RTAttachmentType::DEPTH ) )
        {
            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];
            const bool hasStencil = att->descriptor()._type == RTAttachmentType::DEPTH_STENCIL;

            vkTexture* vkTex = static_cast<vkTexture*>(att->texture().get());
            imageViewDescriptor._subRange = vkTex->getView()._subRange;
            if ( descriptor._writeLayers._depthLayer != INVALID_LAYER_INDEX || needLayeredDepth )
            {
                targetDepthLayer = descriptor._writeLayers._depthLayer == INVALID_LAYER_INDEX ? targetDepthLayer : descriptor._writeLayers._depthLayer;
                imageViewDescriptor._subRange._layerRange = { targetDepthLayer, 1u };
            }
            else if ( descriptor._mipWriteLevel != U16_MAX )
            {
                imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
            }


            imageViewDescriptor._format = vkTex->vkFormat();
            imageViewDescriptor._type = imageViewDescriptor._subRange._layerRange.count > 1u ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
            imageViewDescriptor._usage = hasStencil ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT : ImageUsage::RT_DEPTH_ATTACHMENT;

            _depthAttachmentInfo.imageView = vkTex->getImageView( imageViewDescriptor );

            if ( clearPolicy._clearDepth )
            {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                _depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                _depthAttachmentInfo.clearValue.depthStencil.depth = clearPolicy._clearDepthValue;
            }
            else
            {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                _depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
                _depthAttachmentInfo.clearValue.depthStencil.depth = 1.f;
            }

            pipelineRenderingCreateInfoOut.depthAttachmentFormat = vkTex->vkFormat();
            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }
        else
        {
            pipelineRenderingCreateInfoOut.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
            _renderingInfo.pDepthAttachment = nullptr;
        }

        transitionAttachments( cmdBuffer, descriptor, true );
        vkCmdBeginRendering( cmdBuffer, &_renderingInfo );
    }

    void vkRenderTarget::end( VkCommandBuffer cmdBuffer )
    {
        vkCmdEndRendering( cmdBuffer );
        transitionAttachments( cmdBuffer, _previousPolicy, false );
    }

}; //namespace Divide