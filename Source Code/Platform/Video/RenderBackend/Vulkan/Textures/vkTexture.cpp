#include "stdafx.h"

#include "Headers/vkTexture.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"

namespace Divide
{
    namespace
    {
        FORCE_INLINE [[nodiscard]] U8 GetBytesPerPixel( const GFXDataFormat format, const GFXImageFormat baseFormat ) noexcept
        {
            return Texture::GetSizeFactor( format ) * NumChannels( baseFormat );
        }

        VkFlags GetFlagForUsage( const ImageUsage usage , const TextureDescriptor& descriptor) noexcept
        {
            DIVIDE_ASSERT(usage != ImageUsage::COUNT);

            const bool multisampled = descriptor.msaaSamples() > 0u;
            const bool compressed = IsCompressed( descriptor.baseFormat() );
            const bool isDepthTexture = IsDepthTexture( descriptor.baseFormat() );
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
                            _subRange._layerRange.offset,
                            _subRange._layerRange.count,
                            _subRange._mipLevels.offset,
                            _subRange._mipLevels.count,
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
                          const Str256& name,
                          const ResourcePath& assetNames,
                          const ResourcePath& assetLocations,
                          const TextureDescriptor& texDescriptor,
                          ResourceCache& parentCache )
        : Texture( context, descriptorHash, name, assetNames, assetLocations, texDescriptor, parentCache )
    {
        static std::atomic_uint s_textureHandle = 1u;
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

    void vkTexture::generateMipmaps( VkCommandBuffer cmdBuffer, const U16 baseLevel, const U16 baseLayer, const U16 layerCount, ImageUsage crtUsage )
    {
        VK_API::PushDebugMessage(cmdBuffer, "vkTexture::generateMipmaps");

        VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
        memBarrier.image = image()->_image;
        memBarrier.subresourceRange.aspectMask = GetAspectFlags( descriptor() );

        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.imageMemoryBarrierCount = 1u;
        dependencyInfo.pImageMemoryBarriers = &memBarrier;

        {
            ImageSubRange baseSubRange = {};
            baseSubRange._mipLevels = { baseLevel, 1u };
            baseSubRange._layerRange = { baseLayer, layerCount };
            if ( IsCubeTexture( descriptor().texType() ) && _vkType == VK_IMAGE_TYPE_2D )
            {
                baseSubRange._layerRange.offset *= 6u;
                baseSubRange._layerRange.count *= 6u;
            }

            memBarrier.subresourceRange.baseMipLevel = baseSubRange._mipLevels.offset;
            memBarrier.subresourceRange.levelCount = baseSubRange._mipLevels.count;;
            memBarrier.subresourceRange.baseArrayLayer = baseSubRange._layerRange.offset;
            memBarrier.subresourceRange.layerCount = baseSubRange._layerRange.count;
        }

        bool barrier = false;
        memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        switch ( crtUsage )
        {
            case ImageUsage::UNDEFINED:
            {
            } break;
            case ImageUsage::SHADER_READ:
            {
                barrier = true;
                const VkImageLayout targetLayout = IsDepthTexture( _descriptor.baseFormat() ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask = VK_API::ALL_SHADER_STAGES;
                memBarrier.oldLayout = targetLayout;
            } break;
            case ImageUsage::SHADER_WRITE:
            {
                barrier = true;
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case ImageUsage::SHADER_READ_WRITE:
            {
                barrier = true;
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask = VK_API::ALL_SHADER_STAGES;
                memBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            default: DIVIDE_UNEXPECTED_CALL_MSG("To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!"); break;
        }

        if ( barrier )
        {
            vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
        }

        memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        memBarrier.subresourceRange.levelCount = 1u;

        VkImageBlit image_blit{};
        image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_blit.srcSubresource.baseArrayLayer = memBarrier.subresourceRange.baseArrayLayer;
        image_blit.srcSubresource.layerCount = memBarrier.subresourceRange.layerCount;
        image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_blit.dstSubresource.baseArrayLayer = image_blit.srcSubresource.baseArrayLayer;
        image_blit.dstSubresource.layerCount = image_blit.srcSubresource.layerCount;
        image_blit.srcOffsets[1].z = 1;
        image_blit.dstOffsets[1].z = 1;

        for ( U16 m = baseLevel + 1u; m < mipCount(); ++m )
        {
            // Source
            image_blit.srcSubresource.mipLevel = m - 1u;
            image_blit.srcOffsets[1].x = int32_t( _width >> (m - 1) );
            image_blit.srcOffsets[1].y = int32_t( _height >> (m - 1) );

            // Destination
            const int32_t mipWidth = int32_t( _width >> m );
            const int32_t mipHeight = int32_t( _height >> m );

            image_blit.dstSubresource.mipLevel = m;
            image_blit.dstOffsets[1].x = mipWidth > 1 ? mipWidth : 1;
            image_blit.dstOffsets[1].y = mipHeight > 1 ? mipHeight : 1;

            // Prepare current mip level as image blit destination
            memBarrier.subresourceRange.baseMipLevel = m;

            memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            vkCmdPipelineBarrier2( cmdBuffer, &dependencyInfo );

            // Blit from previous level
            vkCmdBlitImage( cmdBuffer,
                            _image->_image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            _image->_image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &image_blit,
                            VK_FILTER_LINEAR );

            // Prepare current mip level as image blit source for next level
            memBarrier.srcAccessMask = memBarrier.dstAccessMask;
            memBarrier.oldLayout = memBarrier.newLayout;
            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            vkCmdPipelineBarrier2( cmdBuffer, &dependencyInfo );
        }


        // After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
        memBarrier.oldLayout = memBarrier.newLayout;
        memBarrier.srcAccessMask = memBarrier.dstAccessMask;
        memBarrier.subresourceRange.baseMipLevel = baseLevel;
        memBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;


        switch ( crtUsage )
        {
            case ImageUsage::UNDEFINED:
            {
            } break;
            case ImageUsage::SHADER_READ:
            {
                const VkImageLayout targetLayout = IsDepthTexture( _descriptor.baseFormat() ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask = VK_API::ALL_SHADER_STAGES;
                memBarrier.newLayout = targetLayout;
            } break;
            case ImageUsage::SHADER_WRITE:
            {
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case ImageUsage::SHADER_READ_WRITE:
            {
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask = VK_API::ALL_SHADER_STAGES;
                memBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            default: DIVIDE_UNEXPECTED_CALL_MSG( "To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!" ); break;
        }

        vkCmdPipelineBarrier2( cmdBuffer, &dependencyInfo );

        VK_API::PopDebugMessage(cmdBuffer);
    }

    void vkTexture::submitTextureData()
    {
        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, const QueueType queue, const bool isDedicatedQueue )
        {
            const VkImageLayout targetLayout = IsDepthTexture( _descriptor.baseFormat() ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags( _descriptor );
            range.baseArrayLayer = 0u;
            range.layerCount = VK_REMAINING_ARRAY_LAYERS;
            range.baseMipLevel = 0u;
            range.levelCount = VK_REMAINING_MIP_LEVELS;

            VkImageMemoryBarrier2 imageBarriers[2] = {};

            imageBarriers[0] = vk::imageMemoryBarrier2();
            imageBarriers[0].image = _image->_image;
            imageBarriers[0].subresourceRange = range;

            imageBarriers[0].srcAccessMask = VK_ACCESS_2_NONE;
            imageBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            imageBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            imageBarriers[0].dstStageMask = VK_API::ALL_SHADER_STAGES;
            imageBarriers[0].newLayout = targetLayout;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;
            if ( _resolvedImage != nullptr )
            {
                imageBarriers[1] = imageBarriers[0];
                imageBarriers[1].image = _resolvedImage->_image;
                dependencyInfo.imageMemoryBarrierCount = 2;
            }

            dependencyInfo.pImageMemoryBarriers = imageBarriers;
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);
        }, "vkTexture::submitTextureData()");
    }

    void vkTexture::prepareTextureData( const U16 width, const U16 height, const U16 depth, const bool emptyAllocation )
    {
        Texture::prepareTextureData( width, height, depth, emptyAllocation );

        vkFormat( VKUtil::internalFormat( _descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized() ) );
        
        if ( _image != nullptr )
        {
            ++_testRefreshCounter;
        }
        clearImageViewCache();

        _image = eastl::make_unique<AllocatedImage>();
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
        imageInfo.arrayLayers = _numLayers;
        imageInfo.extent.width = to_U32( _width );
        imageInfo.extent.height = to_U32( _height );
        imageInfo.extent.depth = to_U32( _depth );

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
            if ( _vkType == VK_IMAGE_TYPE_2D )
            {
                imageInfo.arrayLayers *= 6;
            }
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

        LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
        VK_CHECK( vmaCreateImage( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                  &imageInfo,
                                  &vmaallocinfo,
                                  &_image->_image,
                                  &_image->_allocation,
                                  &_image->_allocInfo ) );

        auto imageName = Util::StringFormat( "%s_%d", resourceName().c_str(), _testRefreshCounter );
        vmaSetAllocationName( *VK_API::GetStateTracker()._allocatorInstance._allocator, _image->_allocation, imageName.c_str() );
        Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_image->_image, VK_OBJECT_TYPE_IMAGE, imageName.c_str() );


        if ( sampleFlagBits() != VK_SAMPLE_COUNT_1_BIT )
        {
            _resolvedImage = eastl::make_unique<AllocatedImage>();

            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;


            VK_CHECK( vmaCreateImage( *VK_API::GetStateTracker()._allocatorInstance._allocator,
                                      &imageInfo,
                                      &vmaallocinfo,
                                      &_resolvedImage->_image,
                                      &_resolvedImage->_allocation,
                                      &_resolvedImage->_allocInfo ) );

            imageName = Util::StringFormat( "%s_%d_resolved", resourceName().c_str(), _testRefreshCounter );
            vmaSetAllocationName( *VK_API::GetStateTracker()._allocatorInstance._allocator, _resolvedImage->_allocation, imageName.c_str() );
            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_resolvedImage->_image, VK_OBJECT_TYPE_IMAGE, imageName.c_str() );
        }
    }

    void vkTexture::loadDataInternal( const Byte* data, const size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        DIVIDE_ASSERT(_descriptor.allowRegionUpdates());

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

        const U8 bpp_dest = GetBytesPerPixel( _descriptor.dataType(), _descriptor.baseFormat() );
        const size_t rowOffset_dest = (bpp_dest * pixelUnpackAlignment._alignment) * _width;
        const U16 subHeight = bottomRightY - topLeftY;
        const U16 subWidth = bottomRightX - topLeftX;
        const size_t subRowSize = subWidth * (pixelUnpackAlignment._alignment * bpp_dest);
        const size_t pitch = pixelUnpackAlignment._rowLength == 0u ? rowOffset_dest : pixelUnpackAlignment._rowLength;

        if ( _stagingBuffer == nullptr )
        {
            size_t totalSize = 0u;
            U16 mipWidth = _width;
            U16 mipHeight = _height;

            _mipData.resize( mipCount() );

           
            for ( U32 l = 0u; l < _numLayers; ++l )
            {
                for ( U8 m = 0u; m < mipCount(); ++m )
                {
                    mipWidth = _width >> m;
                    mipHeight = _height >> m;

                    _mipData[m]._dimensions = {mipWidth, mipHeight, _depth};
                    _mipData[m]._size = mipWidth * mipHeight * _depth * bpp_dest;
                    totalSize += _mipData[m]._size;

                }
            };

            _stagingBuffer = VKUtil::createStagingBuffer( totalSize, resourceName() );
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

        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, const QueueType queue, const bool isDedicatedQueue )
        {
            const VkImageLayout targetLayout = IsDepthTexture( _descriptor.baseFormat() ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags( _descriptor );
            range.levelCount = 1;
            range.layerCount = 1;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            memBarrier.subresourceRange.aspectMask = vkTexture::GetAspectFlags( descriptor() );
            memBarrier.image = _image->_image;

            dependencyInfo.pImageMemoryBarriers = &memBarrier;


            size_t dataOffset = 0u;
            for ( U32 l = 0u; l < _numLayers; ++l )
            {
                for ( U8 m = 0u; m < mipCount(); ++m )
                {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = l;

                    memBarrier.subresourceRange = range;
                    memBarrier.oldLayout = targetLayout;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    memBarrier.srcStageMask = VK_API::ALL_SHADER_STAGES;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    vkCmdPipelineBarrier2( cmd, &dependencyInfo );
                    
                    VkBufferImageCopy copyRegion = {};
                    copyRegion.bufferOffset = dataOffset;
                    copyRegion.bufferRowLength = 0u;
                    copyRegion.bufferImageHeight = 0u;
                    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.imageSubresource.mipLevel = m;
                    copyRegion.imageSubresource.baseArrayLayer = l;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset.x = 0u;
                    copyRegion.imageOffset.y = 0u;
                    copyRegion.imageOffset.z = 0u;
                    copyRegion.imageExtent.width = _mipData[m]._dimensions.width;
                    copyRegion.imageExtent.height = _mipData[m]._dimensions.height;
                    copyRegion.imageExtent.depth = _mipData[m]._dimensions.depth;

                    //copy the buffer into the image
                    vkCmdCopyBufferToImage( cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                    memBarrier.oldLayout = memBarrier.newLayout;
                    memBarrier.srcAccessMask = memBarrier.dstAccessMask;
                    memBarrier.srcStageMask = memBarrier.dstStageMask;

                    memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.dstStageMask = VK_API::ALL_SHADER_STAGES;
                    memBarrier.newLayout = targetLayout;

                    //barrier the image into the shader readable layout
                    vkCmdPipelineBarrier2( cmd, &dependencyInfo );

                    dataOffset += _mipData[m]._size;
                }
            }
        }, "vkTexture::loadDataInternal" );
    }

    void vkTexture::loadDataInternal( const ImageTools::ImageData& imageData, const vec3<U16>& offset, const PixelAlignment& pixelUnpackAlignment )
    {
        const U16 numLayers = imageData.layerCount();
        const U8 numMips = imageData.mipCount();
        _mipData.resize(numMips);

        DIVIDE_ASSERT( _numLayers * (vkType() == VK_IMAGE_TYPE_3D ? 1u : 6u) >= numLayers );

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
            _stagingBuffer = VKUtil::createStagingBuffer( totalSize, resourceName() );
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

        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, const QueueType queue, const bool isDedicatedQueue )
        {
            const VkImageLayout targetLayout = IsDepthTexture( _descriptor.baseFormat() ) ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags( _descriptor );
            range.levelCount = 1;
            range.layerCount = 1;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            memBarrier.subresourceRange.aspectMask = vkTexture::GetAspectFlags( descriptor() );
            memBarrier.image = _image->_image;

            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            size_t dataOffset = 0u;
            for ( U32 l = 0u; l < numLayers; ++l )
            {
                const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

                for ( U8 m = 0u; m < numMips; ++m )
                {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = l;

                    memBarrier.subresourceRange = range;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                    memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    vkCmdPipelineBarrier2( cmd, &dependencyInfo );
                    
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
                    vkCmdCopyBufferToImage( cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                    memBarrier.oldLayout = memBarrier.newLayout;
                    memBarrier.srcAccessMask = memBarrier.dstAccessMask;
                    memBarrier.srcStageMask = memBarrier.dstStageMask;

                    if ( needsMipMaps && m + 1u == numMips )
                    {
                        memBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                        memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    }
                    else
                    {
                        memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                        memBarrier.dstStageMask = VK_API::ALL_SHADER_STAGES;
                        memBarrier.newLayout = targetLayout;
                    }

                    //barrier the image into the shader readable layout
                    vkCmdPipelineBarrier2( cmd, &dependencyInfo );

                    dataOffset += mip->_size;
                }
            }

            if ( needsMipMaps )
            {
                generateMipmaps( cmd, 0u, 0u, to_U16(_numLayers), ImageUsage::UNDEFINED );
            }

            if ( !_descriptor.allowRegionUpdates() )
            {
                _stagingBuffer.reset();
            }

        }, "vkTexture::loadDataInternal" );
    }

    void vkTexture::clearData( [[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level ) const noexcept
    {
    }

    void vkTexture::clearSubData( [[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32> depthRange ) const noexcept
    {
    }

    Texture::TextureReadbackData vkTexture::readData( [[maybe_unused]] U16 mipLevel, [[maybe_unused]] const PixelAlignment& pixelPackAlignment, [[maybe_unused]] GFXDataFormat desiredFormat ) const noexcept
    {
        TextureReadbackData data{};
        return MOV( data );
    }

    bool operator==( const vkTexture::CachedImageView::Descriptor& lhs, const vkTexture::CachedImageView::Descriptor& rhs ) noexcept
    {
        return lhs._usage == rhs._usage &&
               lhs._type == rhs._type &&
               lhs._format == rhs._format &&
               lhs._subRange == rhs._subRange;
    }

    VkImageView vkTexture::getImageView( const CachedImageView::Descriptor& viewDescriptor ) const
    {
        const size_t viewHash = viewDescriptor.getHash();

        const auto it = _imageViewCache.find( viewHash );
        if ( it != end( _imageViewCache ) )
        {
            return it->second._view;
        }

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

        range.baseMipLevel = newView._descriptor._subRange._mipLevels.offset;
        range.levelCount = newView._descriptor._subRange._mipLevels.count == U16_MAX ? VK_REMAINING_MIP_LEVELS : newView._descriptor._subRange._mipLevels.count;
        range.baseArrayLayer = newView._descriptor._subRange._layerRange.offset;
        range.layerCount = newView._descriptor._subRange._layerRange.count == U16_MAX ? VK_REMAINING_ARRAY_LAYERS : newView._descriptor._subRange._layerRange.count;
        if ( IsCubeTexture(newView._descriptor._type) && _vkType == VK_IMAGE_TYPE_2D && range.layerCount != VK_REMAINING_ARRAY_LAYERS )
        {
            range.layerCount *= 6;
        }

        VkImageViewCreateInfo imageInfo = vk::imageViewCreateInfo();
        imageInfo.image = viewDescriptor._resolveTarget ? _resolvedImage->_image : _image->_image;
        imageInfo.format = newView._descriptor._format;
        imageInfo.viewType = vkTextureViewTypeTable[to_base( newView._descriptor._type )];
        imageInfo.subresourceRange = range;

        VkImageViewUsageCreateInfo viewCreateInfo = vk::imageViewUsageCreateInfo();
        if ( !viewDescriptor._resolveTarget )
        {
            viewCreateInfo.usage = GetFlagForUsage( newView._descriptor._usage, _descriptor);
            viewCreateInfo.pNext = nullptr;
            imageInfo.pNext = &viewCreateInfo;
        }

        VK_CHECK( vkCreateImageView( VK_API::GetStateTracker()._device->getVKDevice(), &imageInfo, nullptr, &newView._view ) );

        Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)newView._view, VK_OBJECT_TYPE_IMAGE_VIEW, Util::StringFormat("%s_view_%zu", resourceName().c_str(), _imageViewCache.size()).c_str() );
        hashAlg::emplace(_imageViewCache, viewHash, newView);
        return newView._view;
    }

}; //namespace Divide
