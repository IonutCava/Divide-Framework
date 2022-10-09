#include "stdafx.h"

#include "Headers/Texture.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

constexpr U16 BYTE_BUFFER_VERSION = 1u;

ResourcePath Texture::s_missingTextureFileName("missing_texture.jpg");

Texture_ptr Texture::s_defaulTexture = nullptr;
bool Texture::s_useDDSCache = true;

void Texture::OnStartup(GFXDevice& gfx) {
    ImageTools::OnStartup(gfx.renderAPI() != RenderAPI::OpenGL);

    TextureDescriptor textureDescriptor(TextureType::TEXTURE_2D_ARRAY);
    textureDescriptor.srgb(false);
    textureDescriptor.baseFormat(GFXImageFormat::RGBA);

    ResourceDescriptor textureResourceDescriptor("defaultEmptyTexture");
    textureResourceDescriptor.propertyDescriptor(textureDescriptor);
    textureResourceDescriptor.waitForReady(true);
    s_defaulTexture = CreateResource<Texture>(gfx.parent().resourceCache(), textureResourceDescriptor);

    Byte* defaultTexData = MemoryManager_NEW Byte[1u * 1u * 4];
    defaultTexData[0] = defaultTexData[1] = defaultTexData[2] = to_byte(0u); //RGB: black
    defaultTexData[3] = to_byte(1u); //Alpha: 1

    ImageTools::ImageData imgDataDefault = {};
    if (!imgDataDefault.loadFromMemory(defaultTexData, 4, 1u, 1u, 1u, 4)) {
        DIVIDE_UNEXPECTED_CALL();
    }
    s_defaulTexture->loadData(imgDataDefault);
    MemoryManager::DELETE_ARRAY(defaultTexData);
}

void Texture::OnShutdown() noexcept {
    s_defaulTexture.reset();
    ImageTools::OnShutdown();
}

bool Texture::UseTextureDDSCache() noexcept {
    return s_useDDSCache;
}

const Texture_ptr& Texture::DefaultTexture() noexcept {
    return s_defaulTexture;
}

ResourcePath Texture::GetCachePath(ResourcePath originalPath) noexcept {
    constexpr std::array<std::string_view, 2> searchPattern = {
        "//", "\\"
    };

    Util::ReplaceStringInPlace(originalPath, searchPattern, "/");
    Util::ReplaceStringInPlace(originalPath, "/", "_");
    if (originalPath.str().back() == '_') {
        originalPath.pop_back();
    }
    const ResourcePath cachePath = Paths::g_cacheLocation + Paths::Textures::g_metadataLocation + originalPath + "/";

    return cachePath;
}

U8 Texture::GetSizeFactor(const GFXDataFormat format) noexcept {
    switch (format) {
        case GFXDataFormat::UNSIGNED_BYTE:
        case GFXDataFormat::SIGNED_BYTE: return 1u;

        case GFXDataFormat::UNSIGNED_SHORT:
        case GFXDataFormat::SIGNED_SHORT:
        case GFXDataFormat::FLOAT_16: return 2u;

        case GFXDataFormat::UNSIGNED_INT:
        case GFXDataFormat::SIGNED_INT:
        case GFXDataFormat::FLOAT_32: return 4u;
    };

    return 1u;
}

Texture::Texture(GFXDevice& context,
                 const size_t descriptorHash,
                 const Str256& name,
                 const ResourcePath& assetNames,
                 const ResourcePath& assetLocations,
                 const TextureDescriptor& texDescriptor,
                 ResourceCache& parentCache)
    : CachedResource(ResourceType::GPU_OBJECT, descriptorHash, name, assetNames, assetLocations),
      GraphicsResource(context, Type::TEXTURE, getGUID(), _ID(name.c_str())),
      _descriptor(texDescriptor),
      _numLayers(texDescriptor.layerCount()),
      _parentCache(parentCache)
{
    _defaultView._srcTexture._internalTexture = this;
    _defaultView._mipLevels.max = 1u;
    _defaultView._isDefaultView = true;
    _defaultView._usage = ImageUsage::UNDEFINED;
}

