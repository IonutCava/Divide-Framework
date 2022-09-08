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
    TextureDescriptor(const TextureType type) noexcept;
    TextureDescriptor(const TextureType type, const GFXDataFormat dataType) noexcept;
    TextureDescriptor(const TextureType type, const GFXDataFormat dataType, const GFXImageFormat format) noexcept;

    [[nodiscard]] size_t getHash() const noexcept override;

    PROPERTY_RW(U16, layerCount, 1);
    PROPERTY_RW(U16, mipBaseLevel, 0);
    PROPERTY_RW(U8,  msaaSamples, 0);
    PROPERTY_RW(GFXDataFormat,  dataType, GFXDataFormat::COUNT);
    PROPERTY_RW(GFXImageFormat, baseFormat, GFXImageFormat::COUNT);
    PROPERTY_RW(TextureType, texType, TextureType::COUNT);
    /// Use SRGB colour space
    PROPERTY_RW(bool, srgb, false);
    PROPERTY_RW(bool, normalized, true);
    PROPERTY_RW(ImageTools::ImportOptions, textureOptions);
    PROPERTY_RW(MipMappingState, mipMappingState, MipMappingState::AUTO);

    void addImageUsageFlag(const ImageUsage usage) noexcept;
    void removeImageUsageFlag(const ImageUsage usage) noexcept;
    [[nodiscard]] bool hasUsageFlagSet(const ImageUsage usage) const noexcept;

private:
    U32 _usageMask{ 1u << to_base(ImageUsage::SHADER_SAMPLE) };
};

[[nodiscard]] bool IsCompressed(GFXImageFormat format) noexcept;
[[nodiscard]] bool HasAlphaChannel (GFXImageFormat format) noexcept;

bool operator==(const TextureDescriptor& lhs, const TextureDescriptor& rhs) noexcept;
bool operator!=(const TextureDescriptor& lhs, const TextureDescriptor& rhs) noexcept;

bool IsCubeTexture(const TextureType texType) noexcept;
bool IsArrayTexture(const TextureType texType) noexcept;

U8 NumChannels(const GFXImageFormat format) noexcept;

}  // namespace Divide

#endif  _TEXTURE_DESCRIPTOR_H_

#include "TextureDescriptor.inl"
