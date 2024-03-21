

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

    void vkRenderTarget::blitFrom( VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        if ( source == nullptr || !IsValid(params) )
        {
            return;
        }

        VK_API::PushDebugMessage(cmdBuffer, "vkRrenderTarget::blitFrom");

        vkRenderTarget* input = source;
        vkRenderTarget* output = this;
        const vec2<U16> inputDim = input->_descriptor._resolution;
        const vec2<U16> outputDim = output->_descriptor._resolution;

        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        for ( const RTBlitEntry entry : params )
        {
            if ( entry._input._index == INVALID_INDEX ||
                 entry._output._index == INVALID_INDEX ||
                 !input->_attachmentsUsed[entry._input._index] ||
                 !output->_attachmentsUsed[entry._output._index] )
            {
                continue;
            }

            PROFILE_SCOPE( "Blit Entry Loop", Profiler::Category::Graphics );

            std::array<VkImageMemoryBarrier2, 2> imageBarriers{};
            U8 imageBarrierCount = 0u;

            const RTAttachment_uptr& inAtt = input->_attachments[entry._input._index];
            const RTAttachment_uptr& outAtt = output->_attachments[entry._output._index];

            vkTexture* vkTexIn = static_cast<vkTexture*>(inAtt->texture().get());
            vkTexture* vkTexOut = static_cast<vkTexture*>(outAtt->texture().get());

            const bool isDepthTextureIn = IsDepthTexture( vkTexIn->descriptor().packing() );
            const bool isDepthTextureOut = IsDepthTexture( vkTexOut->descriptor().packing() );

            U16 layerCount = entry._layerCount;
            DIVIDE_ASSERT(layerCount != U16_MAX && entry._mipCount != U16_MAX );
            if ( IsCubeTexture( vkTexIn->descriptor().texType() ) )
            {
                layerCount *= 6u;
            }

            const VkImageSubresourceRange subResourceIn = {
                  .aspectMask = vkTexture::GetAspectFlags( vkTexIn->descriptor() ),
                  .baseMipLevel = entry._input._mipOffset,
                  .levelCount = entry._mipCount,
                  .baseArrayLayer = entry._input._layerOffset,
                  .layerCount = layerCount
            };
            const VkImageSubresourceRange subResourceOut = {
                  .aspectMask = vkTexture::GetAspectFlags( vkTexOut->descriptor() ),
                  .baseMipLevel = entry._output._mipOffset,
                  .levelCount = entry._mipCount,
                  .baseArrayLayer = entry._output._layerOffset,
                  .layerCount = layerCount
            };
            {
                const vkTexture::TransitionType sourceTransition = isDepthTextureIn ? vkTexture::TransitionType::SHADER_READ_TO_BLIT_READ_DEPTH : vkTexture::TransitionType::SHADER_READ_TO_BLIT_READ_COLOUR;
                const vkTexture::TransitionType targetTransition = isDepthTextureOut ? vkTexture::TransitionType::SHADER_READ_TO_BLIT_WRITE_DEPTH : vkTexture::TransitionType::SHADER_READ_TO_BLIT_WRITE_COLOUR;

                vkTexture::TransitionTexture( sourceTransition, subResourceIn, vkTexIn->image()->_image, imageBarriers[imageBarrierCount++] );
                vkTexture::TransitionTexture( targetTransition, subResourceOut, vkTexOut->image()->_image, imageBarriers[imageBarrierCount++] );
            }

            const U16 srcDepth = vkTexIn->descriptor().texType() == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;
            const U16 dstDepth = vkTexOut->descriptor().texType() == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;

            if ( imageBarrierCount > 0u )
            {
                dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
                dependencyInfo.pImageMemoryBarriers = imageBarriers.data();

                VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                imageBarrierCount = 0u;
            }

            VkImageBlit2 blitRegions[MAX_BLIT_ENTRIES] = {};
            U8 blitRegionCount = 0u;


            for ( U8 mip = 0u; mip < entry._mipCount; ++mip )
            {
                const VkImageSubresourceLayers srcSubResource = {
                    .aspectMask = VkImageAspectFlags( entry._input._index == RT_DEPTH_ATTACHMENT_IDX ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT ),
                    .mipLevel = to_U32( entry._input._mipOffset + mip ),
                    .baseArrayLayer = entry._input._layerOffset,
                    .layerCount = layerCount,
                };

                const VkImageSubresourceLayers dstSubResource = {
                    .aspectMask = VkImageAspectFlags( entry._output._index == RT_DEPTH_ATTACHMENT_IDX ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT ),
                    .mipLevel = to_U32( entry._output._mipOffset + mip ),
                    .baseArrayLayer = entry._output._layerOffset,
                    .layerCount = layerCount,
                };

                VkImageBlit2& blitRegion = blitRegions[blitRegionCount++];

                blitRegion = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                    .srcSubresource = srcSubResource,
                    .dstSubresource = dstSubResource,
                };

                blitRegion.srcOffsets[0] = blitRegion.dstOffsets[0] = {
                    .x = 0,
                    .y = 0,
                    .z = 0
                };

                blitRegion.srcOffsets[1] = {
                    .x = vkTexIn->width(),
                    .y = vkTexIn->height(),
                    .z = srcDepth
                };

                blitRegion.dstOffsets[1] = {
                    .x = vkTexOut->width(),
                    .y = vkTexOut->height(),
                    .z = dstDepth
                };
            }

            if ( blitRegionCount > 0u )
            {
                const VkBlitImageInfo2 blitInfo = {
                    .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                    .srcImage = vkTexIn->image()->_image,
                    .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .dstImage = vkTexOut->image()->_image,
                    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .regionCount = blitRegionCount,
                    .pRegions = blitRegions,
                    .filter = VK_FILTER_NEAREST,
                };

                VK_PROFILE( vkCmdBlitImage2, cmdBuffer, &blitInfo );
            }

            {
                const vkTexture::TransitionType sourceTransition = isDepthTextureIn ? vkTexture::TransitionType::BLIT_READ_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::BLIT_READ_TO_SHADER_READ_COLOUR;
                const vkTexture::TransitionType targetTransition = isDepthTextureOut ? vkTexture::TransitionType::BLIT_WRITE_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::BLIT_WRITE_TO_SHADER_READ_COLOUR;

                vkTexture::TransitionTexture( sourceTransition, subResourceIn, vkTexIn->image()->_image, imageBarriers[imageBarrierCount++] );
                vkTexture::TransitionTexture( targetTransition, subResourceOut, vkTexOut->image()->_image, imageBarriers[imageBarrierCount++] );
            }

            if ( imageBarrierCount > 0u )
            {
                dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
                dependencyInfo.pImageMemoryBarriers = imageBarriers.data();

                VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                imageBarrierCount = 0u;
            }
        }

        VK_API::PopDebugMessage( cmdBuffer );
    }

    void vkRenderTarget::transitionAttachments( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTTransitionMask& transitionMask, const bool toWrite )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        DIVIDE_ASSERT( descriptor._mipWriteLevel != INVALID_INDEX );

        // Double the number of barriers needed in case we have an MSAA RenderTarget (we need to transition the resolve targets as well)
        thread_local std::array<VkImageMemoryBarrier2, RT_MAX_ATTACHMENT_COUNT * 2> memBarriers{};
        U8 memBarrierCount = 0u;
        thread_local VkDependencyInfo dependencyInfo = vk::dependencyInfo();

        const DrawLayerEntry& srcDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
        DrawLayerEntry targetDepthLayer{};

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( descriptor._writeLayers[i]._layer != INVALID_INDEX && (descriptor._writeLayers[i]._layer > 0u || descriptor._writeLayers[i]._cubeFace > 0u) )
            {
                targetDepthLayer = descriptor._writeLayers[i];
                break;
            }
        }

        {
            PROFILE_SCOPE( "Colour Attachments", Profiler::Category::Graphics);
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
            {
                if ( _attachmentsUsed[i] && descriptor._drawMask[i] )
                {
                    const auto& att = _attachments[i];
                    RTAttachment::Layout usage = att->_attachmentUsage;
                    if ( (toWrite && usage == RTAttachment::Layout::ATTACHMENT) ||
                         (!toWrite && usage == RTAttachment::Layout::SHADER_READ) )
                    {
                        continue;
                    }

                    vkTexture* vkTexRender = static_cast<vkTexture*>(att->renderTexture().get());

                    ImageView targetView = vkTexRender->getView();
                    
                    const DrawLayerEntry targetColourLayer = descriptor._writeLayers[i]._layer == INVALID_INDEX ? targetDepthLayer : descriptor._writeLayers[i];

                    if ( IsCubeTexture( vkTexRender->descriptor().texType() ) )
                    {
                        targetView._subRange._layerRange = { to_U16(targetColourLayer._cubeFace + (targetColourLayer._layer * 6u)), descriptor._layeredRendering ? U16_MAX : U16_ONE };
                    }
                    else
                    {
                        assert( targetColourLayer._cubeFace == 0u );
                        targetView._subRange._layerRange = { targetColourLayer._layer, descriptor._layeredRendering ? U16_MAX : U16_ONE };
                    }

                    if ( descriptor._mipWriteLevel > 0u )
                    {
                        targetView._subRange._mipLevels =  { descriptor._mipWriteLevel, 1u };
                    }

                    const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[i];
                    vkTexture::TransitionType targetTransition = toWrite ? vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ;

                    if (toWrite )
                    {
                        _subresourceRange[i].aspectMask = vkTexture::GetAspectFlags( vkTexRender->descriptor() );

                        if ( usage == RTAttachment::Layout::UNDEFINED )
                        {
                            targetTransition = vkTexture::TransitionType::UNDEFINED_TO_COLOUR_ATTACHMENT;

                            _subresourceRange[i].baseMipLevel = 0u;
                            _subresourceRange[i].levelCount = VK_REMAINING_MIP_LEVELS;
                            _subresourceRange[i].baseArrayLayer = 0u;
                            _subresourceRange[i].layerCount = VK_REMAINING_ARRAY_LAYERS;
                        }
                        else
                        {
                            _subresourceRange[i].baseMipLevel = targetView._subRange._mipLevels._offset;
                            _subresourceRange[i].levelCount = targetView._subRange._mipLevels._count == U16_MAX ? VK_REMAINING_MIP_LEVELS : targetView._subRange._mipLevels._count;
                            _subresourceRange[i].baseArrayLayer = targetView._subRange._layerRange._offset;
                            _subresourceRange[i].layerCount = targetView._subRange._layerRange._count == U16_MAX ? VK_REMAINING_ARRAY_LAYERS : targetView._subRange._layerRange._count;
                        }
                        usage = RTAttachment::Layout::ATTACHMENT;
                    }
                    else
                    {
                        usage = RTAttachment::Layout::SHADER_READ;
                    }

                    if ( transitionMask[i] )
                    {
                        att->_attachmentUsage = usage;

                        if ( !resolveMSAA || descriptor._keepMSAADataAfterResolve )
                        {
                            vkTexture::TransitionTexture( targetTransition, _subresourceRange[i], vkTexRender->image()->_image, memBarriers[memBarrierCount++]);
                        }

                        if ( resolveMSAA )
                        {
                            vkTexture* vkTexResolve = static_cast<vkTexture*>(_attachments[i]->resolvedTexture().get());
                            DIVIDE_ASSERT( vkTexRender->sampleFlagBits() != VK_SAMPLE_COUNT_1_BIT && vkTexResolve->sampleFlagBits() == VK_SAMPLE_COUNT_1_BIT );

                            PROFILE_SCOPE( "Colour Resolve", Profiler::Category::Graphics );
                            vkTexture::TransitionTexture( targetTransition, _subresourceRange[i], vkTexResolve->image()->_image, memBarriers[memBarrierCount++] );
                        }

                        if ( att->_descriptor._externalAttachment != nullptr)
                        {
                            att->_descriptor._externalAttachment->_attachmentUsage = usage;
                        }
                    }
                }
            }
        }

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] )
        {
            PROFILE_SCOPE( "Depth Attachment", Profiler::Category::Graphics );

            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];

            RTAttachment::Layout usage = att->_attachmentUsage;
            if ( (toWrite && usage != RTAttachment::Layout::ATTACHMENT) ||
                 (!toWrite && usage != RTAttachment::Layout::SHADER_READ) )
            {
                vkTexture* vkTexRender = static_cast<vkTexture*>(att->renderTexture().get());

                ImageView targetView = vkTexRender->getView();
                const DrawLayerEntry depthEntry = srcDepthLayer._layer == INVALID_INDEX ? targetDepthLayer : srcDepthLayer;
                if ( IsCubeTexture( vkTexRender->descriptor().texType() ) )
                {
                    targetView._subRange._layerRange = { to_U16(depthEntry._cubeFace + (depthEntry._layer * 6u)), descriptor._layeredRendering ? U16_MAX : U16_ONE };
                }
                else
                {
                    targetView._subRange._layerRange = { depthEntry._layer, descriptor._layeredRendering ? U16_MAX : U16_ONE };
                }
            
                if ( descriptor._mipWriteLevel > 0u )
                {
                    targetView._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                }
            
                const bool hasStencil = att->_descriptor._type == RTAttachmentType::DEPTH_STENCIL;
                vkTexture::TransitionType targetTransition = toWrite 
                                                                ? hasStencil ? vkTexture::TransitionType::SHADER_READ_TO_DEPTH_STENCIL_ATTACHMENT : vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT
                                                                : hasStencil ? vkTexture::TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ : vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ;

                if ( toWrite )
                {
                    _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].aspectMask = vkTexture::GetAspectFlags( vkTexRender->descriptor() );
                    if ( usage == RTAttachment::Layout::UNDEFINED )
                    {
                        targetTransition = (hasStencil ? vkTexture::TransitionType::UNDEFINED_TO_DEPTH_STENCIL_ATTACHMENT : vkTexture::TransitionType::UNDEFINED_TO_DEPTH_ATTACHMENT);

                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].baseMipLevel = 0u;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].levelCount = VK_REMAINING_MIP_LEVELS;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].baseArrayLayer = 0u;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].layerCount = VK_REMAINING_ARRAY_LAYERS;
                    }
                    else
                    {
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].baseMipLevel = targetView._subRange._mipLevels._offset;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].levelCount = targetView._subRange._mipLevels._count == U16_MAX ? VK_REMAINING_MIP_LEVELS : targetView._subRange._mipLevels._count;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].baseArrayLayer = targetView._subRange._layerRange._offset;
                        _subresourceRange[RT_DEPTH_ATTACHMENT_IDX].layerCount = targetView._subRange._layerRange._count == U16_MAX ? VK_REMAINING_ARRAY_LAYERS : targetView._subRange._layerRange._count;
                    }
                    usage = RTAttachment::Layout::ATTACHMENT;
                }
                else
                {
                    usage = RTAttachment::Layout::SHADER_READ;
                }

                if ( transitionMask[RT_DEPTH_ATTACHMENT_IDX] )
                {
                    att->_attachmentUsage = usage;

                    const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX];
                    if ( !resolveMSAA || descriptor._keepMSAADataAfterResolve )
                    {
                        vkTexture::TransitionTexture( targetTransition, _subresourceRange[RT_DEPTH_ATTACHMENT_IDX], vkTexRender->image()->_image, memBarriers[memBarrierCount++] );
                    }

                    if ( resolveMSAA )
                    {
                        vkTexture* vkTexResolve = static_cast<vkTexture*>(att->resolvedTexture().get());
                        DIVIDE_ASSERT(vkTexRender->sampleFlagBits() != VK_SAMPLE_COUNT_1_BIT && vkTexResolve->sampleFlagBits() == VK_SAMPLE_COUNT_1_BIT );

                        PROFILE_SCOPE( "Depth Resolve", Profiler::Category::Graphics );
                        vkTexture::TransitionTexture( targetTransition, _subresourceRange[RT_DEPTH_ATTACHMENT_IDX], vkTexResolve->image()->_image, memBarriers[memBarrierCount++] );
                    }

                    if ( att->_descriptor._externalAttachment != nullptr )
                    {
                        att->_descriptor._externalAttachment->_attachmentUsage = usage;
                    }
                }
            }
        }

        if ( memBarrierCount > 0u )
        {
            dependencyInfo.imageMemoryBarrierCount = memBarrierCount;
            dependencyInfo.pImageMemoryBarriers = memBarriers.data();

            VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
        }
    }


    void vkRenderTarget::begin( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        static RTTransitionMask s_defaultTransitionMask = create_array< RT_MAX_ATTACHMENT_COUNT >(true);

        _previousPolicy = descriptor;

        assert(pipelineRenderingCreateInfoOut.sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);

        const bool needLayeredColour = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer != INVALID_INDEX && (descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer > 0u || descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._cubeFace > 0u);
        DrawLayerEntry targetColourLayer{};

        bool needLayeredDepth = false;
        DrawLayerEntry targetDepthLayer{};

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( descriptor._writeLayers[i]._layer != INVALID_INDEX && (descriptor._writeLayers[i]._layer > 0u || descriptor._writeLayers[i]._cubeFace > 0u) )
            {
                needLayeredDepth = true;
                targetDepthLayer = descriptor._writeLayers[i];
                break;
            }
        }

        vkTexture::CachedImageView::Descriptor imageViewDescriptor{};
        imageViewDescriptor._subRange = {};

        U16 layerCount = 1u;

        U8 stagingIndex = 0u;

        {
            PROFILE_SCOPE( "Colour Attachments", Profiler::Category::Graphics );
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
            {
                if ( _attachmentsUsed[i] && descriptor._drawMask[i] )
                {
                    vkTexture* vkTexRender = static_cast<vkTexture*>(_attachments[i]->renderTexture().get());
                    imageViewDescriptor._subRange = vkTexRender->getView()._subRange;
                    if ( descriptor._writeLayers[i]._layer != INVALID_INDEX || needLayeredColour )
                    {
                        layerCount = std::max( layerCount, vkTexRender->depth() );
                        targetColourLayer = descriptor._writeLayers[i]._layer == INVALID_INDEX ? targetColourLayer : descriptor._writeLayers[i];
                        if ( IsCubeTexture( vkTexRender->descriptor().texType() ) )
                        {
                            imageViewDescriptor._subRange._layerRange = { to_U16(targetColourLayer._cubeFace + (targetColourLayer._layer * 6u)), descriptor._layeredRendering ? U16_MAX : U16_ONE };
                            layerCount *= 6u;
                        }
                        else
                        {
                            assert( targetColourLayer._cubeFace == 0u );
                            imageViewDescriptor._subRange._layerRange = { targetColourLayer._layer, descriptor._layeredRendering ? U16_MAX : U16_ONE };
                        }
                    }
                    else if ( descriptor._mipWriteLevel > 0u )
                    {
                        imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                    }

                    imageViewDescriptor._resolveTarget = false;
                    imageViewDescriptor._format = vkTexRender->vkFormat();
                    imageViewDescriptor._type = imageViewDescriptor._subRange._layerRange._count > 1u ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
                    imageViewDescriptor._usage = ImageUsage::RT_COLOUR_ATTACHMENT;

                    VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];
                    info.imageView = vkTexRender->getImageView( imageViewDescriptor );
                    _colourAttachmentFormats[stagingIndex] = vkTexRender->vkFormat();
                    
                    info.clearValue = {};
                    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                    const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[i];
                    // If we specify a clear policy, we want to clear out attachment on load
                    if ( clearPolicy[i]._enabled )
                    {
                        info.clearValue.color = {
                            clearPolicy[i]._colour.r,
                            clearPolicy[i]._colour.g,
                            clearPolicy[i]._colour.b,
                            clearPolicy[i]._colour.a
                        };
                        info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    }
                    else if ( resolveMSAA && !descriptor._keepMSAADataAfterResolve && !_keptMSAAData)
                    {
                        info.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    }
                    else
                    {
                        info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    }
                    // MSAA complicates things a lot. For best performance we want to inline resolve and also don't care about the msaa target's contents that much
                    // Ideally, we should not care about our MSAA load and store and just focus on the resolve target ...
                    if ( resolveMSAA )
                    {
                        imageViewDescriptor._resolveTarget = true;

                        if ( !descriptor._keepMSAADataAfterResolve )
                        {
                            info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                        }

                        info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                        info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        info.resolveImageView = static_cast<vkTexture*>(_attachments[i]->resolvedTexture().get())->getImageView( imageViewDescriptor );
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
        }
        pipelineRenderingCreateInfoOut.colorAttachmentCount = stagingIndex;
        pipelineRenderingCreateInfoOut.pColorAttachmentFormats = _colourAttachmentFormats.data();

        _renderingInfo.colorAttachmentCount = stagingIndex;
        _renderingInfo.pColorAttachments = _stagingColourAttachmentInfo.data();

        if ( _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] )
        {
            PROFILE_SCOPE( "Depth Attachment", Profiler::Category::Graphics );

            const auto& att = _attachments[RT_DEPTH_ATTACHMENT_IDX];
            const bool hasStencil = att->_descriptor._type == RTAttachmentType::DEPTH_STENCIL;

            vkTexture* vkTexRender = static_cast<vkTexture*>(att->renderTexture().get());
            imageViewDescriptor._subRange = vkTexRender->getView()._subRange;
            if ( descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer != INVALID_INDEX || needLayeredDepth )
            {
                layerCount = std::max( layerCount, vkTexRender->depth() );
                targetDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer == INVALID_INDEX ? targetDepthLayer : descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
                if ( IsCubeTexture( vkTexRender->descriptor().texType() ) )
                {
                    imageViewDescriptor._subRange._layerRange = { to_U16(targetDepthLayer._cubeFace + (targetDepthLayer._layer * 6u)), descriptor._layeredRendering ? U16_MAX : U16_ONE };
                    layerCount *= 6u;
                }
                else
                {
                    assert( targetColourLayer._cubeFace == 0u );
                    imageViewDescriptor._subRange._layerRange = { targetDepthLayer._layer, descriptor._layeredRendering ? U16_MAX : U16_ONE };
                }
            }
            else if ( descriptor._mipWriteLevel != U16_MAX )
            {
                imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
            }

            imageViewDescriptor._resolveTarget = false;
            imageViewDescriptor._format = vkTexRender->vkFormat();
            imageViewDescriptor._type = imageViewDescriptor._subRange._layerRange._count > 1u ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
            imageViewDescriptor._usage = hasStencil ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT : ImageUsage::RT_DEPTH_ATTACHMENT;

            _depthAttachmentInfo.imageView = vkTexRender->getImageView( imageViewDescriptor );

            _depthAttachmentInfo.clearValue.depthStencil.depth = 1.f;

            // We should always load/clear depth values since DONT_CARE will lead to failed depth tests
            _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            _depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            // If we specify a clear policy, we want to clear out attachment on load
            if ( clearPolicy[RT_DEPTH_ATTACHMENT_IDX]._enabled )
            {
                _depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                _depthAttachmentInfo.clearValue.depthStencil.depth = clearPolicy[RT_DEPTH_ATTACHMENT_IDX]._colour.r;
                _depthAttachmentInfo.clearValue.depthStencil.stencil = to_U32(clearPolicy[RT_DEPTH_ATTACHMENT_IDX]._colour.g);
            }

            const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX];

            if ( resolveMSAA )
            {
                imageViewDescriptor._resolveTarget = true;

                if ( !descriptor._keepMSAADataAfterResolve )
                {
                    _depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }

                _depthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                _depthAttachmentInfo.resolveImageLayout = hasStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                _depthAttachmentInfo.resolveImageView = static_cast<vkTexture*>(att->resolvedTexture().get())->getImageView( imageViewDescriptor );
            }

            pipelineRenderingCreateInfoOut.depthAttachmentFormat = vkTexRender->vkFormat();
            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }
        else
        {
            pipelineRenderingCreateInfoOut.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
            _renderingInfo.pDepthAttachment = nullptr;
        }

        _renderingInfo.layerCount = descriptor._layeredRendering ? layerCount : 1;
        transitionAttachments( cmdBuffer, descriptor, s_defaultTransitionMask, true );
        _keptMSAAData = descriptor._keepMSAADataAfterResolve;
    }

    void vkRenderTarget::end( VkCommandBuffer cmdBuffer, const RTTransitionMask& mask )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        transitionAttachments( cmdBuffer, _previousPolicy, mask, false );
    }

}; //namespace Divide