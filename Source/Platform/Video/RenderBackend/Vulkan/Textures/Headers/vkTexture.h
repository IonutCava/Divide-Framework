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
    FWD_DECLARE_MANAGED_STRUCT( VMABuffer );

    class vkTexture final : public Texture
    {
    public:
        enum class TransitionType : U8
        {
            UNDEFINED_TO_COLOUR_ATTACHMENT = 0u,
            UNDEFINED_TO_COLOUR_RESOLVE_ATTACHMENT,
            UNDEFINED_TO_DEPTH_ATTACHMENT,
            UNDEFINED_TO_DEPTH_RESOLVE_ATTACHMENT,
            UNDEFINED_TO_DEPTH_STENCIL_ATTACHMENT,
            UNDEFINED_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT,

            SHADER_READ_TO_COLOUR_ATTACHMENT,
            SHADER_READ_TO_COLOUR_RESOLVE_ATTACHMENT,
            SHADER_READ_TO_DEPTH_ATTACHMENT,
            SHADER_READ_TO_DEPTH_RESOLVE_ATTACHMENT,
            SHADER_READ_TO_DEPTH_STENCIL_ATTACHMENT,
            SHADER_READ_TO_DEPTH_STENCIL_RESOLVE_ATTACHMENT,

            COLOUR_ATTACHMENT_TO_SHADER_READ,
            COLOUR_RESOLVE_ATTACHMENT_TO_SHADER_READ,
            DEPTH_ATTACHMENT_TO_SHADER_READ,
            DEPTH_RESOLVE_ATTACHMENT_TO_SHADER_READ,
            DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ,
            DEPTH_STENCIL_RESOLVE_ATTACHMENT_TO_SHADER_READ,

            COLOUR_ATTACHMENT_TO_SHADER_WRITE,
            DEPTH_ATTACHMENT_TO_SHADER_WRITE,
            DEPTH_STENCIL_ATTACHMENT_TO_SHADER_WRITE,

            COLOUR_ATTACHMENT_TO_SHADER_READ_WRITE,
            DEPTH_ATTACHMENT_TO_SHADER_READ_WRITE,
            DEPTH_STENCIL_ATTACHMENT_TO_SHADER_READ_WRITE,

            UNDEFINED_TO_SHADER_READ_COLOUR,
            UNDEFINED_TO_SHADER_READ_DEPTH,

            UNDEFINED_TO_SHADER_READ_WRITE,

            GENERAL_TO_SHADER_READ_COLOUR,
            GENERAL_TO_SHADER_READ_DEPTH,

            UNDEFINED_TO_GENERAL,
            SHADER_READ_COLOUR_TO_GENERAL,
            SHADER_READ_DEPTH_TO_GENERAL,

            SHADER_READ_COLOUR_TO_SHADER_READ_WRITE,
            SHADER_READ_DEPTH_TO_SHADER_READ_WRITE,

            SHADER_READ_WRITE_TO_SHADER_READ_COLOUR,
            SHADER_READ_WRITE_TO_SHADER_READ_DEPTH,

            SHADER_READ_TO_BLIT_READ_COLOUR,
            SHADER_READ_TO_BLIT_WRITE_COLOUR,
            BLIT_READ_TO_SHADER_READ_COLOUR,
            BLIT_WRITE_TO_SHADER_READ_COLOUR,

            SHADER_READ_TO_BLIT_READ_DEPTH,
            SHADER_READ_TO_BLIT_WRITE_DEPTH,
            BLIT_READ_TO_SHADER_READ_DEPTH,
            BLIT_WRITE_TO_SHADER_READ_DEPTH,

            SHADER_READ_TO_COPY_WRITE_COLOUR,
            SHADER_READ_TO_COPY_WRITE_DEPTH,

            SHADER_READ_TO_COPY_READ_COLOUR,
            SHADER_READ_TO_COPY_READ_DEPTH,

            COPY_READ_TO_SHADER_READ_COLOUR,
            COPY_READ_TO_SHADER_READ_DEPTH,

            COPY_WRITE_TO_SHADER_READ_COLOUR,
            COPY_WRITE_TO_SHADER_READ_DEPTH,

            SHADER_READ_WRITE_TO_COPY_READ,

            GENERAL_TO_COPY_READ,
            GENERAL_TO_COPY_WRITE,
            COPY_READ_TO_GENERAL,
            COPY_WRITE_TO_GENERAL,

            COPY_WRITE_TO_COPY_READ,

            COUNT
        };
        struct CachedImageView
        {
            struct Descriptor final : Hashable
            {
                ImageSubRange _subRange{};
                VkFormat _format{ VK_FORMAT_MAX_ENUM };
                TextureType _type{ TextureType::COUNT };
                ImageUsage _usage{ ImageUsage::COUNT };
                bool _resolveTarget{false};
                [[nodiscard]] size_t getHash() const noexcept override;

            } _descriptor;
            VkImageView _view{VK_NULL_HANDLE};
        };

        vkTexture( GFXDevice& context,
                   const size_t descriptorHash,
                   const std::string_view name,
                   std::string_view assetNames,
                   const ResourcePath& assetLocations,
                   const TextureDescriptor& texDescriptor,
                   ResourceCache& parentCache );

        virtual ~vkTexture() override;

        bool unload() override;

        void clearData( VkCommandBuffer cmdBuffer, const UColour4& clearColour, SubRange layerRange, U8 mipLevel ) const noexcept;
        void generateMipmaps( VkCommandBuffer cmdBuffer, U16 baseLevel, U16 baseLayer, U16 layerCount, ImageUsage crtUsage);

        PROPERTY_R( AllocatedImage_uptr, image, nullptr );
        PROPERTY_R_IW( VkImageType, vkType, VK_IMAGE_TYPE_MAX_ENUM );
        PROPERTY_R_IW( VkFormat, vkFormat, VK_FORMAT_MAX_ENUM );
        PROPERTY_R_IW( VkSampleCountFlagBits, sampleFlagBits, VK_SAMPLE_COUNT_1_BIT );

        [[nodiscard]] ImageReadbackData readData(U8 mipLevel, const PixelAlignment& pixelPackAlignment) const noexcept override;
        [[nodiscard]] ImageReadbackData readData( VkCommandBuffer cmdBuffer, U8 mipLevel, const PixelAlignment& pixelPackAlignment) const noexcept;
        [[nodiscard]] VkImageView getImageView( const CachedImageView::Descriptor& descriptor ) const;

        [[nodiscard]] static VkImageAspectFlags GetAspectFlags( const TextureDescriptor& descriptor ) noexcept;

        static void Copy( VkCommandBuffer cmdBuffer, const vkTexture* source, const U8 sourceSamples, const vkTexture* destination, const U8 destinationSamples, CopyTexParams params );

        static void TransitionTexture( TransitionType type, const VkImageSubresourceRange& subresourceRange, VkImage image,  VkImageMemoryBarrier2& memBarrier );

    private:
        void loadDataInternal( const ImageTools::ImageData& imageData, const vec3<U16>& offset, const PixelAlignment& pixelUnpackAlignment ) override;
        void loadDataInternal( const Byte* data, size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment ) override;
        void prepareTextureData( U16 width, U16 height, U16 depth, bool emptyAllocation ) override;
        void clearDataInternal( const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, vec2<I32> depthRange ) const;
        void clearImageViewCache();

        void loadDataInternal( const Byte* data, size_t size, U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment, bool emptyAllocation );
    private:
        struct Mip
        {
            vec3<U16> _dimensions{};
            size_t _size{0u};
        };

        mutable hashMap<size_t, CachedImageView> _imageViewCache;
        VMABuffer_uptr _stagingBuffer;
        VkDeviceSize _stagingBufferSize{ 0u };
        vector<Mip> _mipData;
        U8 _testRefreshCounter { 0u };
    };

} //namespace Divide

#endif //VK_TEXTURE_H
