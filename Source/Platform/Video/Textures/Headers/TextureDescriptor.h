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
#ifndef DVD_TEXTURE_DESCRIPTOR_H_
#define DVD_TEXTURE_DESCRIPTOR_H_

#include "Utility/Headers/ImageToolsFwd.h"
#include "Core/Resources/Headers/Resource.h"

namespace Divide {

struct SamplerDescriptor;
class Texture;

struct PixelAlignment
{
    size_t _alignment{ 4u };
    size_t _rowLength{ 0u };
    size_t _skipPixels{ 0u };
    size_t _skipRows{ 0u };
};

enum class MipMappingState : U8
{
    AUTO = 0,
    MANUAL,
    OFF,
    COUNT
};

/// Use to define a texture with details such as type, image formats, etc
/// We do not define copy constructors as we must define descriptors only with POD
template<>
struct PropertyDescriptor<Texture>
{
   public:

    ImageTools::ImportOptions _textureOptions{};
    U32 _usageMask{ 1u << to_base(ImageUsage::SHADER_READ) };
    U16 _layerCount{ 0u };
    U16 _mipBaseLevel{ 0u };
    TextureType _texType{ TextureType::TEXTURE_2D };
    GFXDataFormat _dataType{ GFXDataFormat::UNSIGNED_BYTE };
    GFXImagePacking _packing{ GFXImagePacking::NORMALIZED };
    GFXImageFormat _baseFormat{ GFXImageFormat::RGBA };
    MipMappingState _mipMappingState{ MipMappingState::AUTO };
    U8 _msaaSamples{ 0u };
    bool _allowRegionUpdates{ false };
};

using TextureDescriptor = PropertyDescriptor<Texture>;

template<>
inline size_t GetHash( const PropertyDescriptor<Texture>& descriptor ) noexcept;

              void AddImageUsageFlag( PropertyDescriptor<Texture>& descriptor, const ImageUsage usage ) noexcept;
              void RemoveImageUsageFlag( PropertyDescriptor<Texture>& descriptor, const ImageUsage usage ) noexcept;
[[nodiscard]] bool HasUsageFlagSet( const PropertyDescriptor<Texture>& descriptor, const ImageUsage usage ) noexcept;

[[nodiscard]] bool IsCompressed(GFXImageFormat format) noexcept;
[[nodiscard]] bool HasAlphaChannel (GFXImageFormat format) noexcept;

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

#endif //DVD_TEXTURE_DESCRIPTOR_H_

#include "TextureDescriptor.inl"
