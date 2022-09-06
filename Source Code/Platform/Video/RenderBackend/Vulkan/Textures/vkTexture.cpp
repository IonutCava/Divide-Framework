#include "stdafx.h"

#include "Headers/vkTexture.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h"

namespace Divide {
    namespace {
        inline VkImageAspectFlags GetAspectFlags(const TextureDescriptor& descriptor) noexcept {
            VkImageAspectFlags ret = VK_IMAGE_ASPECT_COLOR_BIT;

            if (descriptor.depthAttachmentCompatible()) {
                ret = VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (descriptor.stencilAttachmentCompatible()) {
                ret |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            return ret;
        }

        FORCE_INLINE [[nodiscard]] U8 GetBytesPerPixel(const GFXDataFormat format, const GFXImageFormat baseFormat) noexcept {
            return Texture::GetSizeFactor(format) * NumChannels(baseFormat);
        }
    };

    AllocatedImage::~AllocatedImage()
    {
        if (_image != VK_NULL_HANDLE) {
            VK_API::RegisterCustomAPIDelete([img = _image, alloc = _allocation]([[maybe_unused]] VkDevice device) {
                UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
                vmaDestroyImage(*VK_API::GetStateTracker()->_allocatorInstance._allocator, img, alloc);
            }, true);
        }
    }

    vkTexture::vkTexture(GFXDevice& context,
                         const size_t descriptorHash,
                         const Str256& name,
                         const ResourcePath& assetNames,
                         const ResourcePath& assetLocations,
                         const TextureDescriptor& texDescriptor,
                         ResourceCache& parentCache)
        : Texture(context, descriptorHash, name, assetNames, assetLocations, texDescriptor, parentCache)
    {
        static std::atomic_uint s_textureHandle = 1u;
    }

    vkTexture::~vkTexture()
    {
        unload();
    }

    bool vkTexture::unload() {
        VK_API::RegisterCustomAPIDelete([views = _imageViewCache](VkDevice device) {
            for (auto& it : views) {
                vkDestroyImageView(device, it._view, nullptr);
            }
        }, true);
        
        _vkView = VK_NULL_HANDLE;
        _imageViewCache.clear();

        return Texture::unload();
    }

    void vkTexture::generateTextureMipmap(VkCommandBuffer cmd, const U8 baseLevel) {

        VkImageSubresourceRange range;
        range.aspectMask = GetAspectFlags(_descriptor);
        range.baseArrayLayer = 0u;
        range.layerCount = _numLayers;

        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.image = _image->_image;
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

        for (U8 m = baseLevel; m < mipCount(); ++m) {
            VkImageBlit image_blit{};

            // Source
            image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_blit.srcSubresource.layerCount = _numLayers;
            image_blit.srcSubresource.mipLevel = m - 1;
            image_blit.srcSubresource.baseArrayLayer = 0;
            image_blit.srcOffsets[1].x = int32_t(_width >> (m - 1));
            image_blit.srcOffsets[1].y = int32_t(_height >> (m - 1));
            image_blit.srcOffsets[1].z = 1;

            // Destination
            image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            image_blit.dstSubresource.layerCount = _numLayers;
            image_blit.dstSubresource.mipLevel = m;
            image_blit.dstSubresource.baseArrayLayer = 0;
            image_blit.dstOffsets[1].x = int32_t(_width >> m);
            image_blit.dstOffsets[1].y = int32_t(_height >> m);
            image_blit.dstOffsets[1].z = 1;

            range.baseMipLevel = m;
            range.levelCount = 1;

            // Prepare current mip level as image blit destination
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.subresourceRange = range;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

            // Blit from previous level
            vkCmdBlitImage(cmd,
                            _image->_image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            _image->_image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &image_blit,
                            VK_FILTER_LINEAR);

            // Prepare current mip level as image blit source for next level
            imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageBarrier.subresourceRange = range;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
        }

        // After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
        range.baseMipLevel = baseLevel;
        range.levelCount = VK_REMAINING_MIP_LEVELS;
        range.layerCount = VK_REMAINING_ARRAY_LAYERS;
     
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageBarrier.subresourceRange = range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    }

    void vkTexture::submitTextureData() {

        if (_image != nullptr) {
            CachedImageView::Descriptor defaultViewDescriptor{};
            defaultViewDescriptor._mipLevels = { 0u, VK_REMAINING_MIP_LEVELS };
            defaultViewDescriptor._layers = { 0u, VK_REMAINING_ARRAY_LAYERS };
            defaultViewDescriptor._format = vkFormat();
            defaultViewDescriptor._type = _descriptor.texType();
            defaultViewDescriptor._rwFlag = _descriptor.rwFlag();

            _vkView = getImageView(defaultViewDescriptor);
        }
    }

    void vkTexture::prepareTextureData(const U16 width, const U16 height, const U16 depth, const bool emptyAllocation) {
        Texture::prepareTextureData(width, height, depth, emptyAllocation);

        vkFormat(VKUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized()));
        _image = eastl::make_unique<AllocatedImage>();
        _vkType = vkTextureTypeTable[to_base(descriptor().texType())];

        VkImageCreateInfo imageInfo = vk::imageCreateInfo();
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.format = vkFormat();
        imageInfo.imageType = vkType();
        imageInfo.mipLevels = mipCount();
        imageInfo.arrayLayers = _numLayers;
        imageInfo.extent.width = to_U32(_width);
        imageInfo.extent.height = to_U32(_height);
        imageInfo.extent.depth = to_U32(_depth);

        if (!emptyAllocation) {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if (_descriptor.rwFlag() == ImageFlag::READ_WRITE || _descriptor.rwFlag() == ImageFlag::READ) {
            imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if (_descriptor.rwFlag() == ImageFlag::READ_WRITE || _descriptor.rwFlag() == ImageFlag::WRITE) {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        if (!IsCompressed(_descriptor.baseFormat())) {
            if (_descriptor.colorAttachmentCompatible())
            {
                imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (_descriptor.depthAttachmentCompatible() || _descriptor.stencilAttachmentCompatible())
            {
                imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if (_descriptor.msaaSamples() > 0u)
        {
            assert(isPowerOfTwo(_descriptor.msaaSamples()));
            imageInfo.samples = static_cast<VkSampleCountFlagBits>(_descriptor.msaaSamples());
        }
        if (vkType() == VK_IMAGE_TYPE_3D) {
            imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        }
        if (IsCubeTexture(_descriptor.texType())) {
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            imageInfo.arrayLayers *= 6;
        }

        if (emptyAllocation) {
            imageInfo.flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        }
        VmaAllocationCreateInfo vmaallocinfo = {};
        vmaallocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaallocinfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        vmaallocinfo.priority = 1.f;

        UniqueLock<Mutex> w_lock(VK_API::GetStateTracker()->_allocatorInstance._allocatorLock);
        VK_CHECK(vmaCreateImage(*VK_API::GetStateTracker()->_allocatorInstance._allocator,
                                &imageInfo,
                                &vmaallocinfo,
                                &_image->_image,
                                &_image->_allocation,
                                &_image->_allocInfo));
    }

    void vkTexture::loadDataInternal(const ImageTools::ImageData& imageData) {
        const U32 numLayers = imageData.layerCount();
        const U8 numMips = imageData.mipCount();
        if (vkType() == VK_IMAGE_TYPE_3D) {
            DIVIDE_ASSERT(_numLayers >= numLayers);
        } else {
            DIVIDE_ASSERT(_numLayers * 6 >= numLayers);
        }

        U16 maxDepth = 0u;
        size_t totalSize = 0u;
        for (U32 l = 0u; l < numLayers; ++l) {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for (U8 m = 0u; m < numMips; ++m) {
                const ImageTools::LayerData* mip = layer.getMip(m);
                totalSize += mip->_size;
                maxDepth = std::max(maxDepth, mip->_dimensions.depth);
            }
        }
        DIVIDE_ASSERT(_depth >= maxDepth);

        const AllocatedBuffer_uptr stagingBuffer = VKUtil::createStagingBuffer(totalSize);
        Byte* target = (Byte*)stagingBuffer->_allocInfo.pMappedData;

        size_t offset = 0u;
        for (U32 l = 0u; l < numLayers; ++l) {
            const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
            for (U8 m = 0u; m < numMips; ++m) {
                const ImageTools::LayerData* mip = layer.getMip(m);
                memcpy(&target[offset], mip->data(), mip->_size);
                offset += mip->_size;
            }
        }

       
        VK_API::GetStateTracker()->_cmdContext->flushCommandBuffer([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier imageBarrier_toTransfer = {};
            imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

            const bool needsMipmaps = _descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO && numMips < mipCount();

            imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier_toTransfer.srcAccessMask = 0;
            imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier_toTransfer.image = _image->_image;

            VkImageSubresourceRange range;
            range.aspectMask = GetAspectFlags(_descriptor);
            range.levelCount = 1;
            range.layerCount = 1;

            size_t offset = 0u;
            for (U32 l = 0u; l < numLayers; ++l) {
                const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

                for (U8 m = 0u; m < numMips; ++m) {
                    range.baseMipLevel = m;
                    range.baseArrayLayer = l;

                    imageBarrier_toTransfer.subresourceRange = range;
                    //barrier the image into the transfer-receive layout
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

                    const ImageTools::LayerData* mip = layer.getMip(m);

                    VkBufferImageCopy copyRegion = {};
                    copyRegion.bufferOffset = offset;
                    copyRegion.bufferRowLength = 0;
                    copyRegion.bufferImageHeight = 0;
                    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.imageSubresource.mipLevel = m;
                    copyRegion.imageSubresource.baseArrayLayer = l;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageExtent.width = mip->_dimensions.width;
                    copyRegion.imageExtent.height = mip->_dimensions.height;
                    copyRegion.imageExtent.depth = mip->_dimensions.depth;

                    //copy the buffer into the image
                    vkCmdCopyBufferToImage(cmd, stagingBuffer->_buffer, _image->_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                    VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;
                    imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                    if (needsMipmaps && m + 1u == numMips) {
                        imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                        imageBarrier_toReadable.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    } else {
                        imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    }

                    const VkPipelineStageFlagBits targetStage = needsMipmaps ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

                    //barrier the image into the shader readable layout
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, targetStage, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toReadable);

                    offset += mip->_size;
                }
            }

            if (needsMipmaps) {
                generateTextureMipmap(cmd, numMips);
            }

        });
    }

    void vkTexture::clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level) const noexcept {
    }

    void vkTexture::clearSubData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32>& depthRange) const noexcept {
    }

    Texture::TextureReadbackData vkTexture::readData([[maybe_unused]] U16 mipLevel, [[maybe_unused]] GFXDataFormat desiredFormat) const noexcept {
        TextureReadbackData data{};
        return MOV(data);
    }

    bool vkTexture::CachedImageView::Descriptor::operator==(const Descriptor& other) noexcept {
        return _rwFlag == other._rwFlag &&
               _type == other._type &&
               _format == other._format &&
               _layers == other._layers &&
               _mipLevels == other._mipLevels;
    }

    VkImageView vkTexture::getImageView(const CachedImageView::Descriptor& descriptor) {
        for (CachedImageView& view : _imageViewCache) {
            if (view._descriptor == descriptor) {
                return view._view;
            }
        }

        VkImageSubresourceRange range{};
        range.aspectMask = GetAspectFlags(_descriptor);
        range.baseArrayLayer = descriptor._layers.min;
        range.layerCount = descriptor._layers.max;
        range.baseMipLevel = descriptor._mipLevels.min;
        range.levelCount = descriptor._mipLevels.max;

        if (IsCubeTexture(descriptor._type) &&
            range.layerCount != VK_REMAINING_ARRAY_LAYERS) {
            range.layerCount *= 6;
        }

        VkImageViewCreateInfo imageInfo = vk::imageViewCreateInfo();
        imageInfo.image = _image->_image;
        imageInfo.format = descriptor._format;
        imageInfo.viewType = vkTextureViewTypeTable[to_base(descriptor._type)];
        imageInfo.subresourceRange = range;

        VkImageViewUsageCreateInfo viewCreateInfo = vk::imageViewUsageCreateInfo();
        if (descriptor._rwFlag == ImageFlag::WRITE || descriptor._rwFlag == ImageFlag::READ_WRITE) {
            viewCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        if (descriptor._rwFlag == ImageFlag::READ || descriptor._rwFlag == ImageFlag::READ_WRITE) {
            viewCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        imageInfo.pNext = &viewCreateInfo;
        

        CachedImageView newView{};
        newView._descriptor = descriptor;
        VK_CHECK(vkCreateImageView(VK_API::GetStateTracker()->_device->getVKDevice(), &imageInfo, nullptr, &newView._view));
        

        _imageViewCache.emplace_back(newView);
        return newView._view;
    }
}; //namespace Divide
