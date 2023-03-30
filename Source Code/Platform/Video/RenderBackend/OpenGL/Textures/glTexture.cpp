#include "stdafx.h"

#include "config.h"


#include "Headers/glTexture.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

namespace {
    FORCE_INLINE [[nodiscard]] U8 GetBytesPerPixel(const GFXDataFormat format, const GFXImageFormat baseFormat) noexcept {
        return Texture::GetSizeFactor(format) * NumChannels(baseFormat);
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
    DIVIDE_ASSERT(!_hasStorage && "glTexture::reserveStorage error: double call detected!");

    const GLenum glInternalFormat = GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized());
    const GLuint msaaSamples = static_cast<GLuint>(_descriptor.msaaSamples());
    const bool isCubeMap = IsCubeTexture(_descriptor.texType());
    DIVIDE_ASSERT(!(isCubeMap && _width != _height) && "glTexture::reserverStorage error: width and height for cube map texture do not match!");

    switch (descriptor().texType()) {
        case TextureType::TEXTURE_1D: {
            assert(_depth == 1u);
            glTextureStorage1D(
                _loadingHandle,
                mipCount(),
                glInternalFormat,
                _width);

        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_2D: {
            assert(descriptor().texType() == TextureType::TEXTURE_1D_ARRAY || _depth == 1u);
            if (msaaSamples == 0u) {
                glTextureStorage2D(
                    _loadingHandle,
                    mipCount(),
                    glInternalFormat,
                    _width,
                    descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? _depth : _height);
            } else {
                glTextureStorage2DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat,
                    _width,
                    descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? _depth : _height,
                    GL_TRUE);
            }
        } break;
        case TextureType::TEXTURE_3D: {
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
            const U32 layerCount = isCubeMap ? _depth * 6 : _depth;

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
    if constexpr(Config::ENABLE_GPU_VALIDATION) {
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

void glTexture::loadDataInternal(const ImageTools::ImageData& imageData, const vec3<U16>& offset, const PixelAlignment& pixelUnpackAlignment )
{
    const U32 numLayers = imageData.layerCount();
    const U8 numMips = imageData.mipCount();

    for ( U32 l = 0u; l < numLayers; ++l )
    {
        const ImageTools::ImageLayer& layer = imageData.imageLayers()[l];
        for ( U8 m = 0u; m < numMips; ++m )
        {
            const ImageTools::LayerData* mip = layer.getMip( m );
            assert( mip->_size > 0u );

            loadDataInternal((Byte*)mip->data(), mip->_size, m, vec3<U16>{offset.x, offset.y, offset.z + l}, mip->_dimensions, pixelUnpackAlignment);
        }
    }
}

void glTexture::loadDataInternal( const Byte* data, const size_t size, const U8 targetMip, const vec3<U16>& offset, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
{
    GL_API::GetStateTracker().setPixelUnpackAlignment(pixelUnpackAlignment);

    const bool isCompressed = IsCompressed(_descriptor.baseFormat());

    const GLenum glFormat = isCompressed ? GLUtil::internalFormat(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.srgb(), _descriptor.normalized())
                                         : GLUtil::glImageFormatTable[to_U32(_descriptor.baseFormat())];

    const GLenum dataFormat = GLUtil::glDataFormat[to_U32( _descriptor.dataType() )];

    DIVIDE_ASSERT( _descriptor.msaaSamples() == 0u || data == nullptr);

    switch (descriptor().texType()) {
        case TextureType::TEXTURE_1D: {
            assert(offset.z == 0u);

            if (isCompressed)
            {
                glCompressedTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, glFormat, static_cast<GLsizei>(size), data);
            }
            else
            {
                glTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, glFormat, dataFormat, data);
            }
        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_2D:{
            assert( offset.z == 0u );

            if (isCompressed)
            {
                glCompressedTextureSubImage2D(_loadingHandle, targetMip, offset.x, offset.y, dimensions.width, dimensions.height, glFormat, static_cast<GLsizei>(size), data);
            }
            else
            {
                glTextureSubImage2D(_loadingHandle, targetMip, offset.x, descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? offset.z : offset.y, dimensions.width, dimensions.height, glFormat, dataFormat, data);
            }
        } break;
        case TextureType::TEXTURE_3D:
        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_CUBE_ARRAY:
        {

            if (isCompressed)
            {
                glCompressedTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, offset.z, dimensions.width, dimensions.height, dimensions.depth, glFormat, static_cast<GLsizei>(size), data);
            }
            else
            {
                const bool isCubeMap = IsCubeTexture( _descriptor.texType() );
                const U32 layerOffset = isCubeMap ? offset.z * 6 : offset.z;
                const U32 depth = isCubeMap ? dimensions.depth * 6 : dimensions.depth;
                glTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, layerOffset, dimensions.width, dimensions.height, depth, glFormat, dataFormat, data);
            }
        } break;
        default: break;
    }

    GL_API::GetStateTracker().setPixelUnpackAlignment({});
}

void glTexture::clearData( const UColour4& clearColour, vec2<U16> layerRange, U8 mipLevel ) const
{
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

    if ( mipLevel== U8_MAX )
    {
        assert(mipCount() > 0u);
        mipLevel = to_U8(mipCount() - 1u);
    }

    DIVIDE_ASSERT(!IsCompressed( _descriptor.baseFormat() ), "glTexture::clearData: compressed textures are not supported!");

    const GLenum glFormat = GLUtil::glImageFormatTable[to_U32(_descriptor.baseFormat())];
    const GLenum glType = GLUtil::glDataFormat[to_U32(_descriptor.dataType())];

    if ( layerRange.offset == 0u && (layerRange.count == U16_MAX || layerRange.count == _depth))
    {
        glClearTexImage( _textureHandle, mipLevel, glFormat, glType, GetClearData( _descriptor.dataType() ) );
    }
    else
    {
        const bool isCubeMap = IsCubeTexture( _descriptor.texType() );
        const U32 layerOffset = isCubeMap ? layerRange.offset * 6 : layerRange.offset;
        const U32 depth = isCubeMap ? layerRange.count * 6 : layerRange.count;
        const U16 mipWidth = _width >> mipLevel;
        const U16 mipHeight = _height >> mipLevel;

        glClearTexSubImage(_textureHandle,
                            mipLevel,
                            0,
                            _descriptor.texType() == TextureType::TEXTURE_1D_ARRAY ? layerRange.offset : 0,
                            layerOffset,
                            mipWidth,
                            _descriptor.texType() == TextureType::TEXTURE_1D_ARRAY ? layerRange.count : mipHeight,
                            depth,
                            glFormat,
                            glType,
                            GetClearData( _descriptor.dataType() ) );
 
    }
}

/*static*/ void glTexture::Copy(const glTexture* source, const U8 sourceSamples, const glTexture* destination, const U8 destinationSamples, const CopyTexParams& params)
{
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
            source->textureHandle(),
            GLUtil::internalTextureType(srcType, sourceSamples),
            params._sourceMipLevel,
            params._sourceCoords.x,
            params._sourceCoords.y,
            params._sourceCoords.z,
            //Destination
            destination->textureHandle(),
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

ImageReadbackData glTexture::readData(U8 mipLevel, const PixelAlignment& pixelPackAlignment) const {
    if ( mipLevel == U8_MAX )
    {
        mipLevel = to_U8(mipCount() - 1u);
    }

    GLint texWidth = _width, texHeight = _height;
    glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_WIDTH, &texWidth);
    glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_HEIGHT, &texHeight);

    /** Always assume 4 channels as per GL spec:
      * If the selected texture image does not contain four components, the following mappings are applied.
      * Single-component textures are treated as RGBA buffers with red set to the single-component value, green set to 0, blue set to 0, and alpha set to 1.
      * Two-component textures are treated as RGBA buffers with red set to the value of component zero, alpha set to the value of component one, and green and blue set to 0.
      * Finally, three-component textures are treated as RGBA buffers with red set to component zero, green set to component one, blue set to component two, and alpha set to 1.
    **/

    const auto desiredDataFormat = _descriptor.dataType();
    const auto desiredImageFormat = _descriptor.baseFormat();

    const U8 bpp = GetBytesPerPixel(desiredDataFormat, desiredImageFormat);
    DIVIDE_ASSERT(bpp == 4 && desiredImageFormat == GFXImageFormat::RGBA && !IsCubeTexture(_descriptor.texType()), "glTexture:readData: unsupported image for readback. Support is very limited!");

    const GLsizei size = (GLsizei{ texWidth } * texHeight * _depth * bpp);

    ImageReadbackData grabData{};
    grabData._data.reset(new Byte[size]);
    grabData._size = size;

    if ( IsCompressed( _descriptor.baseFormat() ) )
    {
        glGetCompressedTextureImage( _textureHandle, mipLevel, size, (bufferPtr)grabData._data.get());
    }
    else
    {
        GL_API::GetStateTracker().setPixelPackAlignment(pixelPackAlignment);

        glGetTextureImage(_textureHandle,
                          mipLevel,
                          GLUtil::glImageFormatTable[to_base( desiredImageFormat )],
                          GLUtil::glDataFormat[to_base( desiredDataFormat )],
                          size,
                          (bufferPtr)grabData._data.get());

        GL_API::GetStateTracker().setPixelPackAlignment({});
    }

    return MOV(grabData);
}

};
