#include "stdafx.h"

#include "Headers/Texture.h"

#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

const char* Texture::s_missingTextureFileName = nullptr;

Texture::Texture(GFXDevice& context,
                 const size_t descriptorHash,
                 const Str256& name,
                 const ResourcePath& assetNames,
                 const ResourcePath& assetLocations,
                 const bool isFlipped,
                 const bool asyncLoad,
                 const TextureDescriptor& texDescriptor)
    : CachedResource(ResourceType::GPU_OBJECT, descriptorHash, name, assetNames, assetLocations),
      GraphicsResource(context, Type::TEXTURE, getGUID(), _ID(name.c_str())),
      _descriptor(texDescriptor),
      _data{0u, TextureType::COUNT},
      _numLayers(texDescriptor.layerCount()),
      _flipped(isFlipped),
      _asyncLoad(asyncLoad)
{
}

bool Texture::load() {
    if (_asyncLoad) {
        Start(*CreateTask([this](const Task & parent) { threadedLoad(); }),
              _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
    } else {
        threadedLoad();
    }
    return true;
}

/// Load texture data using the specified file name
void Texture::threadedLoad() {
    OPTICK_EVENT();

    constexpr std::array<std::string_view, 2> searchPattern = {
     "//", "\\"
    };


    // Each texture face/layer must be in a comma separated list
    stringstream textureLocationList(assetLocation().str());
    stringstream textureFileList(assetName().c_str());

#if defined(_DEBUG)
    _sourceFileList.reserve(6);
#endif

    ImageTools::ImageData dataStorage = {};
    // Flip image if needed
    dataStorage.flip(_flipped);
    dataStorage.set16Bit(_descriptor.dataType() == GFXDataFormat::FLOAT_16 ||
                         _descriptor.dataType() == GFXDataFormat::SIGNED_SHORT ||
                         _descriptor.dataType() == GFXDataFormat::UNSIGNED_SHORT);

    bool loadedFromFile = false;
    // We loop over every texture in the above list and store it in this temporary string
    string currentTextureFile;
    string currentTextureLocation;
    ResourcePath currentTextureFullPath;
    while (std::getline(textureLocationList, currentTextureLocation, ',') &&
           std::getline(textureFileList, currentTextureFile, ','))
    {
        Util::Trim(currentTextureFile);

        // Skip invalid entries
        if (!currentTextureFile.empty()) {

            currentTextureFullPath = currentTextureLocation.empty() ? Paths::g_texturesLocation : ResourcePath{currentTextureLocation};
            currentTextureFullPath.append("/" + currentTextureFile);
            Util::ReplaceStringInPlace(currentTextureFullPath, searchPattern, "/");
#if defined(_DEBUG)
            _sourceFileList.push_back(currentTextureFile);
#endif
            // Attempt to load the current entry
            if (!loadFile(currentTextureFullPath, dataStorage)) {
                // Invalid texture files are not handled yet, so stop loading
                continue;
            }

            loadedFromFile = true;
        }
    }

#if defined(_DEBUG)
    _sourceFileList.shrink_to_fit();
#endif

    if (loadedFromFile) {
        // Create a new Rendering API-dependent texture object
        _descriptor.compressed(dataStorage.compressed());
        _descriptor.baseFormat(dataStorage.format());
        _descriptor.dataType(dataStorage.dataType());
        // Uploading to the GPU dependents on the rendering API
        loadData(dataStorage);

        if (_descriptor.texType() == TextureType::TEXTURE_CUBE_MAP ||
            _descriptor.texType() == TextureType::TEXTURE_CUBE_ARRAY) {
            if (dataStorage.layerCount() % 6 != 0) {
                Console::errorfn(
                    Locale::Get(_ID("ERROR_TEXTURE_LOADER_CUBMAP_INIT_COUNT")),
                    resourceName().c_str());
                return;
            }
        }

        if (_descriptor.texType() == TextureType::TEXTURE_2D_ARRAY ||
            _descriptor.texType() == TextureType::TEXTURE_2D_ARRAY_MS) {
            if (dataStorage.layerCount() != _numLayers) {
                Console::errorfn(
                    Locale::Get(_ID("ERROR_TEXTURE_LOADER_ARRAY_INIT_COUNT")),
                    resourceName().c_str());
                return;
            }
        }

        /*if (_descriptor.texType() == TextureType::TEXTURE_CUBE_ARRAY) {
            if (dataStorage.layerCount() / 6 != _numLayers) {
                Console::errorfn(
                    Locale::Get(_ID("ERROR_TEXTURE_LOADER_ARRAY_INIT_COUNT")),
                    resourceName().c_str());
            }
        }*/
    }
}

U8 Texture::numChannels() const noexcept {
    switch(descriptor().baseFormat()) {
        case GFXImageFormat::RED:  return 1u;
        case GFXImageFormat::RG:   return 2u;
        case GFXImageFormat::RGB:  return 3u;
        case GFXImageFormat::RGBA: return 4u;
    }

    return 0u;
}

bool Texture::loadFile(const ResourcePath& name, ImageTools::ImageData& fileData) {

    if (!ImageTools::ImageDataInterface::CreateImageData(name, _width, _height, _descriptor.srgb(), fileData)) {
        if (fileData.layerCount() > 0) {
            Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LAYER_LOAD")), name.c_str());
            return false;
        }
        Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LOAD")), name.c_str());
        // Missing texture fallback.
        fileData.flip(false);
        // missing_texture.jpg must be something that really stands out
        ImageTools::ImageDataInterface::CreateImageData(Paths::g_assetsLocation + Paths::g_texturesLocation + s_missingTextureFileName, _width, _height, _descriptor.srgb(), fileData);
    } else {
        return checkTransparency(name, fileData);
    }

    return true;
}

bool Texture::checkTransparency(const ResourcePath& name, ImageTools::ImageData& fileData) {
    constexpr std::array<std::string_view, 2> searchPattern = {
        "//", "\\"
    };

    const U32 layer = to_U32(fileData.layerCount() - 1);

    // Extract width, height and bit depth
    const U16 width = fileData.dimensions(layer, 0u).width;
    const U16 height = fileData.dimensions(layer, 0u).height;
    // If we have an alpha channel, we must check for translucency/transparency

    auto[file, path] = splitPathToNameAndLocation(name.c_str());
    Util::ReplaceStringInPlace(path, searchPattern, "/");
    Util::ReplaceStringInPlace(path, "/", "_");
    if (path.str().back() == '_') {
        path.pop_back();
    }
    const ResourcePath cachePath = Paths::g_cacheLocation + Paths::Textures::g_metadataLocation + path + "/" ;
    const ResourcePath cacheName = file + ".cache";

    ByteBuffer metadataCache;
    bool skip = false;
    if (metadataCache.loadFromFile(cachePath.c_str(), cacheName.c_str())) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        metadataCache >> tempVer;
        if (tempVer == BYTE_BUFFER_VERSION) {
            metadataCache >> _hasTransparency;
            metadataCache >> _hasTranslucency;
            skip = true;
        } else {
            metadataCache.clear();
        }
    }

    if (!skip) {
        STUBBED("ToDo: Add support for 16bit and HDR image alpha! -Ionut");
        if (fileData.alpha()) {

            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = width;
            descriptor._partitionSize = std::max(16u, to_U32(width / 10));
            descriptor._useCurrentThread = true;
            descriptor._cbk =  [this, &fileData, height, layer](const Task* /*parent*/, const U32 start, const U32 end) {
                U8 tempA = 0u;
                for (U32 i = start; i < end; ++i) {
                    for (I32 j = 0; j < height; ++j) {
                        if (_hasTransparency && _hasTranslucency) {
                            return;
                        }
                        fileData.getAlpha(i, j, tempA, layer);
                        if (IS_IN_RANGE_INCLUSIVE(tempA, 0, 254)) {
                            _hasTransparency = true;
                            _hasTranslucency = tempA > 1;
                            if (_hasTranslucency) {
                                return;
                            }
                        }
                    }
                }
            };

            parallel_for(_context.context(), descriptor);
            metadataCache << BYTE_BUFFER_VERSION;
            metadataCache << _hasTransparency;
            metadataCache << _hasTranslucency;
            if (!metadataCache.dumpToFile(cachePath.c_str(), cacheName.c_str())) {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    Console::printfn(Locale::Get(_ID("TEXTURE_HAS_TRANSPARENCY_TRANSLUCENCY")),
                                    name.c_str(),
                                    _hasTransparency ? "yes" : "no",
                                    _hasTranslucency ? "yes" : "no");

    return true;
}

void Texture::setSampleCount(U8 newSampleCount) noexcept { 
    CLAMP(newSampleCount, to_U8(0u), _context.gpuState().maxMSAASampleCount());
    if (_descriptor.msaaSamples() != newSampleCount) {
        _descriptor.msaaSamples(newSampleCount);
        loadData({nullptr, 0 }, { width(), height() });
    }
}

};
