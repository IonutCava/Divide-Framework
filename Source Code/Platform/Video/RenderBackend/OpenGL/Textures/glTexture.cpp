#include "stdafx.h"

#include "config.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

#include "Headers/glTexture.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    FORCE_INLINE [[nodiscard]] U8 GetSizeFactor(const GFXDataFormat format) noexcept {
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

    FORCE_INLINE [[nodiscard]] U8 GetBitsPerPixel(const GFXDataFormat format, const GFXImageFormat baseFormat) noexcept {
        return GetSizeFactor(format) * NumChannels(baseFormat) * 8;
    }
};

glTexture::glTexture(GFXDevice& context,
                     const size_t descriptorHash,
                     const Str256& name,
                     const ResourcePath& resourceName,
                     const ResourcePath& resourceLocation,
                     const TextureDescriptor& texDescriptor,
                     ResourceCache& parentCache)

    : Texture(context, descriptorHash, name, resourceName, resourceLocation, texDescriptor, parentCache),
      glObject(glObjectType::TYPE_TEXTURE, context),
     _type(GL_NONE),
     _loadingData(_data)
{
}

glTexture::~glTexture()
{
    unload();
}

SamplerAddress glTexture::getGPUAddress(const size_t samplerHash) {
    assert(_data._textureType != TextureType::COUNT);

    if (ShaderProgram::s_UseBindlessTextures) {
        const GLuint sampler = GL_API::GetSamplerHandle(samplerHash);
        ScopedLock<Mutex> w_lock(_gpuAddressesLock);
        if (_cachedAddressForSampler._sampler != sampler) {
            if (sampler != 0u) {
                _cachedAddressForSampler._address = glGetTextureSamplerHandleARB(_data._textureHandle, sampler);
                _cachedAddressForSampler._sampler = sampler;
            } else {
                _cachedAddressForSampler._address = _baseTexAddress;
                _cachedAddressForSampler._sampler = 0u;
            }
        }
    }

    return _cachedAddressForSampler._address;
}

bool glTexture::unload() {
    if (_data._textureHandle > 0u) {
        glDeleteTextures(1, &_data._textureHandle);
        _data._textureHandle = 0u;
    }

    return true;
}

void glTexture::reserveStorage(const bool fromFile) {
    assert(
        !(_loadingData._textureType == TextureType::TEXTURE_CUBE_MAP && _width != _height) &&
        "glTexture::reserverStorage error: width and height for cube map texture do not match!");

    const GLenum glInternalFormat = GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
    const GLuint handle = _loadingData._textureHandle;
    const GLuint msaaSamples = static_cast<GLuint>(_descriptor.msaaSamples());

    switch (_loadingData._textureType) {
        case TextureType::TEXTURE_1D: {
            glTextureStorage1D(
                handle,
                mipCount(),
                glInternalFormat,
                _width);

        } break;
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_2D: {
            glTextureStorage2D(
                handle,
                mipCount(),
                glInternalFormat,
                _width,
                _height);
        } break;
        case TextureType::TEXTURE_2D_MS: {
            glTextureStorage2DMultisample(
                handle,
                msaaSamples,
                glInternalFormat,
                _width,
                _height,
                GL_TRUE);
        } break;
        case TextureType::TEXTURE_2D_ARRAY_MS: {
            glTextureStorage3DMultisample(
                handle,
                msaaSamples,
                glInternalFormat,
                _width,
                _height,
                _numLayers,
                GL_TRUE);
        } break;
        case TextureType::TEXTURE_3D:
        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_ARRAY: {
            U32 numFaces = 1;
            if (_loadingData._textureType == TextureType::TEXTURE_CUBE_ARRAY && !fromFile) {
                numFaces = 6;
            }
            glTextureStorage3D(
                handle,
                mipCount(),
                glInternalFormat,
                _width,
                _height,
                _numLayers * numFaces);
        } break;
        default: break;
    }
}

void glTexture::validateDescriptor() {

    // Select the proper colour space internal format
    if (_descriptor.baseFormat() == GFXImageFormat::RED ||
        _descriptor.baseFormat() == GFXImageFormat::RG ||
        _descriptor.baseFormat() == GFXImageFormat::DEPTH_COMPONENT)
    {
        // We only support 8 bit per pixel - 3 & 4 channel textures
        assert(!_descriptor.srgb());
    }

    // Cap upper mip count limit
    if (_width > 0 && _height > 0) {
        //http://www.opengl.org/registry/specs/ARB/texture_non_power_of_two.txt
        if (descriptor().mipMappingState() != TextureDescriptor::MipMappingState::OFF) {
            _mipCount = to_U16(std::floorf(std::log2f(std::fmaxf(to_F32(_width), to_F32(_height))))) + 1;
        } else {
            _mipCount = 1u;
        }
    }

    if (_descriptor.msaaSamples() == 0u) {
        if (_descriptor.texType() == TextureType::TEXTURE_2D_MS) {
            _descriptor.texType(TextureType::TEXTURE_2D);
        }
        else if (_descriptor.texType() == TextureType::TEXTURE_2D_ARRAY_MS) {
            _descriptor.texType(TextureType::TEXTURE_2D_ARRAY);
        }
    }
}

