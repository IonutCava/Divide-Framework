/***********************************************************************
    created:    Wed, 8th Feb 2012
    author:     Lukas E Meindl (based on code by Paul D Turner)
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2012 Paul D Turner & The CEGUI Development Team
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
#ifndef _CEGUIFBOTextureTarget_h_
#define _CEGUIFBOTextureTarget_h_

#include "RendererBase.h"
#include "CEGUI/TextureTarget.h"
#include "CEGUI/Rect.h"

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4250)
#endif

// Start of CEGUI namespace section
namespace CEGUI
{
class OpenGLTexture;
class DivideRenderer;

class FBOTextureTarget : public TextureTarget
{
public:
    FBOTextureTarget( DivideRenderer& owner);
    virtual ~FBOTextureTarget();

    // implement parts of RenderTarget interface
    void draw( const GeometryBuffer& buffer );
    void draw( const RenderQueue& queue );
    void setArea( const Rectf& area );
    const Rectf& getArea() const;
    void activate();
    void deactivate();
    void unprojectPoint( const GeometryBuffer& buff,
                         const Vector2f& p_in, Vector2f& p_out ) const;

    // implementation of TextureTarget interface
    void clear();
    void declareRenderSize(const Sizef& sz);
    // specialise functions from TextureTarget
    void grabTexture();
    void restoreTexture();

    // implementation of RenderTarget interface
    bool isImageryCache() const;
    // implementation of parts of TextureTarget interface
    Texture& getTexture() const;
    bool isRenderingInverted() const;

protected:
    //! default size of created texture objects
    static const float DEFAULT_SIZE;

    //! allocate and set up the texture used with the FBO.
    void initialiseRenderTexture();
    //! resize the texture
    void resizeRenderTexture();
    //! Checks for OpenGL framebuffer completeness
    void checkFramebufferStatus();
    //! helper that initialises the cached matrix
    void updateMatrix() const;

protected:
    //! DivideRenderer that created this object
    DivideRenderer& d_owner;
    //! holds defined area for the RenderTarget
    Rectf d_area{0.f, 0.f, 0.f, 0.f};
    //! tangent of the y FOV half-angle; used to calculate viewing distance.
    static const double d_yfov_tan;
    //! saved copy of projection matrix
    mutable glm::mat4 d_matrix;
    //! true if saved matrix is up to date
    mutable bool d_matrixValid{false};
    //! tracks viewing distance (this is set up at the same time as d_matrix)
    mutable double d_viewDistance{0};

    //! Frame buffer object.
    GLuint d_frameBuffer{0u};
    //! Frame buffer object that was bound before we bound this one
    GLuint d_previousFrameBuffer{0u};
    //! helper to generate unique texture names
    static String generateTextureName();
    //! static data used for creating texture names
    static uint s_textureNumber;

    //! helper to create CEGUI::Texture d_CEGUITexture;
    void createCEGUITexture();

    //! Associated OpenGL texture ID
    GLuint d_texture{0u};
    //! we use this to wrap d_texture so it can be used by the core CEGUI lib.
    OpenGLTexture* d_CEGUITexture{nullptr};

    int _prevViewport[4]{ -1, -1, -1, -1 };
};

} // End of  CEGUI namespace section

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif  // end of guard _CEGUIFBOTextureTarget_h_
