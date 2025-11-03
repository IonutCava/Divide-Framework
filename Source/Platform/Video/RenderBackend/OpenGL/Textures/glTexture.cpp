#include "Headers/glTexture.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace Divide {

glTexture::glTexture( PlatformContext& context, const ResourceDescriptor<Texture>& descriptor )

    : Texture(context, descriptor)
    , _type( gl46core::GL_NONE )
{
}

glTexture::~glTexture()
{
    unload();
}

bool glTexture::unload()
{
    if (_textureHandle > 0u)
    {
        if (GL_API::GetStateTracker().unbindTexture(_descriptor._texType, _textureHandle))
        {
            NOP();
        }

        gl46core::glDeleteTextures(1, &_textureHandle);
        _textureHandle = GL_NULL_HANDLE;
    }

    return Texture::unload();
}

bool glTexture::postLoad()
{
    DIVIDE_ASSERT(Runtime::isMainThread());

    if (_loadSync != nullptr)
    {
        gl46core::glWaitSync(_loadSync, gl46core::UnusedMask::GL_UNUSED_BIT, gl46core::GL_TIMEOUT_IGNORED);
        GL_API::DestroyFenceSync(_loadSync);
    }

    return Texture::postLoad();
}

void glTexture::reserveStorage()
{
    DIVIDE_ASSERT(!_hasStorage && "glTexture::reserveStorage error: double call detected!");

    const GLUtil::FormatAndDataType glInternalFormat = GLUtil::InternalFormatAndDataType( _descriptor._baseFormat, _descriptor._dataType, _descriptor._packing );
    const gl46core::GLuint msaaSamples = static_cast<gl46core::GLuint>(_descriptor._msaaSamples);
    const bool isCubeMap = IsCubeTexture( _descriptor._texType );
    DIVIDE_ASSERT(!(isCubeMap && _width != _height) && "glTexture::reserverStorage error: width and height for cube map texture do not match!");

    switch (_descriptor._texType )
    {
        case TextureType::TEXTURE_1D:
        {
            assert(_depth == 1u);
            gl46core::glTextureStorage1D( _loadingHandle,
                                          mipCount(),
                                          glInternalFormat._format,
                                          _width );

        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_2D:
        {
            DIVIDE_GPU_ASSERT(_descriptor._texType == TextureType::TEXTURE_1D_ARRAY || _depth == 1u);
            if (msaaSamples == 0u) {
                gl46core::glTextureStorage2D( _loadingHandle,
                                              mipCount(),
                                              glInternalFormat._format,
                                              _width,
                                              _descriptor._texType == TextureType::TEXTURE_1D_ARRAY ? _depth : _height );
            } else {
                gl46core::glTextureStorage2DMultisample( _loadingHandle,
                                                         msaaSamples,
                                                         glInternalFormat._format,
                                                         _width,
                                                         _descriptor._texType == TextureType::TEXTURE_1D_ARRAY ? _depth : _height,
                                                         gl46core::GL_TRUE );
            }
        } break;
        case TextureType::TEXTURE_3D:
        {
            if (msaaSamples == 0u)
            {
                gl46core::glTextureStorage3D( _loadingHandle,
                                              mipCount(),
                                              glInternalFormat._format,
                                              _width,
                                              _height,
                                              _depth );
            }
            else
            {
                gl46core::glTextureStorage3DMultisample( _loadingHandle,
                                                         msaaSamples,
                                                         glInternalFormat._format,
                                                         _width,
                                                         _height,
                                                         _depth,
                                                         gl46core::GL_TRUE );
            }
        } break;
        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_ARRAY:
        {
            const U32 layerCount = isCubeMap ? _depth * 6 : _depth;

            if (msaaSamples == 0u)
            {
                gl46core::glTextureStorage3D( _loadingHandle,
                                              mipCount(),
                                              glInternalFormat._format,
                                              _width,
                                              _height,
                                              layerCount );
            }
            else
            {
                gl46core::glTextureStorage3DMultisample( _loadingHandle,
                                                         msaaSamples,
                                                         glInternalFormat._format,
                                                         _width,
                                                         _height,
                                                         layerCount,
                                                         gl46core::GL_TRUE );
            }
        } break;
        default: DIVIDE_UNEXPECTED_CALL(); break;
    }

    _hasStorage = true;
}

void glTexture::prepareTextureData(const U16 width, const U16 height, const U16 depth, [[maybe_unused]] const bool emptyAllocation)
{
    Texture::prepareTextureData(width, height, depth, emptyAllocation);

    _type = GLUtil::internalTextureType( _descriptor._texType, _descriptor._msaaSamples );

    gl46core::glCreateTextures(_type, 1, &_loadingHandle);
    _hasStorage = false;

    assert(_loadingHandle != 0 && "glTexture error: failed to generate new texture handle!");
    if constexpr(Config::ENABLE_GPU_VALIDATION)
    {
        gl46core::glObjectLabel( gl46core::GL_TEXTURE, _loadingHandle, -1, resourceName().c_str());
    }

    reserveStorage();
}

void glTexture::submitTextureData()
{
    gl46core::glTextureParameteri(_loadingHandle, gl46core::GL_TEXTURE_BASE_LEVEL, _descriptor._mipBaseLevel );
    gl46core::glTextureParameteri( _loadingHandle, gl46core::GL_TEXTURE_MAX_LEVEL, mipCount());

    if ( _descriptor._mipMappingState == MipMappingState::AUTO)
    {
        gl46core::glGenerateTextureMipmap(_loadingHandle);
    }

    if (_textureHandle > 0u)
    {
        // Immutable storage requires us to create a new texture object 
        gl46core::glDeleteTextures(1, &_textureHandle);
    }

    _textureHandle = _loadingHandle;
    if (!Runtime::isMainThread())
    {
        if (_loadSync != nullptr)
        {
            GL_API::DestroyFenceSync(_loadSync);
        }
        _loadSync = GL_API::CreateFenceSync();
        gl46core::glFlush();
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
    const bool isCompressed = IsCompressed( _descriptor._baseFormat );

    const GLUtil::FormatAndDataType formatAndType = GLUtil::InternalFormatAndDataType( _descriptor._baseFormat, _descriptor._dataType, _descriptor._packing );

    DIVIDE_GPU_ASSERT( _descriptor._msaaSamples == 0u || data == nullptr);

    if ( _descriptor._packing == GFXImagePacking::RGBA_4444 )
    {
        constexpr PixelAlignment customAlignment{ ._alignment = 2u };
        GL_API::GetStateTracker().setPixelUnpackAlignment( customAlignment );
    }
    else
    {
        GL_API::GetStateTracker().setPixelUnpackAlignment( pixelUnpackAlignment );
    }

    switch ( _descriptor._texType )
    {
        case TextureType::TEXTURE_1D:
        {
            assert(offset.z == 0u);

            if (isCompressed)
            {
                gl46core::glCompressedTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, formatAndType._internalFormat, static_cast<gl46core::GLsizei>(size), data);
            }
            else
            {
                gl46core::glTextureSubImage1D(_loadingHandle, targetMip, offset.x, dimensions.width, formatAndType._internalFormat, formatAndType._dataType, data);
            }
        } break;
        case TextureType::TEXTURE_1D_ARRAY:
        case TextureType::TEXTURE_2D:
        {
            assert( offset.z == 0u );

            if (isCompressed)
            {
                gl46core::glCompressedTextureSubImage2D(_loadingHandle, targetMip, offset.x, offset.y, dimensions.width, dimensions.height, formatAndType._internalFormat, static_cast<gl46core::GLsizei>(size), data);
            }
            else
            {
                gl46core::glTextureSubImage2D(_loadingHandle, targetMip, offset.x, _descriptor._texType == TextureType::TEXTURE_1D_ARRAY ? offset.z : offset.y, dimensions.width, dimensions.height, formatAndType._internalFormat, formatAndType._dataType, data);
            }
        } break;
        case TextureType::TEXTURE_3D:
        case TextureType::TEXTURE_2D_ARRAY:
        case TextureType::TEXTURE_CUBE_MAP:
        case TextureType::TEXTURE_CUBE_ARRAY:
        {
            if (isCompressed)
            {
                gl46core::glCompressedTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, offset.z, dimensions.width, dimensions.height, dimensions.depth, formatAndType._internalFormat, static_cast<gl46core::GLsizei>(size), data);
            }
            else
            {
                gl46core::glTextureSubImage3D(_loadingHandle, targetMip, offset.x, offset.y, offset.z, dimensions.width, dimensions.height, dimensions.depth, formatAndType._internalFormat, formatAndType._dataType, data);
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
    uint4 intData;

    const auto GetClearData = [clearColour, &floatData, &shortData, &intData](const GFXDataFormat format)
    {
        switch (format)
        {
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

            default:
            case GFXDataFormat::COUNT:
            {
                DIVIDE_UNEXPECTED_CALL();
            } break;
        }

        return (bufferPtr)nullptr;
    };

    if ( mipLevel== U8_MAX )
    {
        assert(mipCount() > 0u);
        mipLevel = to_U8(mipCount() - 1u);
    }

    DIVIDE_GPU_ASSERT(!IsCompressed( _descriptor._baseFormat ), "glTexture::clearData: compressed textures are not supported!");

    const GLUtil::FormatAndDataType formatAndType = GLUtil::InternalFormatAndDataType( _descriptor._baseFormat, _descriptor._dataType, _descriptor._packing );

    if ( layerRange._offset == 0u && (layerRange._count == U16_MAX || layerRange._count == _depth))
    {
        gl46core::glClearTexImage( _textureHandle, mipLevel, formatAndType._internalFormat, formatAndType._dataType, GetClearData( _descriptor._dataType ) );
    }
    else
    {
        if ( layerRange._count >= _depth )
        {
            layerRange._count = _depth;
        }

        const bool isCubeMap = IsCubeTexture( _descriptor._texType );
        const U32 layerOffset = isCubeMap ? layerRange._offset * 6 : layerRange._offset;
        const U32 depth = isCubeMap ? layerRange._count * 6 : layerRange._count;
        const U16 mipWidth = _width >> mipLevel;
        const U16 mipHeight = _height >> mipLevel;

        gl46core::glClearTexSubImage( _textureHandle,
                                      mipLevel,
                                      0,
                                      _descriptor._texType == TextureType::TEXTURE_1D_ARRAY ? layerRange._offset : 0,
                                      layerOffset,
                                      mipWidth,
                                      _descriptor._texType == TextureType::TEXTURE_1D_ARRAY ? layerRange._count : mipHeight,
                                      depth,
                                      formatAndType._internalFormat,
                                      formatAndType._dataType,
                                      GetClearData( _descriptor._dataType) );
 
    }
}

/*static*/ void glTexture::Copy(const glTexture* source, const U8 sourceSamples, const glTexture* destination, const U8 destinationSamples, const CopyTexParams& params)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // We could handle this with a custom shader pass and temp render targets, so leaving the option i
    DIVIDE_GPU_ASSERT(sourceSamples == destinationSamples == 0u, "glTexture::copy Multisampled textures is not supported yet!");
    DIVIDE_GPU_ASSERT(source != nullptr && destination != nullptr, "glTexture::copy Invalid source and/or destination textures specified!");

    const TextureType srcType = source->_descriptor._texType;
    const TextureType dstType = destination->_descriptor._texType;
    assert(srcType != TextureType::COUNT && dstType != TextureType::COUNT);

    if (srcType != TextureType::COUNT && dstType != TextureType::COUNT)
    {
        U32 layerOffset = params._layerRange.offset;
        U32 layerCount = params._layerRange.count == U16_MAX ? source->_depth : params._layerRange.count;
        if (IsCubeTexture(srcType))
        {
            layerOffset *= 6;
            layerCount *= 6;
        }

        gl46core::glCopyImageSubData(
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

    grabData._bpp = Texture::GetBytesPerPixel( _descriptor._dataType, _descriptor._baseFormat, _descriptor._packing );
    grabData._numComponents = numChannels();
    grabData._sourceIsBGR = IsBGRTexture( _descriptor._baseFormat );

    DIVIDE_GPU_ASSERT(_depth == 1u && !IsCubeTexture( _descriptor._texType ), "glTexture:readData: unsupported image for readback. Support is very limited!");

    mipLevel = std::min(mipLevel, to_U8(mipCount() - 1u));
    if ( IsCompressed( _descriptor._baseFormat ) )
    {
        gl46core::GLint compressedSize = 0;
        gl46core::glGetTextureLevelParameteriv(_textureHandle, static_cast<gl46core::GLint>(mipLevel) , gl46core::GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &compressedSize);
        if ( compressedSize > 0 )
        {
            grabData._data.resize(compressedSize);
            gl46core::glGetCompressedTextureImage( _textureHandle, mipLevel, compressedSize, (bufferPtr)grabData._data.data() );
        }
    }
    else
    {
        grabData._numComponents = 4; //glGetTextureImage pads the data to RGBA
        {
            gl46core::GLint width = _width, height = _height;
            gl46core::glGetTextureLevelParameteriv(_textureHandle, static_cast<gl46core::GLint>(mipLevel), gl46core::GL_TEXTURE_WIDTH,  &width );
            gl46core::glGetTextureLevelParameteriv(_textureHandle, static_cast<gl46core::GLint>(mipLevel), gl46core::GL_TEXTURE_HEIGHT, &height );
            grabData._width = to_U16(width);
            grabData._height = to_U16(height);
        }

        const U8 storagePerComponent = grabData._bpp / numChannels();
        grabData._data.resize( to_size( grabData._width ) * grabData._height * _depth * storagePerComponent * 4 );

        GL_API::GetStateTracker().setPixelPackAlignment(pixelPackAlignment);

        const GLUtil::FormatAndDataType formatAndType = GLUtil::InternalFormatAndDataType( _descriptor._baseFormat, _descriptor._dataType, _descriptor._packing );

        gl46core::glGetTextureImage( _textureHandle,
                                     mipLevel,
                                     formatAndType._internalFormat,
                                     formatAndType._dataType,
                                     (gl46core::GLsizei)grabData._data.size(),
                                     grabData._data.data() );

        GL_API::GetStateTracker().setPixelPackAlignment({});
    }

    return grabData;
}

};
