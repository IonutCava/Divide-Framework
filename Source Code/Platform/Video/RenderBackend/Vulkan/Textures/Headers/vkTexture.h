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

//#include <Vulkan/vulkan_core.h>
#include "Platform/Video/RenderBackend/Vulkan/Headers/VMAInclude.h"

namespace Divide {
    struct AllocatedImage : private NonCopyable {
        ~AllocatedImage();

        VkImage _image{VK_NULL_HANDLE};
        VmaAllocation _allocation{ VK_NULL_HANDLE };
        VmaAllocationInfo _allocInfo{};
    };

    FWD_DECLARE_MANAGED_STRUCT(AllocatedImage);

    class vkTexture final : public Texture {
    public:
        vkTexture(GFXDevice& context,
                  const size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& assetNames,
                  const ResourcePath& assetLocations,
                  const TextureDescriptor& texDescriptor,
                  ResourceCache& parentCache);

        ~vkTexture();

        bool unload() override;

        void bindLayer(U8 slot, U8 level, U8 layer, bool layered, Image::Flag rw_flag) noexcept override;

        void clearData(const UColour4& clearColour, U8 level) const noexcept override;

        void clearSubData(const UColour4& clearColour, U8 level, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const noexcept override;

        TextureReadbackData readData(U16 mipLevel, GFXDataFormat desiredFormat) const noexcept override;

        PROPERTY_R(AllocatedImage_uptr, image, nullptr);
        PROPERTY_R_IW(VkImageType, type, VK_IMAGE_TYPE_MAX_ENUM);
        PROPERTY_R_IW(VkImageView, view, VK_NULL_HANDLE);
        PROPERTY_R_IW(VkFormat, internalFormat, VK_FORMAT_MAX_ENUM);

    private:
        void reserveStorage();
        void loadDataInternal(const ImageTools::ImageData& imageData) override;
        void prepareTextureData(U16 width, U16 height, U16 depth) override;
        void submitTextureData() override;
        void generateTextureMipmap(VkCommandBuffer cmd, U8 baseLevel);
        void clearDataInternal(const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const;
    };
} //namespace Divide

#endif //VK_TEXTURE_H