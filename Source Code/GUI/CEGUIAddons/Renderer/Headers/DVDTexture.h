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
#pragma once
#ifndef _CEGUIDVDTexture_h_
#define _CEGUIDVDTexture_h_

#include "CEGUI/Base.h"
#include "CEGUI/Renderer.h"
#include "CEGUI/Texture.h"
#include "CEGUIRenderer.h"

#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4251)
#endif

namespace CEGUI
{

class DVDTexture final : public Texture
{
public:
#pragma region Texture Interface
    [[nodiscard]] const String& getName() const override;
    [[nodiscard]] const Sizef& getSize() const override;
    [[nodiscard]] const Sizef& getOriginalDataSize() const override;
    [[nodiscard]] const Vector2f& getTexelScaling() const override;
    [[nodiscard]] bool isPixelFormatSupported(PixelFormat fmt) const override;

    void loadFromFile(const String& filename, const String& resourceGroup) override;
    void loadFromMemory(const void* buffer, const Sizef& buffer_size, PixelFormat pixel_format) override;
    void blitFromMemory(const void* sourceData, const Rectf& area) override;
    void blitToMemory(void* targetData) override;
#pragma endregion

    /*!
    \brief
        set the Divide::Texture that this Texture is based on to the specified texture, with the specified size.
    */
    void setDVDTexture(Divide::Texture_ptr tex, const Sizef& size);

    /*!
    \brief
        Return the internal Divide::Texture pointer used by this Texture object.

    \return
        Divide::Texture pointer that this object is using.
    */
    [[nodiscard]] Divide::Texture_ptr getDVDTexture() const;

    /*!
    \brief
        set the size of the internal texture.

    \param sz
        size for the internal texture, in pixels.

    \note
        Depending upon the hardware capabilities, the actual final size of the
        texture may be larger than what is specified when calling this function.
        The texture will never be smaller than what you request here.  To
        discover the actual size, call getSize.

    \exception RendererException
        thrown if the hardware is unable to support a texture large enough to
        fulfill the requested size.

    \return
        Nothing.
    */
    void setTextureSize(const Sizef& sz);

private:
    // Friends (to allow construction and destruction)
    friend Texture& CEGUIRenderer::createTexture(const String&);
    friend Texture& CEGUIRenderer::createTexture(const String&, const String&, const String&);
    friend Texture& CEGUIRenderer::createTexture(const String&, const Sizef&);
    friend Texture& CEGUIRenderer::createTexture(const String&, const Divide::Texture_ptr&, const Sizef&);
    friend void CEGUIRenderer::destroyTexture(Texture&);
    friend void CEGUIRenderer::destroyTexture(const String&);

    //! Basic constructor.
    DVDTexture( CEGUIRenderer& owner, const String& name);
    //! Constructor that creates a Texture from an image file.
    DVDTexture( CEGUIRenderer& owner, const String& name, const String& filename, const String& resourceGroup);
    //! Constructor that creates a Texture with a given size.
    DVDTexture( CEGUIRenderer& owner, const String& name, const Sizef& size);
    //! Constructor that wraps an existing GL texture.
    DVDTexture( CEGUIRenderer& owner, const String& name, const Divide::Texture_ptr& tex, const Sizef& size);
    //! Destructor.
    virtual ~DVDTexture() = default;
    //! generate the DVD texture and set some initial options.
    void generateDVDTexture();
    //! updates cached scale value used to map pixels to texture co-ords.
    void updateCachedScaleValues();
    //! internal texture resize function (does not reset format or other fields)
    void setTextureSize_impl(const Sizef& sz, PixelFormat format);

    [[nodiscard]] size_t getBufferSize();

private:
    //! Size of the texture.
    Sizef _size{0,0};
    //! original size of pixel data loaded into texture
    Sizef _dataSize{0, 0};
    //! cached pixel to texel mapping scale values.
    Vector2f _texelScaling{0.f, 0.f};
    //! CEGUIRenderer that created and owns this DVDTexture
    CEGUIRenderer& _owner;
    //! The name given for this texture.
    const String _name;
    //! Texture format
    PixelFormat _format;
    //! Whether Texture format is a compressed format
    bool _isCompressed{false};
    //! The Divide texture used for storing this DVDTexture's data
    Divide::Texture_ptr _texture;
    //! A Divide sampler hash used for sampling from this texture in shaders
    size_t _samplerHash{0u};
};

} // End of  CEGUI namespace section

#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

#endif // end of guard _CEGUIDVDTexture_h_

#include "DVDTexture.inl"
