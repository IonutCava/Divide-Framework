#include "Headers/vkTexture.h"

#include "Utility/Headers/Localization.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h" //For staging buffers

#include <numeric>
#include <vector>

namespace Divide
{
    namespace
    {
        std::once_flag transition_once_flag;

        VkFlags GetFlagForUsage( const ImageUsage usage , const TextureDescriptor& descriptor) noexcept
        {
            DIVIDE_GPU_ASSERT(usage != ImageUsage::COUNT);

            const bool multisampled = descriptor._msaaSamples > 0u;
            const bool compressed = IsCompressed( descriptor._baseFormat );
            const bool isDepthTexture = IsDepthTexture( descriptor._packing );
            bool supportsStorageBit = !multisampled && !compressed && !isDepthTexture;

            
            VkFlags ret = (usage != ImageUsage::SHADER_WRITE ? VK_IMAGE_USAGE_SAMPLED_BIT : VK_FLAGS_NONE);

            switch ( usage )
            {
                case ImageUsage::SHADER_READ_WRITE:
                case ImageUsage::SHADER_WRITE: DIVIDE_GPU_ASSERT( supportsStorageBit );  break;

                case ImageUsage::RT_COLOUR_ATTACHMENT: 
                case ImageUsage::RT_DEPTH_ATTACHMENT:
                case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                {
                    supportsStorageBit = false;
                    ret |=  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                    ret |= ( usage == ImageUsage::RT_COLOUR_ATTACHMENT ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                    
                } break;

                case ImageUsage::UNDEFINED:
                case ImageUsage::SHADER_READ:
                case ImageUsage::RT_RESOLVE_TARGET:
                case ImageUsage::CPU_READ:  break;

                default:
                case ImageUsage::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
            }

            if ( descriptor._allowRegionUpdates )
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

        void CopyInternal( VkCommandBuffer cmdBuffer, vkTexture::NamedVKImage source, const VkImageAspectFlags sourceAspect, const CopyTextureType sourceType, vkTexture::NamedVKImage destination, const VkImageAspectFlags destinationAspect, const CopyTextureType destinationType, const CopyTexParams& params, const U16 depth )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 2u;

            std::array<VkImageMemoryBarrier2, 2> imageBarriers{};

            const VkImageSubresourceRange subResourceRangeSrc = {
                .aspectMask = sourceAspect,
                .baseMipLevel = params._sourceMipLevel,
                .levelCount = 1u,
                .baseArrayLayer = params._layerRange.offset,
                .layerCount = params._layerRange.count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : params._layerRange.count
            };

            const VkImageSubresourceRange subResourceRangeDst = {
                .aspectMask = destinationAspect,
                .baseMipLevel = params._targetMipLevel,
                .levelCount = 1u,
                .baseArrayLayer = params._layerRange.offset,
                .layerCount = params._layerRange.count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : params._layerRange.count
            };

            {
                const vkTexture::TransitionType sourceTransition  = sourceType == CopyTextureType::COLOUR ? vkTexture::TransitionType::SHADER_READ_TO_COPY_READ_COLOUR : sourceType == CopyTextureType::DEPTH ? vkTexture::TransitionType::SHADER_READ_TO_COPY_READ_DEPTH : vkTexture::TransitionType::GENERAL_TO_COPY_READ;
                const vkTexture::TransitionType destinationTransition = destinationType == CopyTextureType::DEPTH ? vkTexture::TransitionType::SHADER_READ_TO_COPY_WRITE_DEPTH : vkTexture::TransitionType::SHADER_READ_TO_COPY_WRITE_COLOUR;

                vkTexture::TransitionTexture( sourceTransition, subResourceRangeSrc, source, imageBarriers[0] );
                vkTexture::TransitionTexture( destinationTransition, subResourceRangeDst, destination, imageBarriers[1] );
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
                                        source._image,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        destination._image,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        1,
                                        &region );

            {
                const vkTexture::TransitionType sourceTransition = sourceType == CopyTextureType::COLOUR ? vkTexture::TransitionType::COPY_READ_TO_SHADER_READ_COLOUR : sourceType == CopyTextureType::DEPTH ? vkTexture::TransitionType::COPY_READ_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::COPY_READ_TO_GENERAL;
                const vkTexture::TransitionType destinationTransition = destinationType == CopyTextureType::COLOUR ? vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR : destinationType == CopyTextureType::DEPTH ? vkTexture::TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : vkTexture::TransitionType::COPY_WRITE_TO_GENERAL;

                vkTexture::TransitionTexture( sourceTransition, subResourceRangeSrc, source, imageBarriers[0] );
                vkTexture::TransitionTexture( destinationTransition, subResourceRangeDst, destination, imageBarriers[1] );
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
        const bool hasDepthStencil = HasUsageFlagSet( descriptor, ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT );
        const bool hasDepth = HasUsageFlagSet( descriptor, ImageUsage::RT_DEPTH_ATTACHMENT ) || hasDepthStencil;

        return hasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (hasDepthStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u)
                        : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    vkTexture::vkTexture( PlatformContext& context, const ResourceDescriptor<Texture>& descriptor )
        : Texture( context, descriptor )
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
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        const Configuration& config = _context.context().config();

        VK_API::PushDebugMessage( config, cmdBuffer, "vkTexture::generateMipmaps");

        VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.imageMemoryBarrierCount = 1u;
        dependencyInfo.pImageMemoryBarriers = &memBarrier;

        if ( IsCubeTexture( _descriptor._texType ) )
        {
            baseLayer *= 6u;
            if ( layerCount != ALL_LAYERS )
            {
                layerCount *= 6u;
            }
        }

        VkImageSubresourceRange subResourceRange = {
            .aspectMask = GetAspectFlags( _descriptor ),
            .baseMipLevel = baseLevel,
            .levelCount = 1u,
            .baseArrayLayer = baseLayer,
            .layerCount = layerCount == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : layerCount
        };

        const NamedVKImage namedImage{ image()->_image, resourceName().c_str(), HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET) };

        {
            switch ( crtUsage )
            {
                case ImageUsage::UNDEFINED:
                {
                    NOP();
                } break;
                case ImageUsage::SHADER_READ:
                {
                    TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::SHADER_READ_TO_COPY_READ_DEPTH : TransitionType::SHADER_READ_TO_COPY_READ_COLOUR, subResourceRange, namedImage, memBarrier);
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_WRITE:
                {
                    TransitionTexture( TransitionType::GENERAL_TO_COPY_READ, subResourceRange, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_READ_WRITE:
                {
                    TransitionTexture( TransitionType::SHADER_READ_WRITE_TO_COPY_READ, subResourceRange, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                default: DIVIDE_UNEXPECTED_CALL_MSG("To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!"); break;
            }
        }
        subResourceRange.levelCount = 1u;

        VkImageBlit image_blit{};
        image_blit.srcSubresource.aspectMask = GetAspectFlags( _descriptor );
        image_blit.srcSubresource.baseArrayLayer = baseLayer;
        image_blit.srcSubresource.layerCount = layerCount == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : layerCount;
        image_blit.dstSubresource.aspectMask = image_blit.srcSubresource.aspectMask;
        image_blit.dstSubresource.baseArrayLayer = baseLayer;
        image_blit.dstSubresource.layerCount = layerCount == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : layerCount;
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
                TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, subResourceRange, namedImage, memBarrier );
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
                TransitionTexture( TransitionType::COPY_WRITE_TO_COPY_READ, subResourceRange, namedImage, memBarrier );
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
                    TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::COPY_READ_TO_SHADER_READ_DEPTH : TransitionType::COPY_READ_TO_SHADER_READ_COLOUR, subResourceRange, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                case ImageUsage::SHADER_WRITE:
                case ImageUsage::SHADER_READ_WRITE:
                {
                    TransitionTexture( TransitionType::COPY_READ_TO_GENERAL, subResourceRange, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
                } break;
                default: DIVIDE_UNEXPECTED_CALL_MSG( "To compute mipmaps image must be in either LAYOUT_GENERAL or LAYOUT_READ_ONLY_OPTIMAL!" ); break;
            }
        }
        VK_API::PopDebugMessage( config, cmdBuffer );
    }

    void vkTexture::prepareTextureData( const U16 width, const U16 height, const U16 depth, const bool emptyAllocation )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        Texture::prepareTextureData( width, height, depth, emptyAllocation );

        vkFormat( VKUtil::InternalFormat( _descriptor._baseFormat, _descriptor._dataType, _descriptor._packing ) );
        
        if ( _image != nullptr )
        {
            ++_testRefreshCounter;
        }
        clearImageViewCache();

        _image = std::make_unique<AllocatedImage>();
        _vkType = vkTextureTypeTable[to_base( _descriptor._texType )];
        const bool isCubeMap = IsCubeTexture(_descriptor._texType);
        DIVIDE_ASSERT(!(isCubeMap && _width != _height) && "vkTexture::prepareTextureData error: width and height for cube map texture do not match!");

        sampleFlagBits( VK_SAMPLE_COUNT_1_BIT );
        if ( _descriptor._msaaSamples > 0u )
        {
            assert( isPowerOfTwo( _descriptor._msaaSamples ) );
            sampleFlagBits( static_cast<VkSampleCountFlagBits>(_descriptor._msaaSamples) );
        }

        VkImageCreateInfo imageInfo = vk::imageCreateInfo();
        imageInfo.tiling = _descriptor._allowRegionUpdates ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.samples = sampleFlagBits();
        imageInfo.format = vkFormat();
        imageInfo.imageType = vkType();
        imageInfo.mipLevels = mipCount();
        imageInfo.extent.width = to_U32( _width );
        imageInfo.extent.height = to_U32( _height );

        if ( _descriptor._texType == TextureType::TEXTURE_3D )
        {
            imageInfo.extent.depth = to_U32( _depth );
            imageInfo.arrayLayers = 1;
        }
        else
        {
            imageInfo.extent.depth = 1;
            imageInfo.arrayLayers = isCubeMap ? to_U32(_depth) * 6u : to_U32(_depth) * 1u;
        }

        if ( !emptyAllocation || imageInfo.mipLevels > 1u)
        {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        for ( U8 i = 0u; i < to_base( ImageUsage::COUNT ); ++i )
        {
            const ImageUsage testUsage = static_cast<ImageUsage>(i);
            if ( !HasUsageFlagSet( _descriptor, testUsage ) )
            {
                continue;
            }
            imageInfo.usage |= GetFlagForUsage( testUsage, _descriptor);
        }

        if ( !IsCompressed( _descriptor._baseFormat ) )
        {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        if ( vkType() == VK_IMAGE_TYPE_3D )
        {
            imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        }
        if ( isCubeMap )
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
        DIVIDE_GPU_ASSERT( _descriptor._allowRegionUpdates);
        loadDataInternal(data, size, targetMip, offset, dimensions, pixelUnpackAlignment, false);
    }

    void vkTexture::loadDataInternal( const Byte* data, const size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment, [[maybe_unused]] const bool emptyAllocation )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( size == 0u )
        {
            return;
        }

        const U16 topLeftX = offset.x;
        const U16 topLeftY = offset.y;
        const U16 bottomRightX = dimensions.x + offset.x;
        const U16 bottomRightY = dimensions.y + offset.y;

        DIVIDE_GPU_ASSERT( offset.z == 0u && dimensions.z == 1u, "vkTexture::loadDataInternal: 3D textures not supported for sub-image updates!");

        const U8 bpp_dest = GetBytesPerPixel( _descriptor._dataType, _descriptor._baseFormat, _descriptor._packing );
        const U16 subHeightBase = bottomRightY - topLeftY;
        const U16 subWidthBase = bottomRightX - topLeftX;

        const bool isCubeMap = IsCubeTexture(_descriptor._texType);
        const bool is3D = (_descriptor._texType == TextureType::TEXTURE_3D);

        _mipData.resize( mipCount() );

        // Compute per-mip sizes including alignment
        size_t totalSize = 0u;
        size_t maxMipLayerSize = 0u;
        std::vector<size_t> perMipLayerSize(mipCount());
        for ( U8 m = 0u; m < mipCount(); ++m )
        {
            const U16 mipW = std::max<U16>(1u, _width >> m);
            const U16 mipH = std::max<U16>(1u, _height >> m);
            const size_t mipLayerSize = to_size(mipW) * to_size(mipH) * to_size(bpp_dest) * pixelUnpackAlignment._alignment;

            U16 layersForMip = 1;
            if ( is3D )
            {
                layersForMip = std::max<U16>(1u, _depth >> m);
            }
            else
            {
                layersForMip = _depth * (isCubeMap ? 6u : 1u);
            }

            _mipData[m]._dimensions = { mipW, mipH, layersForMip };
            _mipData[m]._size = mipLayerSize * layersForMip;

            perMipLayerSize[m] = mipLayerSize;
            totalSize += _mipData[m]._size;
            maxMipLayerSize = std::max(maxMipLayerSize, mipLayerSize);
        }

        const size_t deviceMaxBufferSize = GFXDevice::GetDeviceInformation()._maxBufferSizeBytes;
        const bool haveFullMipData = (data != nullptr && size >= totalSize);

        // If we have full data and it fits the device, use contiguous path
        if ( haveFullMipData && totalSize <= deviceMaxBufferSize )
        {
            if ( _stagingBuffer == nullptr || _stagingBuffer->_allocInfo.size < totalSize )
            {
                _stagingBuffer = VKUtil::createStagingBuffer( totalSize, resourceName().c_str(), false );
            }

            // Copy full source into staging buffer (assume layer-major ordering)
            Byte* mappedData = reinterpret_cast<Byte*>( _stagingBuffer->_allocInfo.pMappedData );
            memcpy( mappedData, data, totalSize );

            // Flush mapped memory
            {
                LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
                if ( VK_API::GetStateTracker()._allocatorInstance._allocator != nullptr )
                {
                    vmaFlushAllocation( *VK_API::GetStateTracker()._allocatorInstance._allocator, _stagingBuffer->_allocation, 0u, totalSize );
                }
            }

            // Upload all mips/layers from staging buffer
            VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
            {
                PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmd );

                const NamedVKImage namedImage{ image()->_image, resourceName().c_str(), HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET) };

                VkImageSubresourceRange range{};
                range.aspectMask = GetAspectFlags( _descriptor );
                range.levelCount = 1;
                range.layerCount = 1;

                VkDependencyInfo dependencyInfo = vk::dependencyInfo();
                dependencyInfo.imageMemoryBarrierCount = 1;

                VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
                dependencyInfo.pImageMemoryBarriers = &memBarrier;

                size_t dataOffset = 0u;
                for ( U32 layer = 0u; layer < _depth; ++layer )
                {
                    const U32 layerIndexForCopies = isCubeMap ? layer * 6u : layer;
                    for ( U8 m = 0u; m < mipCount(); ++m )
                    {
                        range.baseMipLevel = m;
                        range.baseArrayLayer = layerIndexForCopies;

                        TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, range, namedImage, memBarrier );
                        VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );

                        VkBufferImageCopy copyRegion = {};
                        copyRegion.bufferOffset = dataOffset;
                        copyRegion.bufferRowLength = 0u;
                        copyRegion.bufferImageHeight = 0u;
                        copyRegion.imageSubresource.aspectMask = range.aspectMask;
                        copyRegion.imageSubresource.mipLevel = m;
                        copyRegion.imageSubresource.baseArrayLayer = range.baseArrayLayer;
                        copyRegion.imageSubresource.layerCount = 1;
                        copyRegion.imageOffset = { 0, 0, 0 };
                        copyRegion.imageExtent.width = _mipData[m]._dimensions.width;
                        copyRegion.imageExtent.height = _mipData[m]._dimensions.height;
                        copyRegion.imageExtent.depth = 1u;

                        VK_PROFILE( vkCmdCopyBufferToImage, cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                        TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR, range, namedImage, memBarrier );
                        VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );

                        dataOffset += _mipData[m]._size;
                    }
                }
            }, "vkTexture::loadDataInternal");

