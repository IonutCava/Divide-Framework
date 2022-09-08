#include "stdafx.h"

#include "Headers/TextureDescriptor.h"

namespace Divide {

    TextureDescriptor::TextureDescriptor() noexcept
        : TextureDescriptor(TextureType::TEXTURE_2D)
    {
    }

    TextureDescriptor::TextureDescriptor(const TextureType type) noexcept
        : TextureDescriptor(type, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGB)
    {
    }

    TextureDescriptor::TextureDescriptor(const TextureType type, const GFXDataFormat dataType) noexcept
        : TextureDescriptor(type, dataType, GFXImageFormat::RGB)
    {
    }

    TextureDescriptor::TextureDescriptor(const TextureType type, const GFXDataFormat dataType, const GFXImageFormat format) noexcept
        : PropertyDescriptor(DescriptorType::DESCRIPTOR_TEXTURE),
         _dataType(dataType),
         _baseFormat(format),
         _texType(type)
    {
    }

    void TextureDescriptor::addImageUsageFlag(const ImageUsage usage) noexcept {
        SetBit(_usageMask, 1u << to_base(usage));
    }

    void TextureDescriptor::removeImageUsageFlag(const ImageUsage usage) noexcept {
        ClearBit(_usageMask, 1u << to_base(usage));
    }

    bool TextureDescriptor::hasUsageFlagSet(const ImageUsage usage) const noexcept {
        return BitCompare(_usageMask, 1u << to_base(usage));
    }

    size_t TextureDescriptor::getHash() const noexcept {
        _hash = PropertyDescriptor::getHash();

        Util::Hash_combine(_hash, _layerCount,
                                  _mipBaseLevel,
                                  to_base(_mipMappingState),
                                  _msaaSamples,
                                  to_U32(_dataType),
                                  to_U32(_baseFormat),
                                  to_U32(_texType),
                                  _srgb,
                                  _normalized,
                                  _usageMask,
                                  _textureOptions._alphaChannelTransparency,
                                  _textureOptions._fastCompression,
                                  _textureOptions._isNormalMap,
                                  _textureOptions._mipFilter,
                                  _textureOptions._skipMipMaps,
                                  _textureOptions._outputFormat,
                                  _textureOptions._outputSRGB,
                                  _textureOptions._useDDSCache);

        return _hash;
    }
    
    [[nodiscard]] bool IsCompressed(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1            ||
               format == GFXImageFormat::BC1a           ||
               format == GFXImageFormat::BC2            ||
               format == GFXImageFormat::BC3            ||
               format == GFXImageFormat::BC3n           ||
               format == GFXImageFormat::BC4s           ||
               format == GFXImageFormat::BC4u           ||
               format == GFXImageFormat::BC5s           ||
               format == GFXImageFormat::BC5u           ||
               format == GFXImageFormat::BC6s           ||
               format == GFXImageFormat::BC6u           ||
               format == GFXImageFormat::BC7            ||
               format == GFXImageFormat::BC7_SRGB       ||
               format == GFXImageFormat::DXT1_RGB_SRGB  ||
               format == GFXImageFormat::DXT1_RGBA_SRGB ||
               format == GFXImageFormat::DXT5_RGBA_SRGB ||
               format == GFXImageFormat::DXT3_RGBA_SRGB;
    }

    [[nodiscard]] bool HasAlphaChannel(const GFXImageFormat format) noexcept {
        return format == GFXImageFormat::BC1a           ||
               format == GFXImageFormat::BC2            ||
               format == GFXImageFormat::BC3            ||
               format == GFXImageFormat::BC3n           ||
               format == GFXImageFormat::BC7            ||
               format == GFXImageFormat::BC7_SRGB       ||
               format == GFXImageFormat::DXT1_RGBA_SRGB ||
               format == GFXImageFormat::DXT3_RGBA_SRGB ||
               format == GFXImageFormat::DXT5_RGBA_SRGB ||
               format == GFXImageFormat::BGRA           ||
               format == GFXImageFormat::RGBA;
    }

} //namespace Divide
