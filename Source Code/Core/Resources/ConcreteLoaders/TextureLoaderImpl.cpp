#include "stdafx.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/TextureDescriptor.h"

namespace Divide {


template<>
CachedResource_ptr ImplResourceLoader<Texture>::operator()() {
    assert(_descriptor.enumValue() >= to_base(TextureType::TEXTURE_1D) &&
           _descriptor.enumValue() < to_base(TextureType::COUNT));

    const std::shared_ptr<TextureDescriptor>& texDescriptor = _descriptor.propertyDescriptor<TextureDescriptor>();
    // Samplers are not optional!
    assert(texDescriptor != nullptr);

    if (Texture::s_missingTextureFileName == nullptr) {
        Texture::s_missingTextureFileName = "missing_texture.jpg";
    }

    std::string resourceLocation = _descriptor.assetLocation().str();

    const size_t numCommas = std::count(std::cbegin(_descriptor.assetName().str()),
                                        std::cend(_descriptor.assetName().str()),
                                        ',');
    const size_t crtNumCommas = std::count(std::cbegin(resourceLocation),
                                           std::cend(resourceLocation),
                                           ',');

    if (texDescriptor->layerCount() < numCommas + 1) {
        texDescriptor->layerCount(to_U16(numCommas + 1));
    }

    if (crtNumCommas < numCommas ) {

        if (!resourceLocation.empty()) {
            stringstream textureLocationList(resourceLocation);
            while (std::getline(textureLocationList, resourceLocation, ',')) {}
        }  else {
            resourceLocation = (Paths::g_assetsLocation + Paths::g_texturesLocation).str();
        }

        for (size_t i = crtNumCommas; i < numCommas; ++i) {
            resourceLocation.append("," + resourceLocation);
        }

        _descriptor.assetLocation(ResourcePath(resourceLocation));
    }

    Texture_ptr ptr(_context.gfx().newTexture(_loadingDescriptorHash,
                                              _descriptor.resourceName(),
                                              _descriptor.assetName(),
                                              _descriptor.assetLocation(),
                                              !_descriptor.flag(),
                                              _descriptor.threaded(),
                                              *texDescriptor),
                    DeleteResource(_cache));

    if (!Load(ptr)) {
        Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LOADER_FILE")),
                         _descriptor.assetLocation().c_str(),
                         _descriptor.assetName().c_str(),
                         _descriptor.resourceName().c_str());
        ptr.reset();
    }

    return ptr;
}

}
