
#include "Headers/vkRenderTarget.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

#include "Core/Resources/Headers/ResourceCache.h"

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

    bool vkRenderTarget::initAttachment(RTAttachment* att, const RTAttachmentType type, const RTColourAttachmentSlot slot)
    {
        if (!RenderTarget::initAttachment(att, type, slot))
        {
            return false;
        }

        VK_API::GetStateTracker().IMCmdContext(QueueType::GRAPHICS)->flushCommandBuffer([&](VkCommandBuffer cmdBuffer, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue)
        {
            vkTexture* vkTex = static_cast<vkTexture*>(Get(att->texture()));

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            memBarrier.image = vkTex->image()->_image;
            memBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

            if (type == RTAttachmentType::COLOUR)
            {
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            else
            {
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.newLayout      = type == RTAttachmentType::DEPTH_STENCIL
                                                  ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            }

            memBarrier.subresourceRange.aspectMask     = vkTexture::GetAspectFlags(vkTex->descriptor());
            memBarrier.subresourceRange.baseMipLevel   = 0;
            memBarrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
            memBarrier.subresourceRange.baseArrayLayer = 0;
            memBarrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1u;
            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            VK_PROFILE(vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo);

        }, "vkTexture::postPrepareTransition");

        return true;
    }

    void vkRenderTarget::blitFrom( VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        if ( source == nullptr || !IsValid(params) )
        {
            return;
        }

        VK_API::PushDebugMessage(_context.context().config(), cmdBuffer, "vkRrenderTarget::blitFrom");

        vkRenderTarget* output = this;
        [[maybe_unused]] const vec2<U16> outputDim = output->_descriptor._resolution;

        vkRenderTarget* input = source;
        [[maybe_unused]] const vec2<U16> inputDim = input->_descriptor._resolution;

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

            vkTexture* vkTexIn  = static_cast<vkTexture*>(Get(inAtt->texture()));
            vkTexture* vkTexOut = static_cast<vkTexture*>(Get(outAtt->texture()));

            const bool isDepthTextureIn = IsDepthTexture( vkTexIn->descriptor()._packing );
            const bool isDepthTextureOut = IsDepthTexture( vkTexOut->descriptor()._packing );

            U16 layerCount = entry._layerCount;
            DIVIDE_GPU_ASSERT(layerCount != ALL_LAYERS && entry._mipCount != ALL_MIPS );
            if ( IsCubeTexture( vkTexIn->descriptor()._texType ) )
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

                vkTexture::TransitionTexture(
                    sourceTransition,
                    subResourceIn, 
                    {
                        ._image = vkTexIn->image()->_image,
                        ._name = vkTexIn->resourceName().c_str(),
                        ._isResolveImage = HasUsageFlagSet(vkTexIn->descriptor(), ImageUsage::RT_RESOLVE_TARGET)
                    },
                    imageBarriers[imageBarrierCount++] 
                );

                vkTexture::TransitionTexture(
                    targetTransition,
                    subResourceOut,
                    {
                        ._image = vkTexOut->image()->_image,
                        ._name = vkTexOut->resourceName().c_str(),
                        ._isResolveImage = HasUsageFlagSet(vkTexOut->descriptor(), ImageUsage::RT_RESOLVE_TARGET )
                    },
                    imageBarriers[imageBarrierCount++] 
                );
            }

            const U16 srcDepth = vkTexIn->descriptor()._texType == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;
            const U16 dstDepth = vkTexOut->descriptor()._texType == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;

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

                vkTexture::TransitionTexture(
                    sourceTransition,
                    subResourceIn,
                    {
                      ._image = vkTexIn->image()->_image,
                      ._name = vkTexIn->resourceName().c_str(),
                      ._isResolveImage = HasUsageFlagSet(vkTexIn->descriptor(), ImageUsage::RT_RESOLVE_TARGET)
                    }, 
                    imageBarriers[imageBarrierCount++]
                );
                vkTexture::TransitionTexture(
                    targetTransition,
                    subResourceOut,
                    {
                      ._image = vkTexOut->image()->_image,
                      ._name = vkTexOut->resourceName().c_str(),
                      ._isResolveImage = HasUsageFlagSet(vkTexOut->descriptor(), ImageUsage::RT_RESOLVE_TARGET)
                    }, 
                    imageBarriers[imageBarrierCount++]
                );
            }

            if ( imageBarrierCount > 0u )
            {
                dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
                dependencyInfo.pImageMemoryBarriers = imageBarriers.data();

                VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                imageBarrierCount = 0u;
            }
        }

        VK_API::PopDebugMessage( _context.context().config(), cmdBuffer );
    }

    void vkRenderTarget::transitionAttachments( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTTransitionMask& transitionMask, const bool toWrite )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        // Double the number of barriers needed in case we have an MSAA RenderTarget (we need to transition the resolve targets as well)
        thread_local fixed_vector<VkImageMemoryBarrier2, RT_MAX_ATTACHMENT_COUNT * 2, false> memBarriers{};

        DrawLayerEntry targetDepthLayer{};
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] || descriptor._writeLayers[i]._layer._offset == INVALID_INDEX )
            {
                continue;
            }

            // Writing to a specific layer or cube face of a colour attachment, so use the same for depth
            if ( descriptor._writeLayers[i]._layer._offset > 0u ||
                 descriptor._writeLayers[i]._cubeFace > 0u )
            {
                targetDepthLayer = descriptor._writeLayers[i];
                break;
            }
        }

        VkImageSubresourceRange subresourceRange { .aspectMask = VK_IMAGE_ASPECT_NONE };

        {
            PROFILE_SCOPE( "Colour Attachments", Profiler::Category::Graphics);

            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
            {
                if ( !_attachmentsUsed[i] || !descriptor._drawMask[i] || transitionMask[i] )
                {
                    continue;
                }

                RTAttachment_uptr& attachment = _attachments[i];
                DIVIDE_GPU_ASSERT(attachment->_attachmentUsage != RTAttachment::Layout::UNDEFINED);

                const RTAttachment::Layout targetUsage = toWrite ? RTAttachment::Layout::ATTACHMENT : RTAttachment::Layout::SHADER_READ;

                if ( attachment->_attachmentUsage == targetUsage)
                {
                    continue;
                }

                attachment->_attachmentUsage = targetUsage;

                if (attachment->_descriptor._externalAttachment != nullptr)
                {
                    attachment->_descriptor._externalAttachment->_attachmentUsage = targetUsage;
                }

                const DrawLayerEntry targetColourLayer = descriptor._writeLayers[i]._layer._offset == INVALID_INDEX
                                                                                                    ? targetDepthLayer
                                                                                                    : descriptor._writeLayers[i];

                vkTexture* vkTexRender  = static_cast<vkTexture*>(Get(attachment->renderTexture()));

                ImageView  viewRender = vkTexRender->getView();

                if ( IsCubeTexture(vkTexRender->descriptor()._texType) )
                {
                    viewRender._subRange._layerRange =
                    {
                        ._offset = to_U16(targetColourLayer._cubeFace + (targetColourLayer._layer._offset * 6u)),
                        ._count = to_U16(targetColourLayer._layer._count * 6u)
                    };
                }
                else
                {
                    assert( targetColourLayer._cubeFace == 0u );
                    viewRender._subRange._layerRange = targetColourLayer._layer;
                }

                if ( descriptor._mipWriteLevel != ALL_MIPS )
                {
                    viewRender._subRange._mipLevels._count = 1u;
                    viewRender._subRange._mipLevels._offset = descriptor._mipWriteLevel;
                }

                subresourceRange.aspectMask = vkTexture::GetAspectFlags( vkTexRender->descriptor() );

                subresourceRange.baseMipLevel   = viewRender._subRange._mipLevels._offset;
                subresourceRange.levelCount     = viewRender._subRange._mipLevels._count == ALL_MIPS ? VK_REMAINING_MIP_LEVELS : viewRender._subRange._mipLevels._count;
                subresourceRange.baseArrayLayer = viewRender._subRange._layerRange._offset;
                subresourceRange.layerCount     = viewRender._subRange._layerRange._count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : viewRender._subRange._layerRange._count;


                const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[i];
                if ( !resolveMSAA || descriptor._keepMSAADataAfterResolve )
                {
                    vkTexture::TransitionType targetTransition = toWrite ? vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT
                                                                         : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ;
                    vkTexture::TransitionTexture(
                        targetTransition,
                        subresourceRange,
                        {
                            ._image = vkTexRender->image()->_image,
                            ._name  = vkTexRender->resourceName().c_str(),
                            ._isResolveImage = false
                        },
                        memBarriers.emplace_back()
                    );

                    // If we don't keep MSAA data after resolve the resolve attachment will be written by dynamic rendering (vkCmdEndRendering).
                    // Emitting a pipeline barrier that appears to write the resolve image can cause WRITE_AFTER_WRITE hazards with the validation layers.
                    // Only emit an explicit layout/access barrier for the resolve image when the caller requests keeping MSAA data after resolve (i.e. we need the resolve image contents preserved).
                    if ( resolveMSAA && descriptor._keepMSAADataAfterResolve )
                    {
                        vkTexture* vkTexResolve = static_cast<vkTexture*>(Get(_attachments[i]->resolvedTexture()));

                        vkTexture::TransitionTexture(
                            targetTransition,
                            subresourceRange,
                            {
                                ._image = vkTexResolve->image()->_image,
                                ._name  = vkTexResolve->resourceName().c_str(),
                                ._isResolveImage = true
                            },
                            memBarriers.emplace_back()
                        );
                    }
                }
            }
        }

        const RTAttachment::Layout targetDepthUsage = toWrite ? RTAttachment::Layout::ATTACHMENT : RTAttachment::Layout::SHADER_READ;

        if ( transitionMask[RT_DEPTH_ATTACHMENT_IDX] &&
             _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] &&
             _attachments[RT_DEPTH_ATTACHMENT_IDX]->_attachmentUsage != targetDepthUsage )
        {
            PROFILE_SCOPE( "Depth Attachment", Profiler::Category::Graphics );

            const DrawLayerEntry& srcDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
            const DrawLayerEntry depthEntry = srcDepthLayer._layer._offset == INVALID_INDEX ? targetDepthLayer : srcDepthLayer;

            RTAttachment_uptr& attachment = _attachments[RT_DEPTH_ATTACHMENT_IDX];
            DIVIDE_GPU_ASSERT(attachment->_attachmentUsage != RTAttachment::Layout::UNDEFINED);

            attachment->_attachmentUsage = targetDepthUsage;
            if (attachment->_descriptor._externalAttachment != nullptr)
            {
                attachment->_descriptor._externalAttachment->_attachmentUsage = targetDepthUsage;
            }

            vkTexture* vkTexRender = static_cast<vkTexture*>(Get(attachment->renderTexture()));
            ImageView viewRender = vkTexRender->getView();

            if ( IsCubeTexture( vkTexRender->descriptor()._texType ) )
            {
                viewRender._subRange._layerRange = 
                {
                    ._offset = to_U16(depthEntry._cubeFace + (depthEntry._layer._offset * 6u)),
                    ._count = depthEntry._layer._count
                };
            }
            else
            {
                viewRender._subRange._layerRange = depthEntry._layer;
            }
            
            if ( descriptor._mipWriteLevel != ALL_MIPS)
            {
                viewRender._subRange._mipLevels =
                {
                    ._offset = descriptor._mipWriteLevel,
                    ._count = 1u
                };
            }
            subresourceRange.aspectMask = vkTexture::GetAspectFlags( vkTexRender->descriptor() );

            subresourceRange.baseMipLevel   = viewRender._subRange._mipLevels._offset;
            subresourceRange.levelCount     = viewRender._subRange._mipLevels._count == ALL_MIPS ? VK_REMAINING_MIP_LEVELS : viewRender._subRange._mipLevels._count;
            subresourceRange.baseArrayLayer = viewRender._subRange._layerRange._offset;
            subresourceRange.layerCount     = viewRender._subRange._layerRange._count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : viewRender._subRange._layerRange._count;

            const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX];
            if ( !resolveMSAA || descriptor._keepMSAADataAfterResolve )
            {
                const bool hasStencil = attachment->_descriptor._type == RTAttachmentType::DEPTH_STENCIL;
                const vkTexture::TransitionType targetTransition = toWrite ? vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT
                                                                           : vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ;

                vkTexture::TransitionTexture(
                    targetTransition,
                    subresourceRange,
                    {
                        ._image = vkTexRender->image()->_image,
                        ._name  = vkTexRender->resourceName().c_str(),
                        ._isResolveImage = false,
                        ._hasStencilMask = hasStencil
                    },
                    memBarriers.emplace_back()
                );

                // Same rule as colour resolves: only emit an explicit barrier for the resolve image if the resolve contents must be preserved after resolve.
                if ( resolveMSAA && descriptor._keepMSAADataAfterResolve )
                {
                    vkTexture* vkTexResolve = static_cast<vkTexture*>(Get(attachment->resolvedTexture()));

                    vkTexture::TransitionTexture(
                        targetTransition,
                        subresourceRange,
                        {
                            ._image = vkTexResolve->image()->_image,
                            ._name  = vkTexResolve->resourceName().c_str(),
                            ._isResolveImage = true,
                            ._hasStencilMask = hasStencil
                        },
                        memBarriers.emplace_back()
                    );
                }
            }
        }

        if ( !memBarriers.empty() )
        {
            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = to_U32(memBarriers.size());
            dependencyInfo.pImageMemoryBarriers = memBarriers.data();

            VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );

            memBarriers.reset_lose_memory();
        }
    }

    void vkRenderTarget::begin( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        constexpr RTTransitionMask s_defaultTransitionMask = {true, true, true, true, true };

        assert(pipelineRenderingCreateInfoOut.sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);

        const bool needLayeredColour = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset != INVALID_INDEX && (descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset > 0u || descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._cubeFace > 0u);
        DrawLayerEntry targetColourLayer{};

        bool needLayeredDepth = false;
        DrawLayerEntry targetDepthLayer{};

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] )
            {
                continue;
            }

            if ( descriptor._writeLayers[i]._layer._offset != INVALID_INDEX && (descriptor._writeLayers[i]._layer._offset > 0u || descriptor._writeLayers[i]._cubeFace > 0u) )
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

        bool layeredRendering = false;

        {
            PROFILE_SCOPE( "Colour Attachments", Profiler::Category::Graphics );
            for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
            {
                if ( _attachmentsUsed[i] && descriptor._drawMask[i] )
                {
                    vkTexture* vkTexRender = static_cast<vkTexture*>(Get(_attachments[i]->renderTexture()));
                    imageViewDescriptor._subRange = vkTexRender->getView()._subRange;
                    if ( descriptor._writeLayers[i]._layer._offset != INVALID_INDEX || needLayeredColour )
                    {
                        layerCount = std::max( layerCount, vkTexRender->depth() );
                        targetColourLayer = descriptor._writeLayers[i]._layer._offset == INVALID_INDEX ? targetColourLayer : descriptor._writeLayers[i];
                        if ( IsCubeTexture( vkTexRender->descriptor()._texType ) )
                        {
                            imageViewDescriptor._subRange._layerRange = { to_U16(targetColourLayer._cubeFace + (targetColourLayer._layer._offset * 6u)), targetColourLayer._layer._count };
                            layerCount *= 6u;
                        }
                        else
                        {
                            assert( targetColourLayer._cubeFace == 0u );
                            imageViewDescriptor._subRange._layerRange = targetColourLayer._layer;
                        }
                    }
                    else if ( descriptor._mipWriteLevel != ALL_MIPS )
                    {
                        imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                    }
                    if (imageViewDescriptor._subRange._layerRange._count > 1u)
                    {
                        layeredRendering = true;
                    }

                    imageViewDescriptor._resolveTarget = false;
                    imageViewDescriptor._format        = vkTexRender->vkFormat();
                    imageViewDescriptor._type          = layeredRendering ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
                    imageViewDescriptor._usage         = ImageUsage::RT_COLOUR_ATTACHMENT;

                    VkRenderingAttachmentInfo& info = _colourAttachmentInfo[i];
                    info.imageView = vkTexRender->getImageView( imageViewDescriptor );
                    _colourAttachmentFormats[stagingIndex] = vkTexRender->vkFormat();
                    
                    info.clearValue = {};
                    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                    const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[i];
                    // If we specify a clear policy, we want to clear out attachment on load
                    if ( clearPolicy[i]._enabled )
                    {
                        *info.clearValue.color.float32 = *clearPolicy[i]._colour._v;

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
                        info.resolveImageView = static_cast<vkTexture*>(Get(_attachments[i]->resolvedTexture()))->getImageView( imageViewDescriptor );
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

            vkTexture* vkTexRender = static_cast<vkTexture*>(Get(att->renderTexture()));
            imageViewDescriptor._subRange = vkTexRender->getView()._subRange;
            if ( descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset != INVALID_INDEX || needLayeredDepth )
            {
                layerCount = std::max( layerCount, vkTexRender->depth() );
                targetDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset == INVALID_INDEX ? targetDepthLayer : descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
                if ( IsCubeTexture( vkTexRender->descriptor()._texType ) )
                {
                    imageViewDescriptor._subRange._layerRange = { to_U16(targetDepthLayer._cubeFace + (targetDepthLayer._layer._offset * 6u)), targetDepthLayer._layer._count };
                    layerCount *= 6u;
                }
                else
                {
                    assert( targetColourLayer._cubeFace == 0u );
                    imageViewDescriptor._subRange._layerRange = targetDepthLayer._layer;
                }
            }
            else if ( descriptor._mipWriteLevel != ALL_MIPS )
            {
                imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
            }
            if (imageViewDescriptor._subRange._layerRange._count > 1u)
            {
                layeredRendering = true;
            }

            imageViewDescriptor._resolveTarget = false;
            imageViewDescriptor._format        = vkTexRender->vkFormat();
            imageViewDescriptor._type          = layeredRendering ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
            imageViewDescriptor._usage         = hasStencil ? ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT : ImageUsage::RT_DEPTH_ATTACHMENT;

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

            if ( descriptor._autoResolveMSAA && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX] )
            {
                imageViewDescriptor._resolveTarget = true;

                if ( !descriptor._keepMSAADataAfterResolve )
                {
                    _depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }

                VkResolveModeFlagBits chosenDepthResolve = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                const VkResolveModeFlags supported = VK_API::s_supportedDepthResolveModes;
                if ( supported & VK_RESOLVE_MODE_AVERAGE_BIT )
                {
                    chosenDepthResolve = VK_RESOLVE_MODE_AVERAGE_BIT;
                }
                else if ( supported & VK_RESOLVE_MODE_MIN_BIT )
                {
                    chosenDepthResolve = VK_RESOLVE_MODE_MIN_BIT;
                }
                else if ( supported & VK_RESOLVE_MODE_MAX_BIT )
                {
                    chosenDepthResolve = VK_RESOLVE_MODE_MAX_BIT;
                }
                else if ( supported & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT )
                {
                    chosenDepthResolve = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                }

                _depthAttachmentInfo.resolveMode = chosenDepthResolve;
                _depthAttachmentInfo.resolveImageLayout = hasStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                _depthAttachmentInfo.resolveImageView = static_cast<vkTexture*>(Get(att->resolvedTexture()))->getImageView( imageViewDescriptor );
            }

            pipelineRenderingCreateInfoOut.depthAttachmentFormat = vkTexRender->vkFormat();
            _renderingInfo.pDepthAttachment = &_depthAttachmentInfo;
        }
        else
        {
            pipelineRenderingCreateInfoOut.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
            _renderingInfo.pDepthAttachment = nullptr;
        }

        _renderingInfo.layerCount = layeredRendering ? layerCount : 1;
        transitionAttachments( cmdBuffer, descriptor, s_defaultTransitionMask, true );
        _keptMSAAData = descriptor._keepMSAADataAfterResolve;
        _previousPolicy = descriptor;
    }

    void vkRenderTarget::end( VkCommandBuffer cmdBuffer, const RTTransitionMask& mask )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        transitionAttachments( cmdBuffer, _previousPolicy, mask, false );
    }
}; //namespace Divide
