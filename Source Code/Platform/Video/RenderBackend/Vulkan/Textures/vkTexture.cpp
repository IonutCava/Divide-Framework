#include "stdafx.h"

#include "Headers/vkTexture.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

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
        _defaultView._textureData._textureType = _descriptor.texType();
        _defaultView._textureData._textureHandle = s_textureHandle.fetch_add(1u);
    }

    vkTexture::~vkTexture()
    {
        unload();
    }

    void vkTexture::updateDescriptor() {
        _vkDescriptor.sampler = _sampler;
        _vkDescriptor.imageView = _view;
        _vkDescriptor.imageLayout = _imageLayout;
    }

    bool vkTexture::unload() {
        auto device = VK_API::GetStateTracker()->_device->getVKDevice();

        vkDestroyImageView(device, _view, nullptr);
        vkDestroyImage(device, _image, nullptr);
        if (_sampler) {
            vkDestroySampler(device, _sampler, nullptr);
        }
        vkFreeMemory(device, _deviceMemory, nullptr);

        return true;
    }

    void vkTexture::reserveStorage() {
        //auto& physicalDevice = VK_API::GetStateTracker()->_device->getPhysicalDevice();
        //const VkFormat vkInternalFormat = VKUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
     
        updateDescriptor();
    }

    void vkTexture::bindLayer([[maybe_unused]] U8 slot, [[maybe_unused]] U8 level, [[maybe_unused]] U8 layer, [[maybe_unused]] bool layered, [[maybe_unused]] Image::Flag rw_flag) noexcept {
    }

    void vkTexture::submitTextureData() {
    }

    void vkTexture::prepareTextureData(const U16 width, const U16 height) {
        _loadingData = _defaultView._textureData;

        _width = width;
        _height = height;
        assert(_width > 0 && _height > 0 && "glTexture error: Invalid texture dimensions!");

        validateDescriptor();
        _loadingData._textureType = _descriptor.texType();

        _type = vkTextureTypeTable[to_U32(_loadingData._textureType)];

    }

    void vkTexture::loadDataCompressed([[maybe_unused]] const ImageTools::ImageData& imageData) {
    }

    void vkTexture::loadDataUncompressed([[maybe_unused]] const ImageTools::ImageData& imageData) {
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
