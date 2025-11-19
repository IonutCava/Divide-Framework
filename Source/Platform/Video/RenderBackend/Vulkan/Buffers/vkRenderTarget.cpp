#include "Headers/vkRenderTarget.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"

#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{
    constexpr bool ENABLED_DEBUG_PRINTING = false;

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
        return RenderTarget::initAttachment(att, type, slot);
    }

    VkImageSubresourceRange vkRenderTarget::vkRenderTarget::computeAttachmentSubresourceRange(const U8 slotIndex, const bool resolve) const noexcept
    {
        DIVIDE_ASSERT(slotIndex < to_base(RTColourAttachmentSlot::COUNT) + 1u);

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_NONE;
        range.baseMipLevel = 0u;
        range.levelCount = VK_REMAINING_MIP_LEVELS;
        range.baseArrayLayer = 0u;
        range.layerCount = VK_REMAINING_ARRAY_LAYERS;

        const auto& layered = _renderInfoState._layeredInfo._layeredRendering[slotIndex];
        if ( !_attachmentsUsed[slotIndex] )
        {
            return range;
        }

        const auto& att = _attachments[slotIndex];
        if ( att == nullptr )
        {
            return range;
        }

        vkTexture* vkTex = static_cast<vkTexture*>(Get(resolve ? att->resolvedTexture() : att->renderTexture()));
        range.aspectMask = vkTexture::GetAspectFlags(vkTex->descriptor());

        if ( !layered._enabled )
        {
            return range;
        }

        if ( layered._mipLevel != ALL_MIPS)
        {
            range.baseMipLevel = layered._mipLevel;
            range.levelCount = 1u;
        }

        const SubRange& layerRange = layered._layerRange;
        if (layerRange._count != ALL_LAYERS)
        {
            range.baseArrayLayer = layerRange._offset;
            // If the texture is a cubemap, the layerRange stored earlier already accounts for cube-face expansion (begin() logic does that)
            range.layerCount = layerRange._count;
        }

        return range;
    }

    void vkRenderTarget::blitFrom( VkCommandBuffer cmdBuffer, vkRenderTarget* source, const RTBlitParams& params ) noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        if constexpr ( ENABLED_DEBUG_PRINTING )
        {
            Console::d_errorfn("vkRenderTarget::blitFrom [ {} ]", name().c_str());
        }

        if ( source == nullptr || !IsValid(params) )
        {
            return;
        }

        VK_API::PushDebugMessage(_context.context().config(), cmdBuffer, "vkRrenderTarget::blitFrom");

        vkRenderTarget* output = this;
        [[maybe_unused]] const vec2<U16> outputDim = output->_descriptor._resolution;

        vkRenderTarget* input = source;
        [[maybe_unused]] const vec2<U16> inputDim = input->_descriptor._resolution;

        std::array<VkImageMemoryBarrier2, 4> imageBarriers{};
        U8 imageBarrierCount = 0u;

        const auto flushBarriers = [&imageBarriers, &imageBarrierCount](VkCommandBuffer cmdBuffer)
        {
            if (imageBarrierCount > 0u)
            {
                VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                dependencyInfo.imageMemoryBarrierCount = imageBarrierCount;
                dependencyInfo.pImageMemoryBarriers = imageBarriers.data();

                vkTexture::FlushPipelineBarriers(cmdBuffer, dependencyInfo);
                imageBarrierCount = 0u;
            }
        };

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

            const RTAttachment_uptr& inAtt = input->_attachments[entry._input._index];
            const RTAttachment_uptr& outAtt = output->_attachments[entry._output._index];

            vkTexture* vkTexIn  = static_cast<vkTexture*>(Get(inAtt->texture()));
            vkTexture* vkTexOut = static_cast<vkTexture*>(Get(outAtt->texture()));

            const bool isDepthIn = IsDepthTexture( vkTexIn->descriptor()._packing );
            const bool isDepthOut = IsDepthTexture( vkTexOut->descriptor()._packing );

            U16 layerCount = entry._layerCount;
            DIVIDE_GPU_ASSERT(layerCount != ALL_LAYERS && entry._mipCount != ALL_MIPS );
            if ( IsCubeTexture( vkTexIn->descriptor()._texType ) )
            {
                layerCount *= 6u;
            }

            const VkImageSubresourceRange subResourceIn
            {
                  .aspectMask     = vkTexture::GetAspectFlags( vkTexIn->descriptor() ),
                  .baseMipLevel   = entry._input._mipOffset,
                  .levelCount     = entry._mipCount,
                  .baseArrayLayer = entry._input._layerOffset,
                  .layerCount     = layerCount
            };

            const VkImageSubresourceRange subResourceOut
            {
                  .aspectMask     = vkTexture::GetAspectFlags( vkTexOut->descriptor() ),
                  .baseMipLevel   = entry._output._mipOffset,
                  .levelCount     = entry._mipCount,
                  .baseArrayLayer = entry._output._layerOffset,
                  .layerCount     = layerCount
            };

            const bool inIsResolve = HasUsageFlagSet(vkTexIn->descriptor(), ImageUsage::RT_RESOLVE_TARGET);
            const bool outIsResolve = HasUsageFlagSet(vkTexOut->descriptor(), ImageUsage::RT_RESOLVE_TARGET);

            RTUsageTracker& sourceUsage      = inIsResolve ? inAtt->_resolveUsage : inAtt->_renderUsage;
            RTUsageTracker& destinationUsage = outIsResolve ? outAtt->_resolveUsage : outAtt->_renderUsage;

            DIVIDE_ASSERT(sourceUsage._usage != RTUsageTracker::Layout::COUNT &&
                          destinationUsage._usage != RTUsageTracker::Layout::COUNT,
                          "vkRenderTarget::blitFrom: Invalid usage state for source or destination texture!");

            const bool inIsAttachment  = sourceUsage._usage == RTUsageTracker::Layout::ATTACHMENT;
            const bool outIsAttachment = destinationUsage._usage == RTUsageTracker::Layout::ATTACHMENT;

             const vkTexture::TransitionType preSourceTransition =
                inIsAttachment
                    ? (isDepthIn ? vkTexture::TransitionType::ATTACHMENT_TO_BLIT_READ_DEPTH
                                 : vkTexture::TransitionType::ATTACHMENT_TO_BLIT_READ_COLOUR)
                    : vkTexture::TransitionType::SHADER_READ_TO_BLIT_READ;

            const vkTexture::TransitionType preDestTransition =
                outIsAttachment
                    ? (isDepthOut ? vkTexture::TransitionType::ATTACHMENT_TO_BLIT_WRITE_DEPTH
                                  : vkTexture::TransitionType::ATTACHMENT_TO_BLIT_WRITE_COLOUR)
                    : vkTexture::TransitionType::SHADER_READ_TO_BLIT_WRITE;

            vkTexture::TransitionTexture(
                preSourceTransition,
                subResourceIn,
                {
                    ._image = vkTexIn->image()->_image,
                    ._name = vkTexIn->resourceName().c_str(),
                    ._isResolveImage = inIsResolve
                },
                imageBarriers[imageBarrierCount++]
            );

            vkTexture::TransitionTexture(
                preDestTransition,
                subResourceOut,
                {
                    ._image = vkTexOut->image()->_image,
                    ._name = vkTexOut->resourceName().c_str(),
                    ._isResolveImage = outIsResolve
                },
                imageBarriers[imageBarrierCount++]
            );

            flushBarriers(cmdBuffer);

            sourceUsage._usage = RTUsageTracker::Layout::COPY_READ;
            destinationUsage._usage = RTUsageTracker::Layout::COPY_WRITE;

            const U16 srcDepth = vkTexIn->descriptor()._texType == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;
            const U16 dstDepth = vkTexOut->descriptor()._texType == TextureType::TEXTURE_3D ? vkTexIn->depth() : 1u;

            VkImageBlit2 blitRegions[MAX_BLIT_ENTRIES] = {};
            U8 blitRegionCount = 0u;

            for ( U8 mip = 0u; mip < entry._mipCount; ++mip )
            {
                VkImageBlit2& region = blitRegions[blitRegionCount++];
                region = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                    .srcSubresource = {
                        .aspectMask = subResourceIn.aspectMask,
                        .mipLevel = to_U32(entry._input._mipOffset + mip),
                        .baseArrayLayer = entry._input._layerOffset,
                        .layerCount = layerCount
                    },
                    .dstSubresource = {
                        .aspectMask = subResourceOut.aspectMask,
                        .mipLevel = to_U32(entry._output._mipOffset + mip),
                        .baseArrayLayer = entry._output._layerOffset,
                        .layerCount = layerCount
                    }
                };

                region.srcOffsets[0] = { 0,0,0 };
                region.dstOffsets[0] = { 0,0,0 };
                region.srcOffsets[1] = { to_I32(vkTexIn->width()),  to_I32(vkTexIn->height()),  to_I32(srcDepth) };
                region.dstOffsets[1] = { to_I32(vkTexOut->width()), to_I32(vkTexOut->height()), to_I32(dstDepth) };
            }

            if ( blitRegionCount > 0u )
            {
                const VkBlitImageInfo2 blitInfo
                {
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

            // Source back to shader-read (we sampled it after the blit)
            const vkTexture::TransitionType postSourceTransition =
                inIsAttachment
                    ? (isDepthIn ? vkTexture::TransitionType::BLIT_READ_TO_ATTACHMENT_DEPTH
                                 : vkTexture::TransitionType::BLIT_READ_TO_ATTACHMENT_COLOUR)
                    : vkTexture::TransitionType::BLIT_READ_TO_SHADER_READ;

            const vkTexture::TransitionType postDestTransition =
                outIsAttachment
                    ? (isDepthOut ? vkTexture::TransitionType::BLIT_WRITE_TO_ATTACHMENT_DEPTH
                                  : vkTexture::TransitionType::BLIT_WRITE_TO_ATTACHMENT_COLOUR)
                    : vkTexture::TransitionType::BLIT_WRITE_TO_SHADER_READ;

            vkTexture::TransitionTexture(
                postSourceTransition,
                subResourceIn,
                {
                    ._image = vkTexIn->image()->_image,
                    ._name = vkTexIn->resourceName().c_str(),
                    ._isResolveImage = inIsResolve
                },
                imageBarriers[imageBarrierCount++]
            );

            vkTexture::TransitionTexture(
                postDestTransition,
                subResourceOut,
                {
                    ._image = vkTexOut->image()->_image,
                    ._name = vkTexOut->resourceName().c_str(),
                    ._isResolveImage = outIsResolve
                },
                imageBarriers[imageBarrierCount++]
            );

            flushBarriers(cmdBuffer);

            sourceUsage._usage = inIsAttachment ? RTUsageTracker::Layout::ATTACHMENT : RTUsageTracker::Layout::SHADER_READ;
            destinationUsage._usage = outIsAttachment ? RTUsageTracker::Layout::ATTACHMENT : RTUsageTracker::Layout::SHADER_READ;
        }

        // Transition the write target to shader read for sampling if it started as an attachment
        for (const RTBlitEntry entry : params)
        {
            if (entry._input._index == INVALID_INDEX ||
                entry._output._index == INVALID_INDEX ||
                !input->_attachmentsUsed[entry._input._index] ||
                !output->_attachmentsUsed[entry._output._index])
            {
                continue;
            }

            const RTAttachment_uptr& outAtt = output->_attachments[entry._output._index];
            vkTexture* vkTexOut = static_cast<vkTexture*>(Get(outAtt->texture()));
            const bool outIsResolve = HasUsageFlagSet(vkTexOut->descriptor(), ImageUsage::RT_RESOLVE_TARGET);

            RTUsageTracker& outUsage = outIsResolve ? outAtt->_resolveUsage : outAtt->_renderUsage;
            if ( outUsage._usage != RTUsageTracker::Layout::ATTACHMENT )
            {
                continue;
            }

            VkImageSubresourceRange fullRange{};
            fullRange.aspectMask = vkTexture::GetAspectFlags(vkTexOut->descriptor()),
            fullRange.baseMipLevel = 0;
            fullRange.levelCount = VK_REMAINING_MIP_LEVELS;
            fullRange.baseArrayLayer = 0;
            fullRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            vkTexture::FlushPipelineBarrier(
                cmdBuffer,
                IsDepthTexture(vkTexOut->descriptor()._packing)
                    ? vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ
                    : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ,
                fullRange,
                {
                   ._image = vkTexOut->image()->_image,
                   ._name  = vkTexOut->resourceName().c_str(),
                   ._isResolveImage = outIsResolve
                });

            outUsage._usage = RTUsageTracker::Layout::SHADER_READ;
        }

        VK_API::PopDebugMessage( _context.context().config(), cmdBuffer );
    }

    void vkRenderTarget::transitionAttachments( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTTransitionMask& transitionMask, const bool toWrite )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        thread_local fixed_vector<VkImageMemoryBarrier2, RT_MAX_ATTACHMENT_COUNT * 2, false> memBarriers{};
        // Helper to get the actual current usage, accounting for external attachments
        auto getCurrentUsage = [](RTAttachment* att, bool isResolve) -> RTUsageTracker::Layout
        {
            if (att->_descriptor._externalAttachment != nullptr)
            {
                // External attachment: use the source attachment's state
                return isResolve
                    ? att->_descriptor._externalAttachment->_resolveUsage._usage
                    : att->_descriptor._externalAttachment->_renderUsage._usage;
            }
            // Local attachment: use our own state
            return isResolve ? att->_resolveUsage._usage : att->_renderUsage._usage;
        };

        DrawLayerEntry targetDepthLayer{};
        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] || descriptor._writeLayers[i]._layer._offset == INVALID_INDEX )
            {
                continue;
            }

            if ( descriptor._writeLayers[i]._layer._offset > 0u ||
                 descriptor._writeLayers[i]._cubeFace > 0u )
            {
                targetDepthLayer = descriptor._writeLayers[i];
                break;
            }
        }

        PROFILE_SCOPE( "Colour Attachments", Profiler::Category::Graphics);

        for ( U8 i = 0u; i < to_base( RTColourAttachmentSlot::COUNT ); ++i )
        {
            if ( !_attachmentsUsed[i] || !descriptor._drawMask[i] || !transitionMask[i] )
            {
                continue;
            }

            RTAttachment_uptr& attachment = _attachments[i];

            const RTUsageTracker::Layout targetRenderUsage = toWrite ? RTUsageTracker::Layout::ATTACHMENT : RTUsageTracker::Layout::SHADER_READ;
            const RTUsageTracker::Layout currentRenderUsage = getCurrentUsage(attachment.get(), false);
            DIVIDE_GPU_ASSERT(currentRenderUsage != RTUsageTracker::Layout::COUNT);

            if constexpr (ENABLED_DEBUG_PRINTING)
            {
                Console::d_errorfn("vkRenderTarget::transitionAttachments [{}][Colour : {}][ render={} resolve={} ] -> [ {} ]",
                                    name().c_str(),
                                    i,
                                    RTUsageTracker::Names::layout[to_base(currentRenderUsage)],
                                    RTUsageTracker::Names::layout[to_base(attachment->_resolveUsage._usage)],
                                    RTUsageTracker::Names::layout[to_base(targetRenderUsage)]);
            }

            const DrawLayerEntry targetColourLayer = descriptor._writeLayers[i]._layer._offset == INVALID_INDEX
                                                                                    ? targetDepthLayer
                                                                                    : descriptor._writeLayers[i];

            vkTexture* vkTexRender = static_cast<vkTexture*>(Get(attachment->renderTexture()));
            ImageView viewRender = vkTexRender->getView();

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

            const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[i];
            const bool sharedTexture = Get(attachment->renderTexture()) == Get(attachment->resolvedTexture());

            // Render image transition (colour)
            if ( currentRenderUsage != targetRenderUsage )
            {
                const vkTexture::TransitionType targetTransition = toWrite ? vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT
                                                                           : vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ;
                vkTexture::TransitionTexture(
                    targetTransition,
                    computeAttachmentSubresourceRange(i, false),
                    {
                        ._image         = vkTexRender->image()->_image,
                        ._name          = vkTexRender->resourceName().c_str(),
                        ._isResolveImage = false
                    },
                    memBarriers.emplace_back()
                );

                attachment->_renderUsage._usage = targetRenderUsage;
                if (attachment->_descriptor._externalAttachment != nullptr)
                {
                    attachment->_descriptor._externalAttachment->_renderUsage._usage = targetRenderUsage;
                }

                if (sharedTexture)
                {
                    attachment->_resolveUsage._usage = targetRenderUsage;
                    if (attachment->_descriptor._externalAttachment != nullptr)
                    {
                        attachment->_descriptor._externalAttachment->_resolveUsage._usage = targetRenderUsage;
                    }
                }
            }
            else
            {
                if constexpr (ENABLED_DEBUG_PRINTING)
                {
                    Console::d_errorfn("vkRenderTarget::transitionAttachments skip [ {} ]", vkTexRender->resourceName().c_str());
                }
                if (toWrite)
                {
                    _renderInfoState._layeredInfo._layeredRendering[i]._enabled = false;
                }
            }

            // Resolve image transition (colour)
            if (resolveMSAA && !sharedTexture)
            {
                vkTexture* vkTexResolve = static_cast<vkTexture*>(Get(_attachments[i]->resolvedTexture()));


                const RTUsageTracker::Layout targetResolveUsage = toWrite ? RTUsageTracker::Layout::ATTACHMENT
                                                                          : (descriptor._keepMSAADataAfterResolve
                                                                                    ? RTUsageTracker::Layout::SHADER_READ
                                                                                    : RTUsageTracker::Layout::ATTACHMENT);

                const RTUsageTracker::Layout currentResolveUsage = getCurrentUsage(attachment.get(), true);

                if (currentResolveUsage != targetResolveUsage)
                {
                    if (toWrite)
                    {
                        vkTexture::TransitionTexture(
                            vkTexture::TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT,
                            computeAttachmentSubresourceRange(i, true),
                            {
                                ._image = vkTexResolve->image()->_image,
                                ._name  = vkTexResolve->resourceName().c_str(),
                                ._isResolveImage = true
                            },
                            memBarriers.emplace_back()
                        );
                    }
                    else if (descriptor._keepMSAADataAfterResolve)
                    {
                        vkTexture::TransitionTexture(
                            vkTexture::TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ,
                            computeAttachmentSubresourceRange(i, true),
                            {
                                ._image = vkTexResolve->image()->_image,
                                ._name  = vkTexResolve->resourceName().c_str(),
                                ._isResolveImage = true
                            },
                        memBarriers.emplace_back()
                        );

                        attachment->_resolveUsage._usage = targetResolveUsage;
                        if (attachment->_descriptor._externalAttachment != nullptr)
                        {
                            attachment->_descriptor._externalAttachment->_resolveUsage._usage = targetResolveUsage;
                        }
                    }
                }
                else if constexpr (ENABLED_DEBUG_PRINTING)
                {
                    Console::d_errorfn("vkRenderTarget::transitionAttachments skip [ {} ]", vkTexResolve->resourceName().c_str());
                }
            }
        }

        const RTUsageTracker::Layout targetDepthRenderUsage = toWrite ? RTUsageTracker::Layout::ATTACHMENT : RTUsageTracker::Layout::SHADER_READ;

        if ( transitionMask[RT_DEPTH_ATTACHMENT_IDX] && _attachmentsUsed[RT_DEPTH_ATTACHMENT_IDX] )
        {
            PROFILE_SCOPE( "Depth Attachment", Profiler::Category::Graphics );

            const DrawLayerEntry& srcDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
            const DrawLayerEntry depthEntry = srcDepthLayer._layer._offset == INVALID_INDEX ? targetDepthLayer : srcDepthLayer;

            RTAttachment_uptr& attachment = _attachments[RT_DEPTH_ATTACHMENT_IDX];

            const RTUsageTracker::Layout currentDepthRenderUsage = getCurrentUsage(attachment.get(), false);
            DIVIDE_GPU_ASSERT(currentDepthRenderUsage != RTUsageTracker::Layout::COUNT);

            if constexpr (ENABLED_DEBUG_PRINTING)
            {
                Console::d_errorfn("vkRenderTarget::transitionAttachments [ {} ][Depth][ render={} resolve={} ] -> [ {} ]",
                                    name().c_str(),
                                    RTUsageTracker::Names::layout[to_base(currentDepthRenderUsage)],
                                    RTUsageTracker::Names::layout[to_base(attachment->_resolveUsage._usage)],
                                    RTUsageTracker::Names::layout[to_base(targetDepthRenderUsage)]);
            }

            vkTexture* vkTexRender = static_cast<vkTexture*>(Get(attachment->renderTexture()));
            ImageView viewRender = vkTexRender->getView();

            if ( IsCubeTexture( vkTexRender->descriptor()._texType ) )
            {
                viewRender._subRange._layerRange = 
                {
                    ._offset = to_U16(depthEntry._cubeFace + (depthEntry._layer._offset * 6u)),
                    ._count = to_U16(depthEntry._layer._count * 6u)
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

            const bool resolveMSAA = descriptor._autoResolveMSAA && _attachmentsAutoResolve[RT_DEPTH_ATTACHMENT_IDX];
            const bool sharedTexture = Get(attachment->renderTexture()) == Get(attachment->resolvedTexture());

            // Render image transition (depth)
            if ( currentDepthRenderUsage != targetDepthRenderUsage )
            {
                const vkTexture::TransitionType targetTransition = toWrite ? vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT
                                                                           : vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ;

                vkTexture::TransitionTexture(
                    targetTransition,
                    computeAttachmentSubresourceRange(RT_DEPTH_ATTACHMENT_IDX, false),
                    {
                        ._image = vkTexRender->image()->_image,
                        ._name = vkTexRender->resourceName().c_str(),
                        ._isResolveImage = false
                    },
                    memBarriers.emplace_back()
                );

                attachment->_renderUsage._usage = targetDepthRenderUsage;
                if (attachment->_descriptor._externalAttachment != nullptr)
                {
                    attachment->_descriptor._externalAttachment->_renderUsage._usage = targetDepthRenderUsage;
                }

                if (sharedTexture)
                {
                    attachment->_resolveUsage._usage = targetDepthRenderUsage;
                    if (attachment->_descriptor._externalAttachment != nullptr)
                    {
                        attachment->_descriptor._externalAttachment->_resolveUsage._usage = targetDepthRenderUsage;
                    }
                }
            }
            else
            {
                if constexpr (ENABLED_DEBUG_PRINTING)
                {
                    Console::d_errorfn("vkRenderTarget::transitionAttachments skip [ {} ]", vkTexRender->resourceName().c_str());
                }
                if ( toWrite )
                {
                    _renderInfoState._layeredInfo._layeredRendering[RT_DEPTH_ATTACHMENT_IDX]._enabled = false;
                }
            }

            // Resolve image transition (depth)
            if ( resolveMSAA && !sharedTexture )
            {
                vkTexture* vkTexResolve = static_cast<vkTexture*>(Get(attachment->resolvedTexture()));


                const RTUsageTracker::Layout targetDepthResolveUsage = toWrite ? RTUsageTracker::Layout::ATTACHMENT
                                                                                : (descriptor._keepMSAADataAfterResolve
                                                                                    ? RTUsageTracker::Layout::SHADER_READ
                                                                                    : RTUsageTracker::Layout::ATTACHMENT);

                const RTUsageTracker::Layout currentDepthResolveUsage = getCurrentUsage(attachment.get(), true);

                if ( currentDepthResolveUsage != targetDepthResolveUsage )
                {
                    if (toWrite)
                    {
                        vkTexture::TransitionTexture(
                            vkTexture::TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT,
                            computeAttachmentSubresourceRange(RT_DEPTH_ATTACHMENT_IDX, true),
                            {
                                ._image = vkTexResolve->image()->_image,
                                ._name  = vkTexResolve->resourceName().c_str(),
                                ._isResolveImage = true
                            },
                            memBarriers.emplace_back()
                        );
                    }
                    else if (descriptor._keepMSAADataAfterResolve)
                    {
                        vkTexture::TransitionTexture(
                            vkTexture::TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ,
                            computeAttachmentSubresourceRange(RT_DEPTH_ATTACHMENT_IDX, true),
                            {
                                ._image = vkTexResolve->image()->_image,
                                ._name  = vkTexResolve->resourceName().c_str(),
                                ._isResolveImage = true
                            },
                            memBarriers.emplace_back()
                        );
                    }

                    attachment->_resolveUsage._usage = targetDepthResolveUsage;
                    if (attachment->_descriptor._externalAttachment != nullptr)
                    {
                        attachment->_descriptor._externalAttachment->_resolveUsage._usage = targetDepthResolveUsage;
                    }
                }
                else if constexpr (ENABLED_DEBUG_PRINTING)
                {
                    Console::d_errorfn("vkRenderTarget::transitionAttachments skip [ {} ]", vkTexResolve->resourceName().c_str());
                }

            }
        }

        if ( !memBarriers.empty() )
        {
            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = to_U32(memBarriers.size());
            dependencyInfo.pImageMemoryBarriers = memBarriers.data();
            vkTexture::FlushPipelineBarriers(cmdBuffer, dependencyInfo);
            memBarriers.reset_lose_memory();
        }
    }

    void vkRenderTarget::begin( VkCommandBuffer cmdBuffer, const RTDrawDescriptor& descriptor, const RTClearDescriptor& clearPolicy, VkPipelineRenderingCreateInfo& pipelineRenderingCreateInfoOut )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );
        if constexpr (ENABLED_DEBUG_PRINTING)
        {
            Console::d_errorfn("vkRenderTarget::begin [ {} ]", name().c_str());
        }
        constexpr RTTransitionMask s_defaultTransitionMask = {true, true, true, true, true };

        assert(pipelineRenderingCreateInfoOut.sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);

        _renderInfoState._keptMSAAData = descriptor._keepMSAADataAfterResolve;

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
                        const bool isCube = IsCubeTexture(vkTexRender->descriptor()._texType);

                        targetColourLayer = descriptor._writeLayers[i]._layer._offset == INVALID_INDEX ? targetColourLayer : descriptor._writeLayers[i];

                        if (isCube)
                        {
                            imageViewDescriptor._subRange._layerRange = { to_U16(targetColourLayer._cubeFace + (targetColourLayer._layer._offset * 6u)),
                                                                          to_U16(targetColourLayer._layer._count * 6u) };
                        }
                        else
                        {
                            DIVIDE_GPU_ASSERT(targetColourLayer._cubeFace == 0u);
                            imageViewDescriptor._subRange._layerRange = targetColourLayer._layer;
                        }
                    }

                    imageViewDescriptor._type = TextureType::TEXTURE_2D;
                    if ( descriptor._mipWriteLevel != ALL_MIPS )
                    {
                        imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                        _renderInfoState._layeredInfo._layeredRendering[i]._enabled = true;
                        _renderInfoState._layeredInfo._layeredRendering[i]._mipLevel = descriptor._mipWriteLevel;
                    }

                    if (imageViewDescriptor._subRange._layerRange._count > 1u)
                    {
                        _renderInfoState._layeredInfo._layeredRendering[i]._enabled = true;
                        _renderInfoState._layeredInfo._layeredRendering[i]._layerRange = imageViewDescriptor._subRange._layerRange;
                        imageViewDescriptor._type = TextureType::TEXTURE_2D_ARRAY;
                    }

                    imageViewDescriptor._resolveTarget = false;
                    imageViewDescriptor._format        = vkTexRender->vkFormat();
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
                    else if ( resolveMSAA && !descriptor._keepMSAADataAfterResolve && !_renderInfoState._keptMSAAData)
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
                  // Compute the attachment's effective layer count (handle 3D / array / cubemap)
                const bool isCube = IsCubeTexture( vkTexRender->descriptor()._texType );

                targetDepthLayer = descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset == INVALID_INDEX ? targetDepthLayer : descriptor._writeLayers[RT_DEPTH_ATTACHMENT_IDX];
                if ( isCube )
                {
                    imageViewDescriptor._subRange._layerRange = { to_U16(targetDepthLayer._cubeFace + (targetDepthLayer._layer._offset * 6u)),
                                                                  to_U16(targetDepthLayer._layer._count * 6u) };
                }
                else
                {
                    assert( targetColourLayer._cubeFace == 0u );
                    imageViewDescriptor._subRange._layerRange = targetDepthLayer._layer;
                }
            }

            imageViewDescriptor._type = TextureType::TEXTURE_2D;
            if ( descriptor._mipWriteLevel != ALL_MIPS )
            {
                imageViewDescriptor._subRange._mipLevels = { descriptor._mipWriteLevel, 1u };
                _renderInfoState._layeredInfo._layeredRendering[RT_DEPTH_ATTACHMENT_IDX]._enabled = true;
                _renderInfoState._layeredInfo._layeredRendering[RT_DEPTH_ATTACHMENT_IDX]._mipLevel = descriptor._mipWriteLevel;
            }
            if (imageViewDescriptor._subRange._layerRange._count > 1u)
            {
                layerCount = imageViewDescriptor._subRange._layerRange._count;
                _renderInfoState._layeredInfo._layeredRendering[RT_DEPTH_ATTACHMENT_IDX]._enabled = true;
                _renderInfoState._layeredInfo._layeredRendering[RT_DEPTH_ATTACHMENT_IDX]._layerRange = imageViewDescriptor._subRange._layerRange;
                imageViewDescriptor._type = TextureType::TEXTURE_2D_ARRAY;
            }

            imageViewDescriptor._resolveTarget = false;
            imageViewDescriptor._format        = vkTexRender->vkFormat();
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
                _depthAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
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


        _renderingInfo.layerCount = layerCount;
        transitionAttachments( cmdBuffer, descriptor, s_defaultTransitionMask, true );

        _previousPolicy = descriptor;
    }

    void vkRenderTarget::end( VkCommandBuffer cmdBuffer, const RTTransitionMask& mask )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        if constexpr (ENABLED_DEBUG_PRINTING)
        {
            Console::d_errorfn("vkRenderTarget::end [ {} ]", name().c_str());
        }
        transitionAttachments( cmdBuffer, _previousPolicy, mask, false );

        _renderInfoState = {};
    }
}; //namespace Divide
