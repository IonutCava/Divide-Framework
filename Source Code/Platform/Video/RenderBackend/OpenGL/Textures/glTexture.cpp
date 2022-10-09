#include "stdafx.h"

#include "config.h"


#include "Headers/glTexture.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    FORCE_INLINE [[nodiscard]] U8 GetBitsPerPixel(const GFXDataFormat format, const GFXImageFormat baseFormat) noexcept {
        return Texture::GetSizeFactor(format) * NumChannels(baseFormat) * 8;
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
     _type(GL_NONE)
{
}

glTexture::~glTexture()
{
    unload();
}

bool glTexture::unload() {
    if (_textureHandle > 0u) {
        if (GL_API::GetStateTracker().unbindTexture(descriptor().texType(), _textureHandle)) { NOP();
        }
        glDeleteTextures(1, &_textureHandle);
        _textureHandle = GLUtil::k_invalidObjectID;
    }

    return Texture::unload();
}

void glTexture::postLoad() {
    DIVIDE_ASSERT(Runtime::isMainThread());
    if (_loadSync != nullptr) {
        glWaitSync(_loadSync, 0u, GL_TIMEOUT_IGNORED);
        GL_API::DestroyFenceSync(_loadSync);
    }

    Texture::postLoad();
}

void glTexture::reserveStorage() {
    assert(
        !(IsCubeTexture(descriptor().texType()) && _width != _height) &&
        "glTexture::reserverStorage error: width and height for cube map texture do not match!");
    assert(!_hasStorage && "glTexture::reserveStorage error: double call detected!");

    const GLenum glInternalFormat = GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
    const GLuint msaaSamples = static_cast<GLuint>(_descriptor.msaaSamples());

    switch (descriptor().texType()) {
        case TextureType::TEXTURE_1D: {
            assert(_numLayers == 1u);
            glTextureStorage1D(
                _loadingHandle,
                mipCount(),
                glInternalFormat,
                _width);

        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_2D: {
            assert(descriptor().texType() == TextureType::TEXTURE_1D_ARRAY || _numLayers == 1u);
            if (msaaSamples == 0u) {
                glTextureStorage2D(
                    _loadingHandle,
                    mipCount(),
                    glInternalFormat,
                    _width,
                    descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? _numLayers : _height);
            } else {
                glTextureStorage2DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat,
                    _width,
                    descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? _numLayers : _height,
                    GL_TRUE);
            }
        } break;
        case TextureType::TEXTURE_3D: {
            DIVIDE_ASSERT(_numLayers == 1u);

            if (msaaSamples == 0u) {
                glTextureStorage3D(
                    _loadingHandle,
                    mipCount(),
                    glInternalFormat,
                    _width,
                    _height,
                    _depth);
            } else {
                glTextureStorage3DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat,
                    _width,
                    _height,
                    _depth,
                    GL_TRUE);
            }
        } break;

        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_ARRAY: {
            const U32 layerCount = descriptor().texType() == TextureType::TEXTURE_CUBE_ARRAY ? _numLayers * 6 : _numLayers;
            if (msaaSamples == 0u) {
                glTextureStorage3D(
                    _loadingHandle,
                    mipCount(),
                    glInternalFormat,
                    _width,
                    _height,
                    layerCount);
            } else {
                glTextureStorage3DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat,
                    _width,
                    _height,
                    layerCount,
                    GL_TRUE);
            }
        } break;
        default: break;
    }

    _hasStorage = true;
}

void glTexture::prepareTextureData(const U16 width, const U16 height, const U16 depth, [[maybe_unused]] const bool emptyAllocation) {
    Texture::prepareTextureData(width, height, depth, emptyAllocation);

    _type = GLUtil::internalTextureType(_descriptor.texType(), _descriptor.msaaSamples());

    glCreateTextures(_type, 1, &_loadingHandle);
    _hasStorage = false;
    assert(_loadingHandle != 0 && "glTexture error: failed to generate new texture handle!");
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_TEXTURE, _loadingHandle, -1, resourceName().c_str());
    }

    reserveStorage();
}

void glTexture::submitTextureData() {
    glTextureParameteri(_loadingHandle, GL_TEXTURE_BASE_LEVEL, _descriptor.mipBaseLevel());
    glTextureParameteri(_loadingHandle, GL_TEXTURE_MAX_LEVEL, mipCount());

    if (_descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO) {
        glGenerateTextureMipmap(_loadingHandle);
    }

    if (_textureHandle > 0u) {
        // Immutable storage requires us to create a new texture object 
        glDeleteTextures(1, &_textureHandle);
    }

    _textureHandle = _loadingHandle;
    if (!Runtime::isMainThread()) {
        if (_loadSync != nullptr) {
            GL_API::DestroyFenceSync(_loadSync);
        }
        _loadSync = GL_API::CreateFenceSync();
        glFlush();
    }
}