void glTexture::prepareTextureData(const U16 width, const U16 height) {
    _loadingData = _data;

    _width = width;
    _height = height;
    assert(_width > 0 && _height > 0 && "glTexture error: Invalid texture dimensions!");

    validateDescriptor();
    _loadingData._textureType = _descriptor.texType();

    _type = GLUtil::glTextureTypeTable[to_U32(_loadingData._textureType)];

    glCreateTextures(GLUtil::glTextureTypeTable[to_base(_descriptor.texType())], 1, &_loadingData._textureHandle);
    
    assert(_loadingData._textureHandle != 0 && "glTexture error: failed to generate new texture handle!");
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_TEXTURE, _loadingData._textureHandle, -1, resourceName().c_str());
    }
}

void glTexture::submitTextureData() {
    glTextureParameteri(_loadingData._textureHandle, GL_TEXTURE_BASE_LEVEL, _descriptor.mipBaseLevel());
    glTextureParameteri(_loadingData._textureHandle, GL_TEXTURE_MAX_LEVEL, _mipCount);

    if (_descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
        glGenerateTextureMipmap(_loadingData._textureHandle);
    }

    if (_data._textureHandle > 0u) {
        // Immutable storage requires us to create a new texture object 
        glDeleteTextures(1, &_data._textureHandle);
    }

    ScopedLock<Mutex> w_lock(_gpuAddressesLock);
    _data = _loadingData;

    if (ShaderProgram::s_UseBindlessTextures) {
        _baseTexAddress = glGetTextureHandleARB(_data._textureHandle);
    } else {
        _baseTexAddress = _data._textureHandle;
    }
    _cachedAddressForSampler._address = _baseTexAddress;
    _cachedAddressForSampler._sampler = 0u;
}

void glTexture::loadData(const Byte* data, const size_t dataSize, const vec2<U16>& dimensions) {
    prepareTextureData(dimensions.width, dimensions.height);

    // This should never be called for compressed textures
    assert(!IsCompressed(_descriptor.baseFormat()));

    reserveStorage(false);
    if (!IsMultisampledTexture(_loadingData._textureType)) {
        ImageTools::ImageData imgData = {};
        if (imgData.loadFromMemory(data, dataSize, _width, _height, 1, GetSizeFactor(_descriptor.dataType()) * NumChannels(_descriptor.baseFormat()))) {
            loadDataUncompressed(imgData);
            assert(_width > 0 && _height > 0 && "glTexture error: Invalid texture dimensions!");
        }
    }
    submitTextureData();
}

void glTexture::loadData(const ImageTools::ImageData& imageData) {

    prepareTextureData(imageData.dimensions(0u, 0u).width, imageData.dimensions(0u, 0u).height);
    reserveStorage(true);

    if (IsCompressed(_descriptor.baseFormat())) {
        if (_descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
            _descriptor.mipMappingState(TextureDescriptor::MipMappingState::MANUAL);
        }
        loadDataCompressed(imageData);
    } else {
        loadDataUncompressed(imageData);
    }

    submitTextureData();
}

void glTexture::loadDataCompressed(const ImageTools::ImageData& imageData) {

    const GLenum glFormat = GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
    const U32 numLayers = imageData.layerCount();

    GL_API::GetStateTracker()->setPixelPackUnpackAlignment();
    for (U32 l = 0u; l < numLayers; ++l) {
        const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
        const U8 numMips = layer.mipCount();

        for (U8 m = 0u; m < numMips; ++m) {
            const ImageTools::LayerData* mip = layer.getMip(m);
            switch (_loadingData._textureType) {
                case TextureType::TEXTURE_1D: {
                    assert(numLayers == 1);

                    glCompressedTextureSubImage1D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        mip->_dimensions.width,
                        glFormat,
                        static_cast<GLsizei>(mip->_size),
                        mip->data());
                } break;
                case TextureType::TEXTURE_CUBE_MAP:
                case TextureType::TEXTURE_2D: {
                    assert(numLayers == 1);

                    glCompressedTextureSubImage2D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        0,
                        mip->_dimensions.width,
                        mip->_dimensions.height,
                        glFormat,
                        static_cast<GLsizei>(mip->_size),
                        mip->data());
                } break;

                case TextureType::TEXTURE_3D:
                case TextureType::TEXTURE_2D_ARRAY:
                case TextureType::TEXTURE_2D_ARRAY_MS:
                case TextureType::TEXTURE_CUBE_ARRAY: {
                    glCompressedTextureSubImage3D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        0,
                        l,
                        mip->_dimensions.width,
                        mip->_dimensions.height,
                        mip->_dimensions.depth,
                        glFormat,
                        static_cast<GLsizei>(mip->_size),
                        mip->data());
                } break;
                default:
                    DIVIDE_UNEXPECTED_CALL();
                    break;
            }
        }
    }

    if (!Runtime::isMainThread()) {
        glFlush();
    }
}

