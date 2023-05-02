#include "stdafx.h"

#include "config.h"


#include "Headers/glTexture.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

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
        _textureHandle = GL_NULL_HANDLE;
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

    const GLUtil::FormatAndDataType glInternalFormat = GLUtil::InternalFormatAndDataType(_descriptor.baseFormat(), _descriptor.dataType(), _descriptor.packing());
    const GLuint msaaSamples = static_cast<GLuint>(_descriptor.msaaSamples());
    const bool isCubeMap = IsCubeTexture(_descriptor.texType());
    DIVIDE_ASSERT(!(isCubeMap && _width != _height) && "glTexture::reserverStorage error: width and height for cube map texture do not match!");

    switch (descriptor().texType()) {
        case TextureType::TEXTURE_1D: {
            assert(_depth == 1u);
            glTextureStorage1D(
                _loadingHandle,
                mipCount(),
                glInternalFormat._format,
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
                    glInternalFormat._format,
                    _width,
                    descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? _depth : _height);
            } else {
                glTextureStorage2DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat._format,
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
                    glInternalFormat._format,
                    _width,
                    _height,
                    _depth);
            } else {
                glTextureStorage3DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat._format,
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
                    glInternalFormat._format,
                    _width,
                    _height,
                    layerCount);
            } else {
                glTextureStorage3DMultisample(
                    _loadingHandle,
                    msaaSamples,
                    glInternalFormat._format,
                    _width,
                    _height,
                    layerCount,
                    GL_TRUE);
            }
        } break;
        default: DIVIDE_UNEXPECTED_CALL(); break;
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

    Texture::submitTextureData();
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

    const bool isCompressed = IsCompressed(_descriptor.baseFormat());

    const GLenum format = GLUtil::ImageFormat(_descriptor.baseFormat(), _descriptor.packing());
    const GLenum dataType = GLUtil::InternalDataType(_descriptor.dataType(), _descriptor.packing());

    DIVIDE_ASSERT( _descriptor.msaaSamples() == 0u || data == nullptr);

    if ( _descriptor.packing() == GFXImagePacking::RGBA_4444 )
    {
        constexpr PixelAlignment customAlignment{ ._alignment = 2u };
        GL_API::GetStateTracker().setPixelUnpackAlignment( customAlignment );
    }
    else
    {
        GL_API::GetStateTracker().setPixelUnpackAlignment( pixelUnpackAlignment );
    }

    switch (descriptor().texType()) {
        case TextureType::TEXTURE_1D: {
            assert(offset.z == 0u);

            if (isCompressed)
            {
                glCompressedTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, format, static_cast<GLsizei>(size), data);
            }
            else
            {
                glTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, format, dataType, data);
            }
        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_2D:{
            assert( offset.z == 0u );

            if (isCompressed)
            {
                glCompressedTextureSubImage2D(_loadingHandle, targetMip, offset.x, offset.y, dimensions.width, dimensions.height, format, static_cast<GLsizei>(size), data);
            }
            else
            {
                glTextureSubImage2D(_loadingHandle, targetMip, offset.x, descriptor().texType() == TextureType::TEXTURE_1D_ARRAY ? offset.z : offset.y, dimensions.width, dimensions.height, format, dataType, data);
            }
        } break;
        case TextureType::TEXTURE_3D:
        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_CUBE_ARRAY:
        {

            if (isCompressed)
            {
                glCompressedTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, offset.z, dimensions.width, dimensions.height, dimensions.depth, format, static_cast<GLsizei>(size), data);
            }
            else
            {
                glTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, offset.z, dimensions.width, dimensions.height, dimensions.depth, format, dataType, data);
            }
        } break;
        default: break;
    }

    GL_API::GetStateTracker().setPixelUnpackAlignment({});
}

void glTexture::clearData( const UColour4& clearColour, SubRange layerRange, U8 mipLevel ) const
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

    const GLenum glFormat = GLUtil::ImageFormat(_descriptor.baseFormat(), _descriptor.packing());
    const GLenum dataType = GLUtil::InternalDataType( _descriptor.dataType(), _descriptor.packing() );

    if ( layerRange._offset == 0u && (layerRange._count == U16_MAX || layerRange._count == _depth))
    {
        glClearTexImage( _textureHandle, mipLevel, glFormat, dataType, GetClearData( _descriptor.dataType() ) );
    }
    else
    {
        if ( layerRange._count >= _depth )
        {
            layerRange._count = _depth;
        }

        const bool isCubeMap = IsCubeTexture( _descriptor.texType() );
        const U32 layerOffset = isCubeMap ? layerRange._offset * 6 : layerRange._offset;
        const U32 depth = isCubeMap ? layerRange._count * 6 : layerRange._count;
        const U16 mipWidth = _width >> mipLevel;
        const U16 mipHeight = _height >> mipLevel;

        glClearTexSubImage(_textureHandle,
                            mipLevel,
                            0,
                            _descriptor.texType() == TextureType::TEXTURE_1D_ARRAY ? layerRange._offset : 0,
                            layerOffset,
                            mipWidth,
                            _descriptor.texType() == TextureType::TEXTURE_1D_ARRAY ? layerRange._count : mipHeight,
                            depth,
                            glFormat,
                            dataType,
                            GetClearData( _descriptor.dataType() ) );
 
    }
}

