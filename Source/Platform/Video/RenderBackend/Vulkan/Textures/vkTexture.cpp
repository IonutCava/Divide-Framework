

#include "Headers/vkTexture.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"

namespace Divide
{
    namespace
    {
        VkFlags GetFlagForUsage( const ImageUsage usage , const TextureDescriptor& descriptor) noexcept
        {
            DIVIDE_ASSERT(usage != ImageUsage::COUNT);

            const bool multisampled = descriptor.msaaSamples() > 0u;
            const bool compressed = IsCompressed( descriptor.baseFormat() );
            const bool isDepthTexture = IsDepthTexture( descriptor.packing() );
            bool supportsStorageBit = !multisampled && !compressed && !isDepthTexture;

            
            VkFlags ret = (usage != ImageUsage::SHADER_WRITE ? VK_IMAGE_USAGE_SAMPLED_BIT : VK_FLAGS_NONE);

            switch ( usage )
            {
                case ImageUsage::SHADER_READ_WRITE:
                case ImageUsage::SHADER_WRITE: DIVIDE_ASSERT( supportsStorageBit );  break;

                case ImageUsage::RT_COLOUR_ATTACHMENT: 
                case ImageUsage::RT_DEPTH_ATTACHMENT:
                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                {
                    supportsStorageBit = false;
                    ret |=  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                    ret |= ( usage == ImageUsage::RT_COLOUR_ATTACHMENT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                    
                } break;
            }

            if ( descriptor.allowRegionUpdates() )
            {
                ret |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }

            return (supportsStorageBit ? (ret | VK_IMAGE_USAGE_STORAGE_BIT) : ret);
        }

        enum class CopyTextureType : U8
        {
            COLOUR = 0,
            DEPTH,
            GENERAL
        };

        void CopyInternal( VkCommandBuffer cmdBuffer, VkImage source, const VkImageAspectFlags sourceAspect, const CopyTextureType sourceType, VkImage destination, const VkImageAspectFlags destinationAspect, const CopyTextureType destinationType, const CopyTexParams& params, const U16 depth )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 2u;

            std::array<VkImageMemoryBarrier2, 2> imageBarriers{};
            U8 imageBarrierCount = 0u;

            const VkImageSubresourceRange subResourceRangeSrc = {
                .aspectMask = sourceAspect,
                .baseMipLevel = params._sourceMipLevel,
                .levelCount = 1u,
                .baseArrayLayer = params._layerRange.offset,
                .layerCount = params._layerRange.count
            };
            const VkImageSubresourceRange subResourceRangeDst = {
                .aspectMask = destinationAspect,
                .baseMipLevel = params._targetMipLevel,
                .levelCount = 1u,
                .baseArrayLayer = params._layerRange.offset,
                .layerCount = params._layerRange.count
            };

            {
                const vkTexture::TransitionType sourceTransition = sourceType == CopyTextureType::COLOUR ? vkTexture::TransitionType::SHADER_READ_TO_COPY_READ_COLOUR : sourceType == CopyTextureType::DEPTH ? vkTexture::TransitionType::SHADER_READ_TO_COPY_READ_DEPTH : vkTexture::TransitionType::GENERAL_TO_COPY_READ;
                const vkTexture::TransitionType destinationTransition = destinationType == CopyTextureType::DEPTH ? vkTexture::TransitionType::SHADER_READ_TO_COPY_WRITE_DEPTH : vkTexture::TransitionType::SHADER_READ_TO_COPY_WRITE_COLOUR;

                imageBarrierCount = 0u;
                vkTexture::TransitionTexture( sourceTransition, subResourceRangeSrc, source, imageBarriers[imageBarrierCount++] );
                vkTexture::TransitionTexture( destinationTransition, subResourceRangeDst, destination, imageBarriers[imageBarrierCount++] );
            }

            dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
            VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );

            //ToDo: z and layer range need better handling for 3D textures and cubemaps!
            VkImageCopy region{};
            region.srcSubresource = 
            {
              .aspectMask = subResourceRangeSrc.aspectMask,
              .mipLevel = subResourceRangeSrc.baseMipLevel,
              .baseArrayLayer = subResourceRangeSrc.baseArrayLayer,
              .layerCount = subResourceRangeSrc.layerCount
            };
            region.srcOffset.x = params._sourceCoords.x;
            region.srcOffset.y = params._sourceCoords.y;
            region.srcOffset.z = 0;
            region.dstSubresource =
            {
              .aspectMask = subResourceRangeDst.aspectMask,
              .mipLevel = subResourceRangeDst.baseMipLevel,
              .baseArrayLayer = subResourceRangeDst.baseArrayLayer,
              .layerCount = subResourceRangeDst.layerCount
            };
            region.dstOffset.x = params._targetCoords.x;
            region.dstOffset.y = params._targetCoords.y;
            region.dstOffset.z = 0;
            region.extent.width = params._dimensions.width;
            region.extent.height = params._dimensions.height;
            region.extent.depth = depth;