Texture::~Texture()
{
    _parentCache.remove(this);
}

bool Texture::load() {
    Start(*CreateTask([this]([[maybe_unused]] const Task & parent) { threadedLoad(); }),
            _context.context().taskPool(TaskPoolType::HIGH_PRIORITY), TaskPriority::DONT_CARE,
           [&]() {
               postLoad();
           });

    return true;
}

bool Texture::unload()
{
    _defaultView._usage = ImageUsage::UNDEFINED;

    return CachedResource::unload();
}

void Texture::postLoad()
{
    _defaultView._descriptor._baseFormat = _descriptor.baseFormat();
    _defaultView._descriptor._dataType = _descriptor.dataType();
    _defaultView._descriptor._srgb = _descriptor.srgb();
    _defaultView._descriptor._normalized = _descriptor.normalized();
    _defaultView._descriptor._msaaSamples = _descriptor.msaaSamples();
    _defaultView._layerRange = {0u, _numLayers};
    _defaultView._usage = ImageUsage::SHADER_SAMPLE;
}

/// Load texture data using the specified file name
void Texture::threadedLoad()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

    if (!assetLocation().empty())
    {
        const GFXDataFormat requestedFormat = _descriptor.dataType();
        assert(requestedFormat == GFXDataFormat::UNSIGNED_BYTE ||  // Regular image format
               requestedFormat == GFXDataFormat::UNSIGNED_SHORT || // 16Bit
               requestedFormat == GFXDataFormat::FLOAT_32 ||       // HDR
               requestedFormat == GFXDataFormat::COUNT);           // Auto

        constexpr std::array<std::string_view, 2> searchPattern
        {
            "//", "\\"
        };


        // Each texture face/layer must be in a comma separated list
        stringstream textureLocationList(assetLocation().str());
        stringstream textureFileList(assetName().c_str());

        ImageTools::ImageData dataStorage = {};
        dataStorage.requestedFormat(requestedFormat);

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
                Util::ReplaceStringInPlace(currentTextureFile, searchPattern, "/");
                currentTextureFullPath = currentTextureLocation.empty() ? Paths::g_texturesLocation : ResourcePath{ currentTextureLocation };
                const auto[file, path] = splitPathToNameAndLocation(currentTextureFile.c_str());
                if (!path.empty()) {
                    currentTextureFullPath += path;
                }
                
                Util::ReplaceStringInPlace(currentTextureFullPath, searchPattern, "/");
                
                // Attempt to load the current entry
                if (!loadFile(currentTextureFullPath, file, dataStorage)) {
                    // Invalid texture files are not handled yet, so stop loading
                    continue;
                }

                loadedFromFile = true;
            }
        }

        if (loadedFromFile) {
            // Create a new Rendering API-dependent texture object
            _descriptor.baseFormat(dataStorage.format());
            _descriptor.dataType(dataStorage.dataType());
            // Uploading to the GPU dependents on the rendering API
            loadData(dataStorage);

            if (IsCubeTexture(_descriptor.texType())) {
                if (dataStorage.layerCount() % 6 != 0) {
                    Console::errorfn(
                        Locale::Get(_ID("ERROR_TEXTURE_LOADER_CUBMAP_INIT_COUNT")),
                        resourceName().c_str());
                    return;
                }
            }

            if (_descriptor.texType() == TextureType::TEXTURE_1D_ARRAY ||
                _descriptor.texType() == TextureType::TEXTURE_2D_ARRAY) {
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

    CachedResource::load();
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

bool Texture::loadFile(const ResourcePath& path, const ResourcePath& name, ImageTools::ImageData& fileData) {

    if (!fileExists(path + name) || 
        !fileData.loadFromFile(_descriptor.srgb(),
                               _width,
                               _height,
                               path,
                               name,
                               _descriptor.textureOptions())) 
    {
        if (fileData.layerCount() > 0) {
            Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LAYER_LOAD")), name.c_str());
            return false;
        }
        Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LOAD")), name.c_str());
        // missing_texture.jpg must be something that really stands out
        if (!fileData.loadFromFile(_descriptor.srgb(), _width, _height, Paths::g_assetsLocation + Paths::g_texturesLocation, s_missingTextureFileName)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    } else {
        return checkTransparency(path, name, fileData);
    }

    return true;
}

void Texture::prepareTextureData(const U16 width, const U16 height, const U16 depth, [[maybe_unused]] const bool emptyAllocation) {
    _width = width;
    _height = height;
    _depth = depth;
    DIVIDE_ASSERT(_width > 0 && _height > 0 && _depth > 0, "Texture error: Invalid texture dimensions!");

    validateDescriptor();
}

void Texture::loadData(const Byte* data, size_t dataSize, const vec2<U16> dimensions) {
    loadData(data, dataSize, vec3<U16>(dimensions.width, dimensions.height, 1u));
}

void Texture::loadData(const Byte* data, const size_t dataSize, const vec3<U16>& dimensions) {
    // This should never be called for compressed textures
    assert(!IsCompressed(_descriptor.baseFormat()));

    prepareTextureData(dimensions.width, dimensions.height, dimensions.depth, dataSize == 0u || data == nullptr);

    // We can't manually specify data for msaa textures.
    assert(_descriptor.msaaSamples() == 0u || data == nullptr);
    if (data != nullptr) {
        ImageTools::ImageData imgData{};
        if (imgData.loadFromMemory(data, dataSize, dimensions.width, dimensions.height, 1, GetSizeFactor(_descriptor.dataType()) * NumChannels(_descriptor.baseFormat()))) {
            loadDataInternal(imgData);
        }
    }

    submitTextureData();
}

void Texture::loadData(const ImageTools::ImageData& imageData) {
    prepareTextureData(imageData.dimensions(0u, 0u).width, imageData.dimensions(0u, 0u).height, imageData.dimensions(0u, 0u).depth, false);

    if (IsCompressed(_descriptor.baseFormat()) &&
        _descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO)
    {
        _descriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
    }

    loadDataInternal(imageData);

    submitTextureData();
}

bool Texture::checkTransparency(const ResourcePath& path, const ResourcePath& name, ImageTools::ImageData& fileData) {

    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    if (fileData.ignoreAlphaChannelTransparency() || fileData.hasDummyAlphaChannel()) {
        _hasTransparency = false;
        _hasTranslucency = false;
        return true;
    }

    const U32 layer = to_U32(fileData.layerCount() - 1);

    // Extract width, height and bit depth
    const U16 width = fileData.dimensions(layer, 0u).width;
    const U16 height = fileData.dimensions(layer, 0u).height;
    // If we have an alpha channel, we must check for translucency/transparency

    const ResourcePath cachePath = GetCachePath(path);
    const ResourcePath cacheName = name + ".cache";

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
        if (HasAlphaChannel(fileData.format())) {
            bool hasTransulenctOrOpaquePixels = false;
            // Allo about 4 pixels per partition to be ignored
            constexpr U32 transparentPixelsSkipCount = 4u;

            std::atomic_uint transparentPixelCount = 0u;

            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = width;
            descriptor._partitionSize = std::max(16u, to_U32(width / 10));
            descriptor._useCurrentThread = true;
            descriptor._cbk =  [this, &fileData, &hasTransulenctOrOpaquePixels, height, layer, &transparentPixelCount, transparentPixelsSkipCount](const Task* /*parent*/, const U32 start, const U32 end) {
                U8 tempA = 0u;
                for (U32 i = start; i < end; ++i) {
                    for (I32 j = 0; j < height; ++j) {
                        if (_hasTransparency && (_hasTranslucency || hasTransulenctOrOpaquePixels)) {
                            return;
                        }
                        fileData.getAlpha(i, j, tempA, layer);
                        if (IS_IN_RANGE_INCLUSIVE(tempA, 0, 250)) {
                            if (transparentPixelCount.fetch_add(1u) >= transparentPixelsSkipCount) {
                                _hasTransparency = true;
                                _hasTranslucency = tempA > 1;
                                if (_hasTranslucency) {
                                    hasTransulenctOrOpaquePixels = true;
                                    return;
                                }
                            }
                        } else if (tempA > 250) {
                            hasTransulenctOrOpaquePixels = true;
                        }
                    }
                }
            };
            if (_hasTransparency && !_hasTranslucency && !hasTransulenctOrOpaquePixels) {
                // All the alpha values are 0, so this channel is useless.
                _hasTransparency = _hasTranslucency = false;
            }
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

void Texture::setSampleCount(U8 newSampleCount) { 
    CLAMP(newSampleCount, to_U8(0u), _context.gpuState().maxMSAASampleCount());
    if (_descriptor.msaaSamples() != newSampleCount) {
        _descriptor.msaaSamples(newSampleCount);
        loadData(nullptr, 0u, { width(), height(), depth()});
    }
}

bool Texture::imageUsage(const ImageUsage usage)
{
    if (_defaultView._usage != usage)
    {
        _defaultView._usage = usage;
        return true;
    }

    return false;
}

ImageUsage Texture::imageUsage() const noexcept
{
    return _defaultView._usage;
}

void Texture::validateDescriptor() {
    // Select the proper colour space internal format
    if (_descriptor.baseFormat() == GFXImageFormat::RED ||
        _descriptor.baseFormat() == GFXImageFormat::RG ||
        _descriptor.baseFormat() == GFXImageFormat::DEPTH_COMPONENT ||
        _descriptor.baseFormat() == GFXImageFormat::DEPTH_STENCIL_COMPONENT)
    {
        // We only support 8 bit per pixel - 3 & 4 channel textures
        assert(!_descriptor.srgb());
    }

    // Cap upper mip count limit
    if (_width > 0 && _height > 0) {
        //http://www.opengl.org/registry/specs/ARB/texture_non_power_of_two.txt
        if (descriptor().mipMappingState() != TextureDescriptor::MipMappingState::OFF) {
            _defaultView._mipLevels.max = to_U16(std::floorf(std::log2f(std::fmaxf(to_F32(_width), to_F32(_height))))) + 1;
        } else {
            _defaultView._mipLevels.max = 1u;
        }
    }
}

U16 Texture::mipCount() const noexcept {
    return _defaultView._mipLevels.max;
}

ImageView Texture::sampledView() const noexcept
{
    ImageView ret = _defaultView;
    ret._isDefaultView = true;
    ret._usage = ImageUsage::SHADER_SAMPLE;
    return ret;
}

ImageView Texture::getView() const noexcept {
    ImageView ret = _defaultView;
    ret._isDefaultView = false;
    return ret;
}

ImageView Texture::getView(const ImageUsage usage) const noexcept {
    ImageView ret = getView();
    ret._usage = usage;
    return ret;
}

ImageView Texture::getView(const TextureType targetType) const noexcept {
    ImageView ret = getView();
    ret._targetType = targetType;
    return ret;
}

ImageView Texture::getView(const vec2<U16> mipRange) const noexcept {
    ImageView ret = getView();
    ret._mipLevels = mipRange;
    return ret;
}

ImageView Texture::getView(const vec2<U16> mipRange, const vec2<U16> layerRange) const noexcept{
    ImageView ret = getView(mipRange);
    ret._layerRange = layerRange;
    return ret;
}

ImageView Texture::getView(const vec2<U16> mipRange, const vec2<U16> layerRange, const ImageUsage usage) const noexcept{
    ImageView ret = getView(mipRange, layerRange);
    ret._usage = usage;
    return ret;
}

ImageView Texture::getView(const TextureType targetType, const vec2<U16> mipRange) const noexcept {
    ImageView ret = getView(targetType);
    ret._mipLevels = mipRange;
    return ret;
}

ImageView Texture::getView(const TextureType targetType, const vec2<U16> mipRange, const vec2<U16> layerRange) const noexcept {
    ImageView ret = getView(targetType, mipRange);
    ret._layerRange = layerRange;
    return ret;
}

ImageView Texture::getView(const TextureType targetType, const vec2<U16> mipRange, const vec2<U16> layerRange, const ImageUsage usage) const noexcept {
    ImageView ret = getView(targetType, mipRange, layerRange);
    ret._usage = usage;
    return ret;
}

};