/*static*/ void glTexture::Copy(const glTexture* source, const U8 sourceSamples, const glTexture* destination, const U8 destinationSamples, const CopyTexParams& params)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // We could handle this with a custom shader pass and temp render targets, so leaving the option i
    DIVIDE_ASSERT(sourceSamples == destinationSamples == 0u, "glTexture::copy Multisampled textures is not supported yet!");
    DIVIDE_ASSERT(source != nullptr && destination != nullptr, "glTexture::copy Invalid source and/or destination textures specified!");

    const TextureType srcType = source->descriptor().texType();
    const TextureType dstType = destination->descriptor().texType();
    assert(srcType != TextureType::COUNT && dstType != TextureType::COUNT);

    if (srcType != TextureType::COUNT && dstType != TextureType::COUNT) {
        U32 layerOffset = params._layerRange.offset;
        U32 layerCount = params._layerRange.count == U16_MAX ? source->_depth : params._layerRange.count;
        if (IsCubeTexture(srcType))
        {
            layerOffset *= 6;
            layerCount *= 6;
        }

        glCopyImageSubData(
            //Source
            source->textureHandle(),
            GLUtil::internalTextureType(srcType, sourceSamples),
            params._sourceMipLevel,
            params._sourceCoords.x,
            params._sourceCoords.y,
            layerOffset,
            //Destination
            destination->textureHandle(),
            GLUtil::internalTextureType(dstType, destinationSamples),
            params._targetMipLevel,
            params._targetCoords.x,
            params._targetCoords.y,
            layerOffset,
            //Source Dim
            params._dimensions.x,
            params._dimensions.y,
            layerCount);
    }
}

ImageReadbackData glTexture::readData(U8 mipLevel, const PixelAlignment& pixelPackAlignment) const
{
    ImageReadbackData grabData{};

    grabData._bpp = Texture::GetBytesPerPixel( _descriptor.dataType(), _descriptor.baseFormat(), _descriptor.packing() );
    grabData._numComponents = numChannels();
    grabData._sourceIsBGR = IsBGRTexture(_descriptor.baseFormat());

    DIVIDE_ASSERT(_depth == 1u && !IsCubeTexture(_descriptor.texType()), "glTexture:readData: unsupported image for readback. Support is very limited!");

    mipLevel = std::min(mipLevel, to_U8(mipCount() - 1u));
    if ( IsCompressed( _descriptor.baseFormat() ) )
    {
        GLint compressedSize = 0u;
        glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel) , GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &compressedSize);
        if ( compressedSize > 0u )
        {
            grabData._data.resize(compressedSize);
            glGetCompressedTextureImage( _textureHandle, mipLevel, compressedSize, (bufferPtr)grabData._data.data() );
        }
    }
    else
    {
        grabData._numComponents = 4; //glGetTextureImage pads the data to RGBA
        {
            GLint width = _width, height = _height;
            glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_WIDTH,  &width );
            glGetTextureLevelParameteriv(_textureHandle, static_cast<GLint>(mipLevel), GL_TEXTURE_HEIGHT, &height );
            grabData._width = to_U16(width);
            grabData._height = to_U16(height);
        }

        const U8 storagePerComponent = grabData._bpp / numChannels();
        grabData._data.resize( to_size( grabData._width ) * grabData._height * _depth * storagePerComponent * 4 );

        GL_API::GetStateTracker().setPixelPackAlignment(pixelPackAlignment);

        glGetTextureImage(_textureHandle,
                          mipLevel,
                          GLUtil::ImageFormat( _descriptor.baseFormat(), _descriptor.packing() ),
                          GLUtil::InternalDataType( _descriptor.dataType(), _descriptor.packing() ),
                          (GLsizei)grabData._data.size(),
                          grabData._data.data());

        GL_API::GetStateTracker().setPixelPackAlignment({});
    }

    return grabData;
}

};
