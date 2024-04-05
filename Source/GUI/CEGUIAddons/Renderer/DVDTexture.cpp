

/***********************************************************************
    created:    Sun Jan 11 2009
    author:     Paul D Turner
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2009 Paul D Turner & The CEGUI Development Team
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 ***************************************************************************/
#include "Headers/DVDTexture.h"
#include "CEGUI/Exceptions.h"
#include "CEGUI/System.h"
#include "CEGUI/ImageCodec.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

// Start of CEGUI namespace section
namespace CEGUI
{

DVDTexture::DVDTexture( CEGUIRenderer& owner, const String& name )
    : _owner(owner)
    , _name(name)
    , _format( PF_RGBA )
{
    generateDVDTexture();
}

DVDTexture::DVDTexture( CEGUIRenderer& owner, const String& name, const String& filename, const String& resourceGroup )
    : DVDTexture(owner, name)
{
    loadFromFile(filename, resourceGroup);
}

DVDTexture::DVDTexture( CEGUIRenderer& owner, const String& name, const Sizef& size )
    : DVDTexture(owner, name)
{
    setTextureSize(size);
}

DVDTexture::DVDTexture( CEGUIRenderer& owner, const String& name, const Divide::Texture_ptr& tex, const Sizef& size )
    : _size(size)
    , _texture(tex)
    , _dataSize(size)
    , _owner(owner)
    , _name(name)
    , _format(PF_RGBA)
{
    updateCachedScaleValues();
}

void DVDTexture::loadFromFile(const String& filename, const String& resourceGroup)
{
    // Note from PDT:
    // There is somewhat tight coupling here between DVDTexture and the
    // ImageCodec classes - we have intimate knowledge of how they are
    // implemented and that knowledge is relied upon in an unhealthy way; this
    // should be addressed at some stage.

    // load file to memory via resource provider
    RawDataContainer texFile;
    System::getSingleton().getResourceProvider()->loadRawDataContainer(filename, texFile, resourceGroup);

    // get and check existence of CEGUI::System (needed to access ImageCodec)
    System* sys = System::getSingletonPtr();
    Divide::DIVIDE_ASSERT(sys, "CEGUI::System object has not been created: unable to access ImageCodec.");

    Texture* res = sys->getImageCodec().load(texFile, this);

    // unload file data buffer
    System::getSingleton().getResourceProvider()->unloadRawDataContainer(texFile);

    Divide::DIVIDE_ASSERT(res, (sys->getImageCodec().getIdentifierString() + " failed to load image '" + filename + "'.").c_str());
}

void DVDTexture::loadFromMemory(const void* buffer, const Sizef& buffer_size, PixelFormat pixel_format)
{
    Divide::DIVIDE_ASSERT(isPixelFormatSupported(pixel_format), "Data was supplied in an unsupported pixel format.");

    setTextureSize_impl(buffer_size, pixel_format);

    // store size of original data we are loading
    _dataSize = buffer_size;
    updateCachedScaleValues();

    blitFromMemory(buffer, Rectf(Vector2f(0, 0), buffer_size));
}

void DVDTexture::setTextureSize(const Sizef& sz)
{
    setTextureSize_impl(sz, PF_RGBA);

    _dataSize = _size;
    updateCachedScaleValues();
}

void DVDTexture::setTextureSize_impl(const Sizef& sz, PixelFormat format)
{
    using namespace Divide;

    _isCompressed = false;
    _format = format;

    switch ( format )
    {
        case PF_RGB_DXT1:
        case PF_RGBA_DXT1:
        case PF_RGBA_DXT3:
        case PF_RGBA_DXT5:
            _isCompressed = true;
            break;
        case PF_PVRTC2:
        case PF_PVRTC4:
        {
            DIVIDE_UNEXPECTED_CALL_MSG( "DVDTexture::setTextureSize_impl: PVRTC textures not supported!" );
        } break;
    }

    static float maxSize = -1;

    _size = sz;

    // make sure size is within boundaries
    if (maxSize < 0.f)
    {
        maxSize = float(GFXDevice::GetDeviceInformation()._maxTextureSize);
    }

    Divide::DIVIDE_ASSERT(!( _size.d_width > maxSize || _size.d_height > maxSize), "DVDTexture:: size too big");

    _texture->createWithData( nullptr, 0u, vec2<U16>( _size.d_width, _size.d_height), {} );
}

void DVDTexture::blitFromMemory(const void* sourceData, const Rectf& area)
{
    using namespace Divide;

    DIVIDE_ASSERT(_format != PF_PVRTC2 && _format != PF_PVRTC4, "DVDTexture::blitFromMemory: PVRTC textures not supported!" );

    size_t image_size = 0u;
    if (_isCompressed)
    {
        size_t blocksize = 16;
        if ( _format == PF_RGB_DXT1 ||
             _format == PF_RGBA_DXT3 )
        {
            blocksize = 8;
        }

        image_size = size_t(std::ceil( area.getSize().d_width / 4 ) *
                            std::ceil( area.getSize().d_height / 4 ) *
                            blocksize);
    }
    else
    {
        U8 bpp = 4u;
        if ( _format == PF_RGB )
        {
            bpp = 3u;
        }
        else if ( _format == PF_RGBA_4444 || _format == PF_RGB_565 )
        {
            bpp = 2u;
        }

        image_size = size_t(area.getSize().d_width *
                            area.getSize().d_height) *
                            bpp;
    }

    const PixelAlignment pixelUnpackAlignment
    {
        ._alignment = 1u
    };

    vec3<U16> offset;
    offset.x = to_U16(area.left());
    offset.y = to_U16(area.top());
    offset.z = 0u;
    
    vec3<U16> dimensions;
    dimensions.width = to_U16(area.getWidth());
    dimensions.height = to_U16(area.getHeight());
    dimensions.depth = 1u;
    _texture->replaceData( (Byte*)sourceData, image_size, offset, dimensions, pixelUnpackAlignment );
}

void DVDTexture::blitToMemory(void* targetData) {
    const Divide::PixelAlignment pixelPackAlignment = {
        ._alignment = 1u
    };

    auto data = _texture->readData(0u, pixelPackAlignment);
    memcpy(targetData, data._data.data(), data._data.size());
}

void DVDTexture::generateDVDTexture()
{
    using namespace Divide;

    thread_local size_t TEXTURE_IDX = 0u;

    GFXDataFormat dataFormat = GFXDataFormat::UNSIGNED_BYTE;
    GFXImageFormat targetFormat = GFXImageFormat::RGBA;
    GFXImagePacking targetPacking = GFXImagePacking::NORMALIZED;

    switch ( _format )
    {
        case Texture::PixelFormat::PF_RGB:
        case Texture::PixelFormat::PF_RGB_565:
        {
            targetFormat = GFXImageFormat::RGB;
            if ( _format == Texture::PixelFormat::PF_RGB_565 )
            {
                targetPacking = GFXImagePacking::RGB_565;
            }
        } break;
        case Texture::PixelFormat::PF_RGBA:
        case Texture::PixelFormat::PF_RGBA_4444:
        {
            targetFormat = GFXImageFormat::RGBA;
            if ( _format == Texture::PixelFormat::PF_RGBA_4444 )
            {
                targetPacking = GFXImagePacking::RGBA_4444;
            }
        } break;
        case Texture::PixelFormat::PF_RGB_DXT1:
        {
            targetFormat = GFXImageFormat::DXT1_RGB;
        }break;
        case Texture::PixelFormat::PF_RGBA_DXT1:
        {
            targetFormat = GFXImageFormat::DXT1_RGBA;
        }break;
        case Texture::PixelFormat::PF_RGBA_DXT3:
        {
            targetFormat = GFXImageFormat::DXT3_RGBA;
        }break;
        case Texture::PixelFormat::PF_RGBA_DXT5:
        {
            targetFormat = GFXImageFormat::DXT5_RGBA;
        }break;
        case Texture::PixelFormat::PF_PVRTC2:
        case Texture::PixelFormat::PF_PVRTC4:
        {
            DIVIDE_UNEXPECTED_CALL_MSG("DVDTexture::generateDVDTexture: PVRTC textures not supported!");
        } break;
    }

    TextureDescriptor texDescriptor( TextureType::TEXTURE_2D,
                                     dataFormat,
                                     targetFormat,
                                     targetPacking );

    texDescriptor.allowRegionUpdates(true);
    texDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

    ResourceDescriptor resDescriptor( Util::StringFormat("CEGUI_texture_{}", TEXTURE_IDX++).c_str() );
    resDescriptor.propertyDescriptor( texDescriptor );
    resDescriptor.waitForReady( true );
    ResourceCache* parentCache = _owner.context().context().kernel().resourceCache();
    _texture = CreateResource<Divide::Texture>( parentCache, resDescriptor );

    _sampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
    _sampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
    _sampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
    _sampler._anisotropyLevel = 0u;
    _sampler._minFilter = TextureFilter::LINEAR;
    _sampler._magFilter = TextureFilter::LINEAR;
    _sampler._mipSampling = TextureMipSampling::NONE;
}

void DVDTexture::setDVDTexture( Divide::Texture_ptr tex, const Sizef& size)
{
    _texture = tex;
    _dataSize = _size = size;
    updateCachedScaleValues();
}

bool DVDTexture::isPixelFormatSupported(const PixelFormat fmt) const
{
    switch (fmt)
    {
        case PF_RGBA:
        case PF_RGB:
        case PF_RGBA_4444:
        case PF_RGB_565:
        case PF_RGB_DXT1:
        case PF_RGBA_DXT1:
        case PF_RGBA_DXT3:
        case PF_RGBA_DXT5:
            return true;
    }

    return false;
}

} // End of  CEGUI namespace section
