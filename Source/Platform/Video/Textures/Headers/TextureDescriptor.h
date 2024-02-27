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
#ifndef _TEXTURE_DESCRIPTOR_H_
#define _TEXTURE_DESCRIPTOR_H_

#include "Core/Resources/Headers/ResourceDescriptor.h"
#include "Utility/Headers/ImageToolsFwd.h"

namespace Divide {

struct SamplerDescriptor;

struct PixelAlignment
{
    size_t _alignment{ 4u };
    size_t _rowLength{ 0u };
    size_t _skipPixels{ 0u };
    size_t _skipRows{ 0u };
};

/// Use to define a texture with details such as type, image formats, etc
/// We do not define copy constructors as we must define descriptors only with POD
class TextureDescriptor final : public PropertyDescriptor {
   public:
    enum class MipMappingState : U8 {
        AUTO = 0,
        MANUAL,
        OFF,
        COUNT
    };

    TextureDescriptor() noexcept;
    TextureDescriptor(const TextureType type, const GFXDataFormat dataType, const GFXImageFormat format, const GFXImagePacking packing = GFXImagePacking::NORMALIZED) noexcept;

    [[nodiscard]] size_t getHash() const noexcept override;

    PROPERTY_RW(U16, layerCount,   0u);
    PROPERTY_RW(U16, mipBaseLevel, 0u);
    PROPERTY_RW(GFXDataFormat,  dataType, GFXDataFormat::COUNT);
    PROPERTY_RW(GFXImageFormat, baseFormat, GFXImageFormat::COUNT);
    PROPERTY_RW(GFXImagePacking, packing, GFXImagePacking::COUNT);
    PROPERTY_RW(TextureType, texType, TextureType::COUNT);
    PROPERTY_RW(ImageTools::ImportOptions, textureOptions);
    PROPERTY_RW(MipMappingState, mipMappingState, MipMappingState::AUTO);
    PROPERTY_RW(U8,  msaaSamples,  0u);
    PROPERTY_RW(bool, allowRegionUpdates, false);

    void addImageUsageFlag(const ImageUsage usage) noexcept;
    void removeImageUsageFlag(const ImageUsage usage) noexcept;
    [[nodiscard]] bool hasUsageFlagSet(const ImageUsage usage) const noexcept;

private:
    U32 _usageMask{ 1u << to_base(ImageUsage::SHADER_READ) };
};

[[nodiscard]] bool IsCompressed(GFXImageFormat format) noexcept;
[[nodiscard]] bool HasAlphaChannel (GFXImageFormat format) noexcept;

bool operator==(const TextureDescriptor& lhs, const TextureDescriptor& rhs) noexcept;
bool operator!=(const TextureDescriptor& lhs, const TextureDescriptor& rhs) noexcept;

[[nodiscard]] bool Is1DTexture(TextureType texType) noexcept;
[[nodiscard]] bool Is2DTexture(TextureType texType) noexcept;
[[nodiscard]] bool Is3DTexture(TextureType texType) noexcept;
[[nodiscard]] bool IsCubeTexture(TextureType texType) noexcept;
[[nodiscard]] bool IsArrayTexture(TextureType texType) noexcept;
[[nodiscard]] bool IsNormalizedTexture(GFXImagePacking packing) noexcept;
[[nodiscard]] bool IsDepthTexture(GFXImagePacking packing) noexcept;
[[nodiscard]] bool SupportsZOffsetTexture(TextureType texType) noexcept;
[[nodiscard]] bool IsBGRTexture(GFXImageFormat format) noexcept;
[[nodiscard]] U8   NumChannels(GFXImageFormat format) noexcept;

}  // namespace Divide

#endif  _TEXTURE_DESCRIPTOR_H_

#include "TextureDescriptor.inl"