            VK_PROFILE( vkCmdCopyImage, cmdBuffer,
                                        source,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        destination,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        1,
                                        &region );

            {
                const vkTexture::TransitionType sourceTransition = sourceType == CopyTextureType::COLOUR ? vkTexture::TransitionType::COPY_READ_TO_SHADER_READ_COLOUR : sourceType == CopyTextureType::DEPTH ? vkTexture::TransitionType::COPY_READ_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::COPY_READ_TO_GENERAL;
                const vkTexture::TransitionType destinationTransition = destinationType == CopyTextureType::COLOUR ? vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR : destinationType == CopyTextureType::DEPTH ? vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::COPY_WRITE_TO_GENERAL;

                imageBarrierCount = 0u;
                vkTexture::TransitionTexture( sourceTransition, subResourceRangeSrc, source, imageBarriers[imageBarrierCount++] );
                vkTexture::TransitionTexture( destinationTransition, subResourceRangeDst, destination, imageBarriers[imageBarrierCount++] );
            }

            dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
            VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
        }
    }

    AllocatedImage::~AllocatedImage()
    {
        if ( _image != VK_NULL_HANDLE )
        {
            VK_API::RegisterCustomAPIDelete( [img = _image, alloc = _allocation]( [[maybe_unused]] VkDevice device )
                                             {
                                                 LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
                                                 vmaDestroyImage( *VK_API::GetStateTracker()._allocatorInstance._allocator, img, alloc );
                                             }, true );
        }
    }

    size_t vkTexture::CachedImageView::Descriptor::getHash() const noexcept
    {
        _hash = 1337;
        Util::Hash_combine( _hash,
                            _subRange._layerRange._offset,
                            _subRange._layerRange._count,
                            _subRange._mipLevels._offset,
                            _subRange._mipLevels._count,
                            _format,
                            _type,
                            _usage,
                            _resolveTarget);
        return _hash;
    }


    VkImageAspectFlags vkTexture::GetAspectFlags( const TextureDescriptor& descriptor ) noexcept
    {
        const bool hasDepthStencil = descriptor.hasUsageFlagSet( ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
        const bool hasDepth = descriptor.hasUsageFlagSet( ImageUsage::RT_DEPTH_ATTACHMENT ) || hasDepthStencil;

        return hasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (hasDepthStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u)
                        : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    vkTexture::vkTexture( GFXDevice& context,
                          const size_t descriptorHash,
                          const std::string_view name,
                          std::string_view assetNames,
                          const ResourcePath& assetLocations,
                          const TextureDescriptor& texDescriptor,
                          ResourceCache& parentCache )
        : Texture( context, descriptorHash, name, assetNames, assetLocations, texDescriptor, parentCache )
    {
    }

    vkTexture::~vkTexture()
    {
        clearImageViewCache();
    }

    void vkTexture::clearImageViewCache()
    {
        if ( !_imageViewCache.empty() )
        {
            VK_API::RegisterCustomAPIDelete( [views = _imageViewCache]( VkDevice device )
                                             {
                                                 for ( auto [_, imageView] : views )
                                                 {
                                                     vkDestroyImageView( device, imageView._view, nullptr );
                                                 }
                                             }, true );
            _imageViewCache.clear();
        }
    }

    bool vkTexture::unload()
    {
        clearImageViewCache();
        return Texture::unload();
    }

    void vkTexture::generateMipmaps( VkCommandBuffer cmdBuffer, const U16 baseLevel, U16 baseLayer, U16 layerCount, const ImageUsage crtUsage )
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        VK_API::PushDebugMessage(cmdBuffer, "vkTexture::generateMipmaps");


        VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.imageMemoryBarrierCount = 1u;
        dependencyInfo.pImageMemoryBarriers = &memBarrier;

        if ( IsCubeTexture( descriptor().texType() ) )
        {
            baseLayer *= 6u;
            layerCount *= 6u;
        }

        VkImageSubresourceRange subResourceRange = {
            .aspectMask = GetAspectFlags( descriptor() ),
            .baseMipLevel = baseLevel,
            .levelCount = 1u,
            .baseArrayLayer = baseLayer,
            .layerCount = layerCount
        };

        {
            switch ( crtUsage )
            {
                case ImageUsage::UNDEFINED:
                {
                    NOP();
                } break;
                case ImageUsage::SHADER_READ:
                {
                    TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::SHADER_READ_TO_COPY_READ_DEPTH : TransitionType::SHADER_READ_TO_COPY_READ_COLOUR, subResourceRange, image()->_image, memBarrier);
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_WRITE:
                {
                    TransitionTexture( TransitionType::GENERAL_TO_COPY_READ, subResourceRange, image()->_image, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_READ_WRITE:
                {
                    TransitionTexture( TransitionType::SHADER_READ_WRITE_TO_COPY_READ, subResourceRange, image()->_image, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                default: DIVIDE_UNEXPECTED_CALL_MSG("To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!"); break;
            }
        }
        subResourceRange.levelCount = 1u;

        VkImageBlit image_blit{};
        image_blit.srcSubresource.aspectMask = GetAspectFlags( _descriptor );
        image_blit.srcSubresource.baseArrayLayer = baseLayer;
        image_blit.srcSubresource.layerCount = layerCount;
        image_blit.dstSubresource.aspectMask = GetAspectFlags( _descriptor );
        image_blit.dstSubresource.baseArrayLayer = baseLayer;
        image_blit.dstSubresource.layerCount = layerCount;
        image_blit.srcOffsets[1].z = 1;
        image_blit.dstOffsets[1].z = 1;

        for ( U16 m = baseLevel + 1u; m < mipCount(); ++m )
        {
            PROFILE_VK_EVENT( "Mip Loop" );

            // Source
            const int32_t mipWidthSrc = int32_t( _width >> ( m - 1) );
            const int32_t mipHeightSrc = int32_t( _height >> (m - 1) );

            image_blit.srcSubresource.mipLevel = m - 1u;
            image_blit.srcOffsets[1].x = mipWidthSrc > 1 ? mipWidthSrc : 1;
            image_blit.srcOffsets[1].y = mipHeightSrc > 1 ? mipHeightSrc : 1;

            // Destination
            const int32_t mipWidthDst = int32_t( _width >> m );
            const int32_t mipHeightDst = int32_t( _height >> m );

            image_blit.dstSubresource.mipLevel = m;
            image_blit.dstOffsets[1].x = mipWidthDst > 1 ? mipWidthDst : 1;
            image_blit.dstOffsets[1].y = mipHeightDst > 1 ? mipHeightDst : 1;

            subResourceRange.baseMipLevel = m;
            {// Prepare current mip level as image blit destination
                TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, subResourceRange, image()->_image, memBarrier );
                VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
            }
            {
                // Blit from previous level
                VK_PROFILE( vkCmdBlitImage, cmdBuffer,
                                            _image->_image,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            _image->_image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            1,
                                            &image_blit,
                                            VK_FILTER_LINEAR );
            }
            // Prepare current mip level as image blit source for next level
            {
                TransitionTexture( TransitionType::COPY_WRITE_TO_COPY_READ, subResourceRange, image()->_image, memBarrier );
                VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
            }
        }

        // After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to their previous usage
        subResourceRange.baseMipLevel = baseLevel;
        subResourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

        {
            switch ( crtUsage )
            {
                case ImageUsage::UNDEFINED:
                case ImageUsage::SHADER_READ:
                {
                    TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::COPY_READ_TO_SHADER_READ_DEPTH : TransitionType::COPY_READ_TO_SHADER_READ_COLOUR, subResourceRange, image()->_image, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_WRITE:
                case ImageUsage::SHADER_READ_WRITE:
                {
                    TransitionTexture( TransitionType::COPY_READ_TO_GENERAL, subResourceRange, image()->_image, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                default: DIVIDE_UNEXPECTED_CALL_MSG( "To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!" ); break;
            }
        }
        VK_API::PopDebugMessage(cmdBuffer);
    }

    void vkTexture::prepareTextureData( const U16 width, const U16 height, const U16 depth, const bool emptyAllocation )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        Texture::prepareTextureData( width, height, depth, emptyAllocation );

        vkFormat( VKUtil::InternalFormat( _descriptor.baseFormat(), _descriptor.dataType(), _descriptor.packing() ) );
        
        if ( _image != nullptr )
        {
            ++_testRefreshCounter;
        }
        clearImageViewCache();

        _image = std::make_unique<AllocatedImage>();
        _vkType = vkTextureTypeTable[to_base( descriptor().texType() )];

        sampleFlagBits( VK_SAMPLE_COUNT_1_BIT );
        if ( _descriptor.msaaSamples() > 0u )
        {
            assert( isPowerOfTwo( _descriptor.msaaSamples() ) );
            sampleFlagBits( static_cast<VkSampleCountFlagBits>(_descriptor.msaaSamples()) );
        }

        VkImageCreateInfo imageInfo = vk::imageCreateInfo();
        imageInfo.tiling = _descriptor.allowRegionUpdates() ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.samples = sampleFlagBits();
        imageInfo.format = vkFormat();
        imageInfo.imageType = vkType();
        imageInfo.mipLevels = mipCount();
        imageInfo.extent.width = to_U32( _width );
        imageInfo.extent.height = to_U32( _height );

        if ( descriptor().texType() == TextureType::TEXTURE_3D )
        {
            imageInfo.extent.depth = to_U32( _depth );
            imageInfo.arrayLayers = 1;
        }
        else
        {
            imageInfo.extent.depth = 1;
            imageInfo.arrayLayers = to_U32(_depth);
        }

        if ( !emptyAllocation || imageInfo.mipLevels > 1u)
        {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for ( U8 i = 0u; i < to_base( ImageUsage::COUNT ); ++i )
        {
            const ImageUsage testUsage = static_cast<ImageUsage>(i);
            if ( !_descriptor.hasUsageFlagSet( testUsage ) )
            {
                continue;
            }
            imageInfo.usage |= GetFlagForUsage( testUsage, _descriptor);
        }

        if ( !IsCompressed( _descriptor.baseFormat() ) )
        {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        if ( vkType() == VK_IMAGE_TYPE_3D )
        {
            imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        }
        if ( IsCubeTexture( _descriptor.texType() ) )
        {
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        if ( emptyAllocation )
        {
            imageInfo.flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
            //ToDo: Figure out why the validation layers complain if we skip this flag -Ionut:
            imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }
        VmaAllocationCreateInfo vmaallocinfo = {};
        vmaallocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocinfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        vmaallocinfo.priority = 1.f;

        {
            LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            VK_CHECK( vmaCreateImage( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                      &imageInfo,
                                      &vmaallocinfo,
                                      &_image->_image,
                                      &_image->_allocation,
                                      &_image->_allocInfo ) );

            auto imageName = Util::StringFormat( "{}_{}", resourceName().c_str(), _testRefreshCounter );
            vmaSetAllocationName( *VK_API::GetStateTracker()._allocatorInstance._allocator, _image->_allocation, imageName.c_str() );
            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_image->_image, VK_OBJECT_TYPE_IMAGE, imageName.c_str() );
        }

        if ( emptyAllocation && sampleFlagBits() == VK_SAMPLE_COUNT_1_BIT )
        {
            const PixelAlignment alignment{ ._alignment = 1u };
            loadDataInternal( nullptr, _image->_allocInfo.size, 0u, {0u}, {_width, _height, 1u}, alignment, true );
        }
    }

    void vkTexture::loadDataInternal( const Byte* data, const size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        DIVIDE_ASSERT(_descriptor.allowRegionUpdates());
        loadDataInternal(data, size, targetMip, offset, dimensions, pixelUnpackAlignment, false);
    }

    void vkTexture::loadDataInternal( const Byte* data, const size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment, [[maybe_unused]] const bool emptyAllocation )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( size == 0u )
        {
            return;
        }

        VkImageSubresource  range;
        range.aspectMask = GetAspectFlags( _descriptor );
        range.mipLevel = to_I32(targetMip);
        range.arrayLayer = to_I32(offset.depth);

        const U16 topLeftX = offset.x;
        const U16 topLeftY = offset.y;
        const U16 bottomRightX = dimensions.x + offset.x;
        const U16 bottomRightY = dimensions.y + offset.y;

        DIVIDE_ASSERT( offset.z == 0u && dimensions.z == 1u, "vkTexture::loadDataInternal: 3D textures not supported for sub-image updates!");

        const U8 bpp_dest = GetBytesPerPixel( _descriptor.dataType(), _descriptor.baseFormat(), _descriptor.packing() );
        const size_t rowOffset_dest = (bpp_dest * pixelUnpackAlignment._alignment) * _width;
        const U16 subHeight = bottomRightY - topLeftY;
        const U16 subWidth = bottomRightX - topLeftX;
        const size_t subRowSize = subWidth * (pixelUnpackAlignment._alignment * bpp_dest);
        const size_t pitch = pixelUnpackAlignment._rowLength == 0u ? rowOffset_dest : pixelUnpackAlignment._rowLength;

        size_t totalSize = 0u;
        U16 mipWidth = _width;
        U16 mipHeight = _height;

        _mipData.resize( mipCount() );

        for ( U32 l = 0u; l < _depth; ++l )
        {
            for ( U8 m = 0u; m < mipCount(); ++m )
            {
                mipWidth = _width >> m;
                mipHeight = _height >> m;

                _mipData[m]._dimensions = {mipWidth, mipHeight, _depth};
                _mipData[m]._size = mipWidth * mipHeight * _depth * bpp_dest;
                totalSize += _mipData[m]._size;

            }
        }

        if ( _stagingBuffer == nullptr || _stagingBuffer->_allocInfo.size < totalSize)
        {
            _stagingBuffer = VKUtil::createStagingBuffer( totalSize, resourceName().c_str(), false );
        }

        const size_t dstOffset = topLeftY * rowOffset_dest + topLeftX * (pixelUnpackAlignment._alignment * bpp_dest);

        Byte* mappedData = (Byte*)_stagingBuffer->_allocInfo.pMappedData;
        Byte* dstData = &mappedData[dstOffset];

        if ( data != nullptr )
        {
            const size_t srcOffset = pixelUnpackAlignment._skipPixels + (pixelUnpackAlignment._skipRows * _width);
            const Byte* srcData = &data[srcOffset];

            for ( U16 i = 0u; i < subHeight; i++ )
            {
                memcpy( dstData, srcData, subRowSize );
                dstData += rowOffset_dest;
                srcData += pitch;
            }
        }
        else
        {
            for ( U16 i = 0u; i < subHeight; i++ )
            {
                memset( dstData, 0u, subRowSize );
                dstData += rowOffset_dest;
            }
        }

        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmd );

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags( _descriptor );
            range.levelCount = 1;
            range.layerCount = 1;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            size_t dataOffset = 0u;
            for ( U32 l = 0u; l < _depth; ++l )
            {
                for ( U8 m = 0u; m < mipCount(); ++m )
                {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = l;

                    TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, range, _image->_image, memBarrier);
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );
                    
                    VkBufferImageCopy copyRegion = {};
                    copyRegion.bufferOffset = dataOffset;
                    copyRegion.bufferRowLength = 0u;
                    copyRegion.bufferImageHeight = 0u;
                    copyRegion.imageSubresource.aspectMask = range.aspectMask;
                    copyRegion.imageSubresource.mipLevel = m;
                    copyRegion.imageSubresource.baseArrayLayer = l;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset.x = 0u;
                    copyRegion.imageOffset.y = 0u;
                    copyRegion.imageOffset.z = 0u;
                    copyRegion.imageExtent.width = _mipData[m]._dimensions.width;
                    copyRegion.imageExtent.height = _mipData[m]._dimensions.height;
                    copyRegion.imageExtent.depth = 1u;//_mipData[m]._dimensions.depth;

                    //copy the buffer into the image
                    VK_PROFILE( vkCmdCopyBufferToImage, cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                    TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR, range, _image->_image, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );

                    dataOffset += _mipData[m]._size;
                }
            }
        }, "vkTexture::loadDataInternal" );
    }

    void vkTexture::loadDataInternal( const ImageTools::ImageData& imageData, const vec3<U16>& offset, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const U16 numLayers = imageData.layerCount();
        const U8 numMips = imageData.mipCount();
        _mipData.resize(numMips);

        U16 maxDepth = 0u;
        size_t totalSize = 0u;
        for ( U32 l = 0u; l < numLayers; ++l )
        {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for ( U8 m = 0u; m < numMips; ++m )
            {
                const ImageTools::LayerData* mip = layer.getMip( m );
                totalSize += mip->_size;
                _mipData[m]._size = mip->_size;
                _mipData[m]._dimensions = mip->_dimensions;
                maxDepth = std::max( maxDepth, mip->_dimensions.depth );
            }
        }
        DIVIDE_ASSERT( _depth >= maxDepth );

        if ( _stagingBuffer == nullptr )
        {
            _stagingBuffer = VKUtil::createStagingBuffer( totalSize, resourceName().c_str(), false);
        }
        
        Byte* target = (Byte*)_stagingBuffer->_allocInfo.pMappedData;

        size_t dataOffset = 0u;
        for ( U32 l = 0u; l < numLayers; ++l )
        {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for ( U8 m = 0u; m < numMips; ++m )
            {
                const ImageTools::LayerData* mip = layer.getMip( m );
                memcpy( &target[dataOffset], mip->data(), mip->_size );
                dataOffset += mip->_size;
            }
        }

        const bool needsMipMaps = _descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO && numMips < mipCount();
        const bool hasMipMaps = mipCount() > 1u;

        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmd );

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags( _descriptor );
            range.levelCount = 1;
            range.layerCount = 1;


            if ( hasMipMaps && !needsMipMaps )
            {
                range.levelCount = VK_REMAINING_MIP_LEVELS;
            }

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            size_t dataOffset = 0u;
            for ( U32 l = 0u; l < numLayers; ++l )
            {
                const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

                for ( U8 m = 0u; m < numMips; ++m )
                {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = l;

                    TransitionTexture(TransitionType::GENERAL_TO_COPY_WRITE, range, _image->_image, memBarrier);
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );
                    
                    const ImageTools::LayerData* mip = layer.getMip( m );

                    VkBufferImageCopy copyRegion = {};
                    copyRegion.bufferOffset = dataOffset;
                    copyRegion.bufferRowLength = to_U32(pixelUnpackAlignment._rowLength);
                    copyRegion.bufferImageHeight = 0;
                    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.imageSubresource.mipLevel = m;
                    copyRegion.imageSubresource.baseArrayLayer = l;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset.x = offset.x;
                    copyRegion.imageOffset.y = offset.y;
                    copyRegion.imageOffset.z = offset.z;
                    copyRegion.imageExtent.width = mip->_dimensions.width;
                    copyRegion.imageExtent.height = mip->_dimensions.height;
                    copyRegion.imageExtent.depth = mip->_dimensions.depth;

                    //copy the buffer into the image
                    VK_PROFILE( vkCmdCopyBufferToImage, cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                    if ( needsMipMaps && m + 1u == numMips )
                    {
                        TransitionTexture( TransitionType::COPY_WRITE_TO_COPY_READ, range, _image->_image, memBarrier );
                    }
                    else
                    {
                        TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR, range, _image->_image, memBarrier );
                    }

                    //barrier the image into the shader readable layout
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );

                    dataOffset += mip->_size;
                }
            }

            if ( needsMipMaps )
            {
                generateMipmaps( cmd, 0u, 0u, numLayers, ImageUsage::UNDEFINED );
            }

            if ( !_descriptor.allowRegionUpdates() )
            {
                _stagingBuffer.reset();
            }

        }, "vkTexture::loadDataInternal" );
    }

    void vkTexture::clearData( VkCommandBuffer cmdBuffer, const UColour4& clearColour, SubRange layerRange, U8 mipLevel ) const noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        if ( mipLevel == U8_MAX )
        {
            assert( mipCount() > 0u );
            mipLevel = to_U8( mipCount() - 1u );
        }

        VkImageSubresourceRange range;
        range.aspectMask = GetAspectFlags( _descriptor );
        range.baseArrayLayer = layerRange._offset;
        range.layerCount = layerRange._count == U16_MAX ? VK_REMAINING_ARRAY_LAYERS : layerRange._count;
        range.baseMipLevel = mipLevel;
        range.levelCount = 1u;

        VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.imageMemoryBarrierCount = 1u;
        dependencyInfo.pImageMemoryBarriers = &memBarrier;

        TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::SHADER_READ_TO_BLIT_WRITE_DEPTH : TransitionType::SHADER_READ_TO_BLIT_WRITE_COLOUR, range, _image->_image, memBarrier );
        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );

        if ( IsDepthTexture( _descriptor.packing() ) )
        {
            const VkClearDepthStencilValue clearValue = {
                .depth = to_F32(clearColour.r),
                .stencil = to_U32(clearColour.g)
            };

            VK_PROFILE( vkCmdClearDepthStencilImage, cmdBuffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
        }
        else
        {
            const VkClearColorValue clearValue =
            {
                .uint32 = {clearColour.r,clearColour.g,clearColour.b,clearColour.a}
            };

            VK_PROFILE( vkCmdClearColorImage, cmdBuffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
        }

        TransitionTexture( IsDepthTexture( _descriptor.packing() ) ? TransitionType::BLIT_WRITE_TO_SHADER_READ_DEPTH : TransitionType::BLIT_WRITE_TO_SHADER_READ_COLOUR, range, _image->_image, memBarrier );
        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
    }

    ImageReadbackData vkTexture::readData( U8 mipLevel, const PixelAlignment& pixelPackAlignment ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        ImageReadbackData ret{};
        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmd );

            ret = readData(cmd, mipLevel, pixelPackAlignment );
        }, "vkTexture::readData()" );
        return ret;
    }

    ImageReadbackData vkTexture::readData( VkCommandBuffer cmdBuffer, U8 mipLevel, [[maybe_unused]] const PixelAlignment& pixelPackAlignment) const noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        ImageReadbackData grabData{};
        grabData._numComponents = numChannels();

        const auto desiredDataFormat = _descriptor.dataType();
        const auto desiredImageFormat = _descriptor.baseFormat();
        const auto desiredPacking = _descriptor.packing();

        grabData._sourceIsBGR = IsBGRTexture( desiredImageFormat );
        grabData._bpp = GetBytesPerPixel( desiredDataFormat, desiredImageFormat, desiredPacking );

        DIVIDE_ASSERT( (grabData._bpp == 3 || grabData._bpp == 4) && _depth == 1u && !IsCubeTexture( _descriptor.texType() ), "vkTexture:readData: unsupported image for readback. Support is very limited!" );
        grabData._width = _width >> mipLevel;
        grabData._height = _height >> mipLevel;

        const size_t sizeDest = grabData._width * grabData._height * grabData._bpp;
        auto stagingBuffer = VKUtil::createStagingBuffer( sizeDest, "STAGING_BUFFER_READ_TEXTURE", true );

        grabData._data.resize(sizeDest);

        // Create the linear tiled destination image to copy to and to read the memory from
        VkImageCreateInfo imageCreateCI = vk::imageCreateInfo();
        imageCreateCI.tiling = VK_IMAGE_TILING_LINEAR;
        imageCreateCI.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCreateCI.imageType = vkType();
        imageCreateCI.extent.width = to_U32( grabData._width );
        imageCreateCI.extent.height = to_U32( grabData._height );
        imageCreateCI.extent.depth = 1;
        imageCreateCI.arrayLayers = 1;
        imageCreateCI.mipLevels = 1;
        imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo vmaallocinfo = {};
        vmaallocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocinfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        vmaallocinfo.priority = 1.f;

        VkImage dstImage;
        VmaAllocation allocation{ VK_NULL_HANDLE };
        {
            LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
            VK_CHECK( vmaCreateImage( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                      &imageCreateCI,
                                      &vmaallocinfo,
                                      &dstImage,
                                      &allocation,
                                      nullptr));
        }

        VK_API::RegisterCustomAPIDelete( [dstImage, allocation]( [[maybe_unused]] VkDevice device )
                                         {
                                             LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
                                             vmaDestroyImage( *VK_API::GetStateTracker()._allocatorInstance._allocator, dstImage, allocation );
                                         }, true );

        CopyTexParams copyParams{};
        copyParams._sourceMipLevel = mipLevel;
        copyParams._targetMipLevel = mipLevel;
        copyParams._dimensions.width = grabData._width;
        copyParams._dimensions.height = grabData._height;
        CopyInternal(cmdBuffer,
                     image()->_image,
                     GetAspectFlags( descriptor() ),
                     IsDepthTexture( _descriptor.packing() ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                     dstImage,
                     GetAspectFlags( descriptor() ),
                     CopyTextureType::GENERAL,
                     copyParams,
                     1u);

        VkBufferMemoryBarrier2 bufferBarrier = vk::bufferMemoryBarrier2();
        // Enable destination buffer to be written to
        {
            bufferBarrier.srcAccessMask = 0;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bufferBarrier.offset = 0;
            bufferBarrier.size = sizeDest;
            bufferBarrier.buffer = stagingBuffer->_buffer;
        }

        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.bufferMemoryBarrierCount = 1u;
        dependencyInfo.pBufferMemoryBarriers = &bufferBarrier;

        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );

        VkBufferImageCopy image_copy_region{};
        image_copy_region.bufferOffset = 0u;
        image_copy_region.bufferRowLength = grabData._width;
        image_copy_region.bufferImageHeight = grabData._height;
        image_copy_region.imageSubresource.aspectMask = GetAspectFlags( descriptor() );
        image_copy_region.imageSubresource.mipLevel = mipLevel;
        image_copy_region.imageSubresource.baseArrayLayer = 0u;
        image_copy_region.imageSubresource.layerCount = 1u;
        image_copy_region.imageOffset.x = 0;
        image_copy_region.imageOffset.y = 0;
        image_copy_region.imageOffset.z = 0;
        image_copy_region.imageExtent.width = grabData._width;
        image_copy_region.imageExtent.height = grabData._height;
        image_copy_region.imageExtent.depth = 1u;

        VK_PROFILE( vkCmdCopyImageToBuffer, cmdBuffer, dstImage, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer->_buffer, 1u, &image_copy_region );


        // Enable destination buffer to map memory
        {
            bufferBarrier.srcAccessMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            bufferBarrier.srcStageMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        }

        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );


        memcpy(grabData._data.data(), (Byte*)stagingBuffer->_allocInfo.pMappedData, sizeDest);

        return grabData;
    }

    VkImageView vkTexture::getImageView( const CachedImageView::Descriptor& viewDescriptor ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const size_t viewHash = viewDescriptor.getHash();

        const auto it = _imageViewCache.find( viewHash );
        if ( it != end( _imageViewCache ) )
        {
            return it->second._view;
        }
        {
            PROFILE_SCOPE( "Cache miss", Profiler::Category::Graphics );

            DIVIDE_ASSERT( viewDescriptor._usage != ImageUsage::COUNT );

            CachedImageView newView{};
            newView._descriptor = viewDescriptor;

            VkImageSubresourceRange range{};
            if ( descriptor().hasUsageFlagSet( ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT ) )
            {
                range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else
            {
                range.aspectMask = GetAspectFlags( _descriptor );
            }

            range.baseMipLevel = newView._descriptor._subRange._mipLevels._offset;
            range.levelCount = newView._descriptor._subRange._mipLevels._count == U16_MAX ? VK_REMAINING_MIP_LEVELS : newView._descriptor._subRange._mipLevels._count;
            range.baseArrayLayer = newView._descriptor._subRange._layerRange._offset;
            range.layerCount = newView._descriptor._subRange._layerRange._count == U16_MAX ? VK_REMAINING_ARRAY_LAYERS : newView._descriptor._subRange._layerRange._count;

            VkImageViewCreateInfo imageInfo = vk::imageViewCreateInfo();
            imageInfo.image = _image->_image;
            imageInfo.format = newView._descriptor._format;
            imageInfo.viewType = vkTextureViewTypeTable[to_base( newView._descriptor._type )];
            imageInfo.subresourceRange = range;

            VkImageViewUsageCreateInfo viewCreateInfo = vk::imageViewUsageCreateInfo();
            if ( !viewDescriptor._resolveTarget )
            {
                viewCreateInfo.usage = GetFlagForUsage( newView._descriptor._usage, _descriptor);
                imageInfo.pNext = &viewCreateInfo;
            }

            VK_CHECK( vkCreateImageView( VK_API::GetStateTracker()._device->getVKDevice(), &imageInfo, nullptr, &newView._view ) );

            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)newView._view, VK_OBJECT_TYPE_IMAGE_VIEW, Util::StringFormat("{}_view_{}", resourceName().c_str(), _imageViewCache.size()).c_str() );
            hashAlg::emplace(_imageViewCache, viewHash, newView);
            return newView._view;
        }
    }

    /*static*/ void vkTexture::Copy(VkCommandBuffer cmdBuffer, const vkTexture* source, const U8 sourceSamples, const vkTexture* destination, const U8 destinationSamples, CopyTexParams params)
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEX( cmdBuffer );

        // We could handle this with a custom shader pass and temp render targets, so leaving the option i
        DIVIDE_ASSERT( sourceSamples == destinationSamples == 0u, "vkTexture::copy Multisampled textures is not supported yet!" );
        DIVIDE_ASSERT( source != nullptr && destination != nullptr, "vkTexture::copy Invalid source and/or destination textures specified!" );

        const TextureType srcType = source->descriptor().texType();
        const TextureType dstType = destination->descriptor().texType();
        assert( srcType != TextureType::COUNT && dstType != TextureType::COUNT );

        U32 layerOffset = params._layerRange.offset;
        U32 layerCount = params._layerRange.count == U16_MAX ? source->_depth : params._layerRange.count;
        if ( IsCubeTexture( srcType ) )
        {
            layerOffset *= 6;
            layerCount *= 6;
        }
        params._layerRange = { layerOffset, layerCount };

        if ( srcType != TextureType::COUNT && dstType != TextureType::COUNT )
        {

            U16 depth = 1u;
            if ( source->descriptor().texType() == TextureType::TEXTURE_3D )
            {
                depth = source->_depth;
            }

            CopyInternal( cmdBuffer, 
                          source->_image->_image,
                          GetAspectFlags( source->descriptor() ),
                          IsDepthTexture( source->descriptor().packing() ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                          destination->_image->_image,
                          GetAspectFlags( destination->descriptor() ),
                          IsDepthTexture( destination->descriptor().packing() ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                          params,
                          depth );
        }
    }

    /*static*/ void vkTexture::TransitionTexture(const TransitionType type, const VkImageSubresourceRange & subresourceRange, VkImage image, VkImageMemoryBarrier2 & memBarrier)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        memBarrier = vk::imageMemoryBarrier2();
        memBarrier.image = image;
        memBarrier.subresourceRange = subresourceRange;

        constexpr auto SHADER_READ_WRITE_BIT = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        constexpr auto SHADER_SAMPLE_STAGE_MASK = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        switch ( type )
        {
            case TransitionType::UNDEFINED_TO_COLOUR_RESOLVE_ATTACHMENT:
            {
                    memBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::UNDEFINED_TO_COLOUR_ATTACHMENT:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            } break;
            case TransitionType::UNDEFINED_TO_DEPTH_RESOLVE_ATTACHMENT :
            case TransitionType::UNDEFINED_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT :
            {
                memBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::UNDEFINED_TO_DEPTH_ATTACHMENT :
            case TransitionType::UNDEFINED_TO_DEPTH_STENCIL_ATTACHMENT :
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                if ( type == TransitionType::UNDEFINED_TO_DEPTH_STENCIL_ATTACHMENT  || type == TransitionType::UNDEFINED_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT )
                {
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                }

            } break;
            case TransitionType::SHADER_READ_TO_COLOUR_RESOLVE_ATTACHMENT:
            {
                memBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_DEPTH_RESOLVE_ATTACHMENT:
            case TransitionType::SHADER_READ_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT:
            {
                memBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT:
            case TransitionType::SHADER_READ_TO_DEPTH_STENCIL_ATTACHMENT:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                if ( type == TransitionType::SHADER_READ_TO_DEPTH_STENCIL_ATTACHMENT || type == TransitionType::SHADER_READ_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT )
                {
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                }
            } break;
            case TransitionType::COLOUR_RESOLVE_ATTACHMENT_TO_SHADER_READ:
            {
                memBarrier.srcStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ:
            {
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                if ( type == TransitionType::COLOUR_RESOLVE_ATTACHMENT_TO_SHADER_READ )
                {
                    
                }
            } break;
            case TransitionType::DEPTH_RESOLVE_ATTACHMENT_TO_SHADER_READ:
            case TransitionType::DEPTH_STENCIL_RESOLVE_ATTACHMENT_TO_SHADER_READ:
            {
                memBarrier.srcStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
            } [[fallthrough]];
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ:
            case TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ:
            {
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

                if ( type == TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ || type == TransitionType::DEPTH_STENCIL_RESOLVE_ATTACHMENT_TO_SHADER_READ )
                {
                    memBarrier.oldLayout =  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                }
            } break;
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

            } break;
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_WRITE:
            case TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

                if ( type == TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_WRITE )
                {
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ_WRITE:
            case TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

                if ( type == TransitionType::DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ_WRITE )
                {
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                }

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::GENERAL_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_COLOUR_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_DEPTH_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_COLOUR_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_DEPTH_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_WRITE_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::BLIT_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_WRITE_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::BLIT_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_WRITE_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_WRITE_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::COPY_READ_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            } break;
            case TransitionType::COPY_READ_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask = VK_API::ALL_SHADER_STAGES;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_COPY_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::COPY_READ_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COPY_WRITE_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COPY_WRITE_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            }break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        };
    } 

}; //namespace Divide