            return;
        }

        // Otherwise per-mip-per-layer: size staging to max single layer mip size (including alignment)
        const size_t stagingSize = std::min(maxMipLayerSize, deviceMaxBufferSize);
        if ( _stagingBuffer == nullptr || _stagingBuffer->_allocInfo.size < stagingSize )
        {
            _stagingBuffer = VKUtil::createStagingBuffer( stagingSize, resourceName().c_str(), false );
        }
        Byte* stagingMapped = reinterpret_cast<Byte*>( _stagingBuffer->_allocInfo.pMappedData );

        // Precompute per-layer totals for source offsets (layer-major: mips per layer)
        size_t totalBytesPerLayer = std::accumulate(perMipLayerSize.begin(), perMipLayerSize.end(), static_cast<size_t>(0));

        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmd );

            const NamedVKImage namedImage{ image()->_image, resourceName().c_str(), /*HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET)*/false };

            VkImageSubresourceRange range{};
            range.aspectMask = GetAspectFlags( _descriptor );
            range.levelCount = 1;
            range.layerCount = 1;

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            for ( U32 layer = 0u; layer < _depth; ++layer )
            {
                const U32 layerIndexForCopies = isCubeMap ? layer * 6u : layer;
                const size_t layerDataBase = haveFullMipData ? (layer * totalBytesPerLayer) : 0u;

                for ( U8 m = 0u; m < mipCount(); ++m )
                {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = layerIndexForCopies;

                    const size_t mipLayerSize = perMipLayerSize[m];
                    const Byte* srcPtr = nullptr;
                    if ( haveFullMipData )
                    {
                        srcPtr = data + layerDataBase + std::accumulate(perMipLayerSize.begin(), perMipLayerSize.begin() + m, static_cast<size_t>(0));
                    }

                    // Populate staging mapped memory for this mip-layer honoring row stride
                    if ( srcPtr != nullptr )
                    {
                        const U16 mipW = _mipData[m]._dimensions.width;
                        const U16 mipH = _mipData[m]._dimensions.height;
                        const size_t srcRowBytes = to_size(bpp_dest) * to_size(mipW) * pixelUnpackAlignment._alignment;
                        const size_t srcRowStride = pixelUnpackAlignment._rowLength == 0u ? srcRowBytes : pixelUnpackAlignment._rowLength;
                        Byte* dst = stagingMapped;
                        const Byte* s = srcPtr;
                        for ( U16 row = 0; row < mipH; ++row )
                        {
                            memcpy( dst, s, srcRowBytes );
                            dst += srcRowBytes;
                            s += srcRowStride;
                        }
                    }
                    else
                    {
                        // clear only the used region (mipLayerSize)
                        memset( stagingMapped, 0, mipLayerSize );
                    }

                    // Flush the range we just wrote
                    {
                        LockGuard<Mutex> w_lock( VK_API::GetStateTracker()._allocatorInstance._allocatorLock );
                        if ( VK_API::GetStateTracker()._allocatorInstance._allocator != nullptr )
                        {
                            vmaFlushAllocation( *VK_API::GetStateTracker()._allocatorInstance._allocator, _stagingBuffer->_allocation, 0u, mipLayerSize );
                        }
                    }

                    // Transition + copy this single mip-layer
                    TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, range, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );

                    VkBufferImageCopy copyRegion{};
                    copyRegion.bufferOffset = 0u;
                    copyRegion.bufferRowLength = 0u;
                    copyRegion.bufferImageHeight = 0u;
                    copyRegion.imageSubresource.aspectMask = range.aspectMask;
                    copyRegion.imageSubresource.mipLevel = m;
                    copyRegion.imageSubresource.baseArrayLayer = range.baseArrayLayer;
                    copyRegion.imageSubresource.layerCount = 1;

                    const U16 mipW = _mipData[m]._dimensions.width;
                    const U16 mipH = _mipData[m]._dimensions.height;
                    const U32 mipTopLeftX = std::min<U32>( to_U32(topLeftX) >> m, mipW - 1u );
                    const U32 mipTopLeftY = std::min<U32>( to_U32(topLeftY) >> m, mipH - 1u );
                    const U32 mipSubW = std::min<U32>( std::max<U32>(1u, to_U32(subWidthBase) >> m), mipW - mipTopLeftX );
                    const U32 mipSubH = std::min<U32>( std::max<U32>(1u, to_U32(subHeightBase) >> m), mipH - mipTopLeftY );

                    copyRegion.imageOffset.x = static_cast<I32>( mipTopLeftX );
                    copyRegion.imageOffset.y = static_cast<I32>( mipTopLeftY );
                    copyRegion.imageOffset.z = 0u;
                    copyRegion.imageExtent.width = mipSubW;
                    copyRegion.imageExtent.height = mipSubH;
                    copyRegion.imageExtent.depth = 1u;

                    VK_PROFILE( vkCmdCopyBufferToImage, cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

                    TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR, range, namedImage, memBarrier );
                    VK_PROFILE( vkCmdPipelineBarrier2, cmd, &dependencyInfo );
                }
            }

        }, "vkTexture::loadDataInternal" );
    }

    void vkTexture::loadDataInternal(const ImageTools::ImageData& imageData, const vec3<U16>& offset, const PixelAlignment& pixelUnpackAlignment)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        const U16 numLayers = imageData.layerCount();
        const U8 numMips = imageData.mipCount();
        _mipData.resize(numMips);

        U16 maxDepth = 0u;
        size_t totalSize = 0u;
        for (U32 l = 0u; l < numLayers; ++l)
        {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for (U8 m = 0u; m < numMips; ++m)
            {
                const ImageTools::LayerData* mip = layer.getMip(m);
                totalSize += mip->_size;
                _mipData[m]._size = mip->_size;
                _mipData[m]._dimensions = mip->_dimensions;
                maxDepth = std::max(maxDepth, mip->_dimensions.depth);
            }
        }
        DIVIDE_GPU_ASSERT(_depth >= maxDepth);

        if (_stagingBuffer == nullptr)
        {
            _stagingBuffer = VKUtil::createStagingBuffer(totalSize, resourceName().c_str(), false);
        }

        Byte* target = (Byte*)_stagingBuffer->_allocInfo.pMappedData;

        size_t dataOffset = 0u;
        for (U32 l = 0u; l < numLayers; ++l)
        {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for (U8 m = 0u; m < numMips; ++m)
            {
                const ImageTools::LayerData* mip = layer.getMip(m);
                memcpy(&target[dataOffset], mip->data(), mip->_size);
                dataOffset += mip->_size;
            }
        }

        const bool needsMipMaps = _descriptor._mipMappingState == MipMappingState::AUTO && numMips < mipCount();

        VK_API::GetStateTracker().IMCmdContext(QueueType::GRAPHICS)->flushCommandBuffer([&](VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue)
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT(cmd);

            const NamedVKImage namedImage{ image()->_image, resourceName().c_str(), HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET) };

            VkDependencyInfo dependencyInfo = vk::dependencyInfo();
            dependencyInfo.imageMemoryBarrierCount = 1;

            VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
            dependencyInfo.pImageMemoryBarriers = &memBarrier;

            VkImageSubresourceRange fullRange
            {
                .aspectMask = GetAspectFlags(_descriptor),
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            };

            TransitionTexture( TransitionType::GENERAL_TO_COPY_WRITE, fullRange, namedImage, memBarrier);
            VK_PROFILE(vkCmdPipelineBarrier2, cmd, &dependencyInfo);

            size_t dataOffset = 0u;
            for (U32 l = 0u; l < numLayers; ++l)
            {
                const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

                for (U8 m = 0u; m < numMips; ++m)
                {
                    const ImageTools::LayerData* mip = layer.getMip(m);

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
                    VK_PROFILE(vkCmdCopyBufferToImage, cmd, _stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
                    dataOffset += mip->_size;
                }
            }

            if (needsMipMaps)
            {
                TransitionTexture(TransitionType::COPY_WRITE_TO_COPY_READ, fullRange, namedImage, memBarrier);
                VK_PROFILE(vkCmdPipelineBarrier2, cmd, &dependencyInfo);

                generateMipmaps(cmd, 0u, 0u, ALL_LAYERS, ImageUsage::UNDEFINED);
            }
            else
            {
                TransitionTexture(IsDepthTexture(_descriptor._packing) ? TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH : TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR, fullRange, namedImage, memBarrier);
                VK_PROFILE(vkCmdPipelineBarrier2, cmd, &dependencyInfo);
            }

            if (!_descriptor._allowRegionUpdates)
            {
                _stagingBuffer.reset();
            }

        }, "vkTexture::loadDataInternal");
    }

    void vkTexture::clearData( VkCommandBuffer cmdBuffer, const UColour4& clearColour, SubRange layerRange, U8 mipLevel ) const noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        const NamedVKImage namedImage{ image()->_image, resourceName().c_str(), HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET) };

        if ( mipLevel == U8_MAX )
        {
            assert( mipCount() > 0u );
            mipLevel = to_U8( mipCount() - 1u );
        }

        VkImageSubresourceRange range;
        range.aspectMask = GetAspectFlags( _descriptor );
        range.baseArrayLayer = layerRange._offset;
        range.layerCount = layerRange._count == ALL_LAYERS ? VK_REMAINING_ARRAY_LAYERS : layerRange._count;
        range.baseMipLevel = mipLevel;
        range.levelCount = 1u;

        VkImageMemoryBarrier2 memBarrier = vk::imageMemoryBarrier2();
        VkDependencyInfo dependencyInfo = vk::dependencyInfo();
        dependencyInfo.imageMemoryBarrierCount = 1u;
        dependencyInfo.pImageMemoryBarriers = &memBarrier;

        TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::SHADER_READ_TO_BLIT_WRITE_DEPTH : TransitionType::SHADER_READ_TO_BLIT_WRITE_COLOUR, range, namedImage, memBarrier );
        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );

        if ( IsDepthTexture( _descriptor._packing ) )
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

        TransitionTexture( IsDepthTexture( _descriptor._packing ) ? TransitionType::BLIT_WRITE_TO_SHADER_READ_DEPTH : TransitionType::BLIT_WRITE_TO_SHADER_READ_COLOUR, range, namedImage, memBarrier );
        VK_PROFILE( vkCmdPipelineBarrier2, cmdBuffer, &dependencyInfo );
    }

    ImageReadbackData vkTexture::readData( U8 mipLevel, const PixelAlignment& pixelPackAlignment ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        ImageReadbackData ret{};
        VK_API::GetStateTracker().IMCmdContext( QueueType::GRAPHICS )->flushCommandBuffer( [&]( VkCommandBuffer cmd, [[maybe_unused]] const QueueType queue, [[maybe_unused]] const bool isDedicatedQueue )
        {
            PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmd );

            ret = readData(cmd, mipLevel, pixelPackAlignment );
        }, "vkTexture::readData()" );
        return ret;
    }

    ImageReadbackData vkTexture::readData( VkCommandBuffer cmdBuffer, U8 mipLevel, [[maybe_unused]] const PixelAlignment& pixelPackAlignment) const noexcept
    {
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        ImageReadbackData grabData{};
        grabData._numComponents = numChannels();

        const auto desiredDataFormat = _descriptor._dataType;
        const auto desiredImageFormat = _descriptor._baseFormat;
        const auto desiredPacking = _descriptor._packing;

        grabData._sourceIsBGR = IsBGRTexture( desiredImageFormat );
        grabData._bpp = GetBytesPerPixel( desiredDataFormat, desiredImageFormat, desiredPacking );

        DIVIDE_GPU_ASSERT( (grabData._bpp == 3 || grabData._bpp == 4) && _depth == 1u && !IsCubeTexture( _descriptor._texType ), "vkTexture:readData: unsupported image for readback. Support is very limited!" );
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

        VkImageAspectFlags aspectFlags = GetAspectFlags( _descriptor );

        CopyInternal(cmdBuffer,
                     { image()->_image, resourceName().c_str(), HasUsageFlagSet(descriptor(), ImageUsage::RT_RESOLVE_TARGET) },
                     aspectFlags,
                     IsDepthTexture( _descriptor._packing ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                     { dstImage, "STAGING_BUFFER_READ_TEXTURE", false } ,
                     aspectFlags,
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
        image_copy_region.imageSubresource.aspectMask = aspectFlags;
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
            bufferBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            bufferBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT;
            bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
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

            DIVIDE_GPU_ASSERT( viewDescriptor._usage != ImageUsage::COUNT );

            CachedImageView newView{};
            newView._descriptor = viewDescriptor;

            VkImageSubresourceRange range{};
            if ( HasUsageFlagSet( _descriptor, ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT ) )
            {
                range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            else
            {
                range.aspectMask = GetAspectFlags( _descriptor );
            }

            range.baseMipLevel = newView._descriptor._subRange._mipLevels._offset;
            range.levelCount = newView._descriptor._subRange._mipLevels._count == ALL_MIPS ? VK_REMAINING_MIP_LEVELS : newView._descriptor._subRange._mipLevels._count;
            range.baseArrayLayer = newView._descriptor._subRange._layerRange._offset;

            const U16 layerCountIn = newView._descriptor._subRange._layerRange._count;
            if ( layerCountIn == ALL_LAYERS )
            {
                range.layerCount = VK_REMAINING_ARRAY_LAYERS;
            }
            else
            {
                range.layerCount = layerCountIn * (IsCubeTexture(newView._descriptor._type) ? 6u : 1u);
            }

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
        PROFILE_VK_EVENT_AUTO_AND_CONTEXT( cmdBuffer );

        // We could handle this with a custom shader pass and temp render targets, so leaving the option i
        DIVIDE_GPU_ASSERT( sourceSamples == 0u && destinationSamples == 0u, "vkTexture::copy Multisampled textures is not supported yet!" );
        DIVIDE_GPU_ASSERT( source != nullptr && destination != nullptr, "vkTexture::copy Invalid source and/or destination textures specified!" );

        const TextureType srcType = source->_descriptor._texType;
        const TextureType dstType = destination->_descriptor._texType;
        assert( srcType != TextureType::COUNT && dstType != TextureType::COUNT );

        U32 layerOffset = params._layerRange.offset;
        U32 layerCount = params._layerRange.count == ALL_LAYERS ? source->_depth : params._layerRange.count;
        if ( IsCubeTexture( srcType ) )
        {
            layerOffset *= 6;
            layerCount *= 6;
        }
        params._layerRange = { layerOffset, layerCount };

        if ( srcType != TextureType::COUNT && dstType != TextureType::COUNT )
        {

            U16 depth = 1u;
            if ( source->_descriptor._texType == TextureType::TEXTURE_3D )
            {
                depth = source->_depth;
            }

            CopyInternal( cmdBuffer, 
                          { source->_image->_image, source->resourceName().c_str(), HasUsageFlagSet(source->descriptor(), ImageUsage::RT_RESOLVE_TARGET) },
                          GetAspectFlags( source->_descriptor ),
                          IsDepthTexture( source->_descriptor._packing ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                          { destination->_image->_image, destination->resourceName().c_str(), HasUsageFlagSet(destination->descriptor(), ImageUsage::RT_RESOLVE_TARGET) },
                          GetAspectFlags( destination->_descriptor ),
                          IsDepthTexture( destination->_descriptor._packing ) ? CopyTextureType::DEPTH : CopyTextureType::COLOUR,
                          params,
                          depth );
        }
    }

    /*static*/ void vkTexture::TransitionTexture(const TransitionType type, const VkImageSubresourceRange & subresourceRange, const NamedVKImage namedImage, VkImageMemoryBarrier2 & memBarrier)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

#       if 1
            Console::d_errorfn("TransitionTexture [ {} ] to [ {} ]. Layer [ {} - {} ]. Mip [ {} - {} ].", namedImage._name, Names::transitionType[to_base(type)], subresourceRange.baseArrayLayer, subresourceRange.layerCount, subresourceRange.baseMipLevel, subresourceRange.levelCount);
#       endif

        memBarrier = vk::imageMemoryBarrier2();
        memBarrier.image = namedImage._image;
        memBarrier.subresourceRange = subresourceRange;

        constexpr auto SHADER_READ_WRITE_BIT = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

        static auto SHADER_SAMPLE_STAGE_MASK = 0u;

        std::call_once(
            transition_once_flag,
            []()
            {
                SHADER_SAMPLE_STAGE_MASK = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
                                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

                if ( GFXDevice::GetDeviceInformation()._meshShadingSupported )
                {
                    SHADER_SAMPLE_STAGE_MASK |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
                }
            }
        );

        switch ( type )
        {
            case TransitionType::SHADER_READ_TO_COLOUR_ATTACHMENT:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL; // treat prior usage as attachment
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                if (namedImage._isResolveImage)
                {
                    memBarrier.dstStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                }
            } break;
            case TransitionType::SHADER_READ_TO_DEPTH_ATTACHMENT:
            {
                if (namedImage._isResolveImage)
                {
                    // Resolve images may have been written by COLOR_ATTACHMENT_OUTPUT
                    memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK |
                                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT |
                                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                }
                else
                {
                    memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                }
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                         | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                         | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                if (namedImage._isResolveImage)
                {
                    memBarrier.srcStageMask |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                }
            } break;
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ:
            {
                // After inline resolve, depth resolve images have writes in COLOR_ATTACHMENT_OUTPUT domain
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                         | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                         | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT: VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                             | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COLOUR_ATTACHMENT_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::DEPTH_ATTACHMENT_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::GENERAL_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::UNDEFINED_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_COLOUR_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_DEPTH_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_COLOUR_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_DEPTH_TO_SHADER_READ_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                memBarrier.dstAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_READ_COLOUR:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_WRITE_COLOUR:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                if (namedImage._isResolveImage)
                {
                    memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }
            } break;
            case TransitionType::BLIT_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                if ( namedImage._isResolveImage )
                {
                    memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_READ_DEPTH:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_BLIT_WRITE_DEPTH:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::BLIT_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_WRITE_COLOUR:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_WRITE_DEPTH:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_READ_COLOUR:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_TO_COPY_READ_DEPTH:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                }

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::COPY_READ_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COPY_READ_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COPY_WRITE_TO_SHADER_READ_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::COPY_WRITE_TO_SHADER_READ_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                memBarrier.dstStageMask  = SHADER_SAMPLE_STAGE_MASK;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            } break;
            case TransitionType::SHADER_READ_WRITE_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = SHADER_READ_WRITE_BIT;
                memBarrier.srcStageMask  = VK_API::AllShaderStages();
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::GENERAL_TO_COPY_WRITE:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::COPY_READ_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COPY_WRITE_TO_GENERAL:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
            } break;
            case TransitionType::COPY_WRITE_TO_COPY_READ:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::ATTACHMENT_TO_ATTACHMENT_COLOUR:
            {
                // If previous layout tracked as shader read (resolve image not yet transitioned), perform real transition
                if (namedImage._isResolveImage)
                {
                    // Treat prior as SHADER_READ_ONLY_OPTIMAL if not yet attachment
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                    memBarrier.srcStageMask = SHADER_SAMPLE_STAGE_MASK | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

                    memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    // Pure ordering (layout-preserving)
                    memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                    memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                }
            } break;
            case TransitionType::ATTACHMENT_TO_ATTACHMENT_DEPTH:
            {
                if (namedImage._isResolveImage)
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask  = SHADER_SAMPLE_STAGE_MASK
                                             | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                    memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                             | VK_PIPELINE_STAGE_2_RESOLVE_BIT
                                             | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.newLayout     = memBarrier.oldLayout;
                }
                else
                {
                    memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                    memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    memBarrier.newLayout     = memBarrier.oldLayout;
                }
            } break;
            case TransitionType::ATTACHMENT_TO_BLIT_READ_COLOUR:
            {
                // Colour attachment -> read via blit/copy
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            } break;
            case TransitionType::ATTACHMENT_TO_BLIT_WRITE_COLOUR:
            {
                // Rare: colour attachment to be destination of a blit (e.g. fill from another image before resuming attachment writes)
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_ATTACHMENT_COLOUR:
            {
                // After reading as transfer src, go back to attachment for writes
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::BLIT_WRITE_TO_ATTACHMENT_COLOUR:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                         | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::ATTACHMENT_TO_BLIT_READ_DEPTH:
            {
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::ATTACHMENT_TO_BLIT_WRITE_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            } break;
            case TransitionType::BLIT_READ_TO_ATTACHMENT_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                         | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                         | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::BLIT_WRITE_TO_ATTACHMENT_DEPTH:
            {
                memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
                memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                         | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                         | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                         | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
                memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            } break;
            case TransitionType::ATTACHMENT_TO_COPY_READ_COLOUR:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } break;
        case TransitionType::ATTACHMENT_TO_COPY_WRITE_COLOUR:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       (namedImage._isResolveImage ? VK_PIPELINE_STAGE_2_RESOLVE_BIT : 0);
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        } break;
        case TransitionType::COPY_READ_TO_ATTACHMENT_COLOUR:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        } break;
        case TransitionType::COPY_WRITE_TO_ATTACHMENT_COLOUR:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        } break;

        case TransitionType::ATTACHMENT_TO_COPY_READ_DEPTH:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                                       (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                                                                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } break;
        case TransitionType::ATTACHMENT_TO_COPY_WRITE_DEPTH:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                                       (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                                                                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        } break;
        case TransitionType::COPY_READ_TO_ATTACHMENT_DEPTH:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                     | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                     | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                     | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        } break;
        case TransitionType::COPY_WRITE_TO_ATTACHMENT_DEPTH:
        {
            memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            memBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                     | (namedImage._isResolveImage ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0);
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                     | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                     | (namedImage._isResolveImage ? (VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) : 0);
            memBarrier.newLayout     = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        } break;
            default:
            {
                DIVIDE_UNEXPECTED_CALL();
            } break;
        }
    } 

}; //namespace Divide
