/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef VK_TEXTURE_H
#define VK_TEXTURE_H

#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkMemAllocatorInclude.h"

namespace Divide
{
    struct AllocatedImage : private NonCopyable
    {
        ~AllocatedImage();

        VkImage _image{ VK_NULL_HANDLE };
        VmaAllocation _allocation{ VK_NULL_HANDLE };
        VmaAllocationInfo _allocInfo{};
    };

    FWD_DECLARE_MANAGED_STRUCT( AllocatedImage );

    class vkTexture final : public Texture
    {
        public:

        struct CachedImageView
        {
            struct Descriptor final : Hashable
            {
                ImageSubRange _subRange{};
                VkFormat _format{ VK_FORMAT_MAX_ENUM };
                TextureType _type{ TextureType::COUNT };
                ImageUsage _usage{ ImageUsage::COUNT };

                [[nodiscard]] size_t getHash() const noexcept override;

            } _descriptor;
            VkImageView _view{VK_NULL_HANDLE};
        };

        vkTexture( GFXDevice& context,
                   const size_t descriptorHash,
                   const Str256& name,
                   const ResourcePath& assetNames,
                   const ResourcePath& assetLocations,
                   const TextureDescriptor& texDescriptor,
                   ResourceCache& parentCache );

        virtual ~vkTexture();

        bool unload() override;

        void clearData( const UColour4& clearColour, U8 level ) const noexcept override;

        void clearSubData( const UColour4& clearColour, U8 level, const vec4<I32>& rectToClear, vec2<I32> depthRange ) const noexcept override;

        TextureReadbackData readData( U16 mipLevel, GFXDataFormat desiredFormat ) const noexcept override;

        VkImageView getImageView( const CachedImageView::Descriptor& descriptor );
        void generateMipmaps( VkCommandBuffer cmdBuffer, U16 baseLevel, U16 baseLayer, U16 layerCount );
        [[nodiscard]] bool transitionLayout( ImageSubRange subRange, ImageUsage newLayout, VkImageMemoryBarrier2& memBarrierOut );

        PROPERTY_R( AllocatedImage_uptr, image, nullptr );
        PROPERTY_R_IW( VkImageType, vkType, VK_IMAGE_TYPE_MAX_ENUM );
        PROPERTY_R_IW( VkFormat, vkFormat, VK_FORMAT_MAX_ENUM );
        PROPERTY_R_IW( VkSampleCountFlagBits, sampleFlagBits, VK_SAMPLE_COUNT_1_BIT );

        static [[nodiscard]] VkImageAspectFlags GetAspectFlags( const TextureDescriptor& descriptor ) noexcept;
        private:
        void loadDataInternal( const ImageTools::ImageData& imageData ) override;
        void prepareTextureData( U16 width, U16 height, U16 depth, bool emptyAllocation ) override;
        void submitTextureData() override;
        void clearDataInternal( const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, vec2<I32> depthRange ) const;
        void clearImageViewCache();

        private:
        hashMap<size_t, CachedImageView> _imageViewCache;

        VkDeviceSize _stagingBufferSize{ 0u };
    };

    bool operator==( const vkTexture::CachedImageView::Descriptor& lhs, const vkTexture::CachedImageView::Descriptor& rhs) noexcept;
} //namespace Divide

#endif //VK_TEXTURE_H