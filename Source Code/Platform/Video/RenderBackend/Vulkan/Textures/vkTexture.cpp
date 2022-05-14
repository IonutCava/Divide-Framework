#include "stdafx.h"

#include "Headers/vkTexture.h"

namespace Divide {
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
        _data._textureType = _descriptor.texType();
        _data._textureHandle = s_textureHandle.fetch_add(1u);
    }

    [[nodiscard]] SamplerAddress vkTexture::getGPUAddress([[maybe_unused]] size_t samplerHash) noexcept {
        return 0u;
    }

    void vkTexture::bindLayer([[maybe_unused]] U8 slot, [[maybe_unused]] U8 level, [[maybe_unused]] U8 layer, [[maybe_unused]] bool layered, [[maybe_unused]] Image::Flag rw_flag) noexcept {
    }

    void vkTexture::loadData([[maybe_unused]] const ImageTools::ImageData& imageLayers) noexcept {
    }

    void vkTexture::loadData([[maybe_unused]] const Byte* data, [[maybe_unused]] const size_t dataSize, [[maybe_unused]] const vec2<U16>& dimensions) noexcept {
    }

    void vkTexture::clearData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level) const noexcept {
    }

    void vkTexture::clearSubData([[maybe_unused]] const UColour4& clearColour, [[maybe_unused]] U8 level, [[maybe_unused]] const vec4<I32>& rectToClear, [[maybe_unused]] const vec2<I32>& depthRange) const noexcept {
    }

    Texture::TextureReadbackData vkTexture::readData([[maybe_unused]] U16 mipLevel, [[maybe_unused]] GFXDataFormat desiredFormat) const noexcept {
        TextureReadbackData data{};
        return MOV(data);
    }
}; //namespace Divide