void glTexture::loadDataUncompressed(const ImageTools::ImageData& imageData) const {
    const GLenum glFormat = GLUtil::glImageFormatTable[to_U32(_descriptor.baseFormat())];
    const GLenum glType = GLUtil::glDataFormat[to_U32(_descriptor.dataType())];
    const U32 numLayers = imageData.layerCount();
    const U8 numMips = imageData.mipCount();

    GL_API::GetStateTracker()->setPixelPackUnpackAlignment();

    for (U32 l = 0u; l < numLayers; ++l) {
        const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

        for (U8 m = 0u; m < numMips; ++m) {
            const ImageTools::LayerData* mip = layer.getMip(m);
            assert(mip->_size > 0u);

            switch (_loadingData._textureType) {
                case TextureType::TEXTURE_1D: {
                    assert(numLayers == 1);
                    glTextureSubImage1D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        mip->_dimensions.width,
                        glFormat,
                        glType,
                        mip->data()
                    );
                } break;
                case TextureType::TEXTURE_2D:
                case TextureType::TEXTURE_2D_MS: {
                    assert(numLayers == 1);
                    glTextureSubImage2D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        0,
                        mip->_dimensions.width,
                        mip->_dimensions.height,
                        glFormat,
                        glType,
                        IsMultisampledTexture(_loadingData._textureType) ? nullptr : mip->data()
                    );
                } break;
                case TextureType::TEXTURE_3D:
                case TextureType::TEXTURE_2D_ARRAY:
                case TextureType::TEXTURE_2D_ARRAY_MS:
                case TextureType::TEXTURE_CUBE_MAP:
                case TextureType::TEXTURE_CUBE_ARRAY: {
                    glTextureSubImage3D(
                        _loadingData._textureHandle,
                        m,
                        0,
                        0,
                        l,
                        mip->_dimensions.width,
                        mip->_dimensions.height,
                        mip->_dimensions.depth,
                        glFormat,
                        glType,
                        IsMultisampledTexture(_loadingData._textureType) ? nullptr : mip->data()
                    );
                } break;
                default: break;
            }
        }
    }
    if (!Runtime::isMainThread()) {
        glFlush();
    }
}

void glTexture::clearData(const UColour4& clearColour, const U8 level) const {
    clearDataInternal(clearColour, level, false, {}, {});
}