void glTexture::loadDataInternal(const ImageTools::ImageData& imageData) {
    const bool isCompressed = IsCompressed(_descriptor.baseFormat());

    const GLenum glFormat = isCompressed ? GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized())
                                         : GLUtil::glImageFormatTable[to_U32(_descriptor.baseFormat())];

    const U32 numLayers = imageData.layerCount();
    const U8 numMips = imageData.mipCount();

    GL_API::GetStateTracker().setPixelPackUnpackAlignment();
    for (U32 l = 0u; l < numLayers; ++l) {
        const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];

        for (U8 m = 0u; m < numMips; ++m) {
            const ImageTools::LayerData* mip = layer.getMip(m);
            assert(mip->_size > 0u);

            switch (descriptor().texType()) {
                case TextureType::TEXTURE_1D: {
                    assert(numLayers == 1);
                    if (isCompressed) {
                        glCompressedTextureSubImage1D(
                            _loadingHandle,
                            m,
                            0,
                            mip->_dimensions.width,
                            glFormat,
                            static_cast<GLsizei>(mip->_size),
                            mip->data());
                    } else {
                        glTextureSubImage1D(
                            _loadingHandle,
                            m,
                            0,
                            mip->_dimensions.width,
                            glFormat,
                            GLUtil::glDataFormat[to_U32(_descriptor.dataType())],
                            mip->data());
                    }
                } break;
                case TextureType::TEXTURE_1D_ARRAY:
                case TextureType::TEXTURE_2D:{
                    assert(numLayers == 1);
                    if (isCompressed) {
                        glCompressedTextureSubImage2D(
                            _loadingHandle,
                            m,
                            0,
                            0,
                            mip->_dimensions.width,
                            mip->_dimensions.height,
                            glFormat,
                            static_cast<GLsizei>(mip->_size),
                            mip->data());
                    } else {
                        glTextureSubImage2D(
                            _loadingHandle,
                            m,
                            0,
                            descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? l : 0,
                            mip->_dimensions.width,
                            mip->_dimensions.height,
                            glFormat,
                            GLUtil::glDataFormat[to_U32(_descriptor.dataType())],
                            _descriptor.msaaSamples() > 0u ? nullptr : mip->data());
                    }
                } break;
                case TextureType::TEXTURE_3D:
                case TextureType::TEXTURE_2D_ARRAY:
                case TextureType::TEXTURE_CUBE_MAP:
                case TextureType::TEXTURE_CUBE_ARRAY: {
                    if (isCompressed) {
                        glCompressedTextureSubImage3D(
                            _loadingHandle,
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
                    } else {
                        glTextureSubImage3D(
                            _loadingHandle,
                            m,
                            0,
                            0,
                            l,
                            mip->_dimensions.width,
                            mip->_dimensions.height,
                            mip->_dimensions.depth,
                            glFormat,
                            GLUtil::glDataFormat[to_U32(_descriptor.dataType())],
                            _descriptor.msaaSamples() > 0u ? nullptr : mip->data());
                    }
                } break;
                default: break;
            }
        }
    }
}

void glTexture::clearData(const UColour4& clearColour, const U8 level) const {
    clearDataInternal(clearColour, level, false, {}, {});
}

void glTexture::clearSubData(const UColour4& clearColour, const U8 level, const vec4<I32>& rectToClear, const vec2<I32> depthRange) const {
    clearDataInternal(clearColour, level, true, rectToClear, depthRange);
}

void glTexture::clearDataInternal(const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, const vec2<I32> depthRange) const{
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
        glClearTexSubImage(_textureHandle, level, rectToClear.x, rectToClear.y, depthRange.min, rectToClear.z, rectToClear.w, depthRange.max, glFormat, glType, GetClearData(_descriptor.dataType()));
    } else {
        glClearTexImage(_textureHandle, level, glFormat, glType, GetClearData(_descriptor.dataType()));
    }
}

/*static*/ void glTexture::copy(const glTexture* source, const U8 sourceSamples, const glTexture* destination, const U8 destinationSamples, const CopyTexParams& params) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // We could handle this with a custom shader pass and temp render targets, so leaving the option i
    DIVIDE_ASSERT(sourceSamples == destinationSamples == 0u, "glTexcture::copy Multisampled textures is not supported yet!");
    DIVIDE_ASSERT(source != nullptr && destination != nullptr, "glTexture::copy Invalid source and/or destination textures specified!");

    const TextureType srcType = source->descriptor().texType();
    const TextureType dstType = destination->descriptor().texType();
    assert(srcType != TextureType::COUNT && dstType != TextureType::COUNT);

    if (srcType != TextureType::COUNT && dstType != TextureType::COUNT) {
        U32 numFaces = 1;
        if (IsCubeTexture(srcType)) {
            numFaces = 6;
        }

        glCopyImageSubData(
            //Source
            source->_textureHandle,
            GLUtil::internalTextureType(srcType, sourceSamples),
            params._sourceMipLevel,
            params._sourceCoords.x,
            params._sourceCoords.y,
            params._sourceCoords.z,
            //Destination
            destination->_textureHandle,
            GLUtil::internalTextureType(dstType, destinationSamples),
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

    CLAMP(mipLevel, to_U16(0u), mipCount());

    GLint texWidth = _width, texHeight = _height;
    glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_WIDTH, &texWidth);
    glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_HEIGHT, &texHeight);

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

    GL_API::GetStateTracker().setPixelPackAlignment(1);
    glGetTextureImage(_textureHandle,
                      0,
                      GLUtil::glImageFormatTable[to_base(_descriptor.baseFormat())],
                      GLUtil::glDataFormat[to_base(dataFormat)],
                      size,
                      (bufferPtr)grabData._data.get());
    GL_API::GetStateTracker().setPixelPackAlignment();

    return MOV(grabData);
}

};