void glTexture::clearSubData(const UColour4& clearColour, const U8 level, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const {
    clearDataInternal(clearColour, level, true, rectToClear, depthRange);
}

void glTexture::clearDataInternal(const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const{
    FColour4 floatData;
    vec4<U16> shortData;
    vec4<U32> intData;

    const auto GetClearData = [clearColour, &floatData, &shortData, &intData](const GFXDataFormat format) {
        switch (format) {
            case GFXDataFormat::UNSIGNED_BYTE:
            case GFXDataFormat::SIGNED_BYTE:
            {
                return (bufferPtr)clearColour._v;
            }
            case GFXDataFormat::UNSIGNED_SHORT:
            case GFXDataFormat::SIGNED_SHORT:
            {
                shortData = { clearColour.r, clearColour.g, clearColour.b, clearColour.a };
                return (bufferPtr)shortData._v;
            }

            case GFXDataFormat::UNSIGNED_INT:
            case GFXDataFormat::SIGNED_INT:
            {
                intData = { clearColour.r, clearColour.g, clearColour.b, clearColour.a };
                return (bufferPtr)intData._v;
            }

            case GFXDataFormat::FLOAT_16:
            case GFXDataFormat::FLOAT_32:
            {
                floatData = Util::ToFloatColour(clearColour);
                return (bufferPtr)floatData._v;
            }
        }

        return (bufferPtr)nullptr;
    };

    const GLenum glFormat = GLUtil::glImageFormatTable[to_U32(_descriptor.baseFormat())];
    const GLenum glType = GLUtil::glDataFormat[to_U32(_descriptor.dataType())];
    if (clearRect) {
        glClearTexSubImage(_data._textureHandle, level, rectToClear.x, rectToClear.y, depthRange.min, rectToClear.z, rectToClear.w, depthRange.max, glFormat, glType, GetClearData(_descriptor.dataType()));
    } else {
        glClearTexImage(_data._textureHandle, level, glFormat, glType, GetClearData(_descriptor.dataType()));
    }
}

void glTexture::bindLayer(const U8 slot, const U8 level, const U8 layer, const bool layered, const Image::Flag rwFlag) {
    assert(_data._textureType != TextureType::COUNT);

    GLenum access = GL_NONE;
    switch (rwFlag) {
        case Image::Flag::READ       : access = GL_READ_ONLY; break;
        case Image::Flag::WRITE      : access = GL_WRITE_ONLY; break;
        case Image::Flag::READ_WRITE : access = GL_READ_WRITE; break;
        default: break;
    }
    assert(layer == 0u || !layered);
    if (access != GL_NONE) {
        const GLenum glInternalFormat = GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
        if (GL_API::GetStateTracker()->bindTextureImage(slot, _data._textureHandle, level, layered, layer, access, glInternalFormat) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
}


/*static*/ void glTexture::copy(const TextureData& source, const TextureData& destination, const CopyTexParams& params) {
    OPTICK_EVENT();
    assert(source._textureType != TextureType::COUNT && destination._textureType != TextureType::COUNT);
    const TextureType srcType = source._textureType;
    const TextureType dstType = destination._textureType;
    if (srcType != TextureType::COUNT && dstType != TextureType::COUNT) {
        U32 numFaces = 1;
        if (srcType == TextureType::TEXTURE_CUBE_MAP || srcType == TextureType::TEXTURE_CUBE_ARRAY) {
            numFaces = 6;
        }

        glCopyImageSubData(
            //Source
            source._textureHandle,
            GLUtil::glTextureTypeTable[to_U32(srcType)],
            params._sourceMipLevel,
            params._sourceCoords.x,
            params._sourceCoords.y,
            params._sourceCoords.z,
            //Destination
            destination._textureHandle,
            GLUtil::glTextureTypeTable[to_U32(dstType)],
            params._targetMipLevel,
            params._targetCoords.x,
            params._targetCoords.y,
            params._targetCoords.z,
            //Source Dim
            params._dimensions.x,
            params._dimensions.y,
            params._dimensions.z * numFaces);
    }
}

Texture::TextureReadbackData glTexture::readData(U16 mipLevel, const GFXDataFormat desiredFormat) const {
    if (IsCompressed(_descriptor.baseFormat())) {
        DIVIDE_ASSERT(false, "glTexture::readData: Compressed textures not supported!");
        TextureReadbackData data{};
        return MOV(data);
    }

    CLAMP(mipLevel, to_U16(0u), _mipCount);

    GLint texWidth = _width, texHeight = _height;
    glGetTextureLevelParameteriv(_data._textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_WIDTH, &texWidth);
    glGetTextureLevelParameteriv(_data._textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_HEIGHT, &texHeight);

    /** Always assume 4 channels as per GL spec:
      * If the selected texture image does not contain four components, the following mappings are applied.
      * Single-component textures are treated as RGBA buffers with red set to the single-component value, green set to 0, blue set to 0, and alpha set to 1.
      * Two-component textures are treated as RGBA buffers with red set to the value of component zero, alpha set to the value of component one, and green and blue set to 0.
      * Finally, three-component textures are treated as RGBA buffers with red set to component zero, green set to component one, blue set to component two, and alpha set to 1.
      **/
      
    const GFXDataFormat dataFormat = desiredFormat == GFXDataFormat::COUNT ? _descriptor.dataType() : desiredFormat;
    const U8 bpp = GetBitsPerPixel(desiredFormat, _descriptor.baseFormat());

    const GLsizei size = GLsizei{ texWidth } * texHeight * bpp;

    TextureReadbackData grabData{};
    grabData._data.reset(new Byte[size]);
    grabData._size = size;

    GL_API::GetStateTracker()->setPixelPackAlignment(1);
    glGetTextureImage(_data._textureHandle,
                      0,
                      GLUtil::glImageFormatTable[to_base(_descriptor.baseFormat())],
                      GLUtil::glDataFormat[to_base(dataFormat)],
                      size,
                      (bufferPtr)grabData._data.get());
    GL_API::GetStateTracker()->setPixelPackAlignment();

    return MOV(grabData);
}

};
