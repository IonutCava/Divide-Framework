#include "stdafx.h"

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
#include "GL.h"
#include "GL3FBOTextureTarget.h"

#include "GL3Renderer.h"
#include "Texture.h"

#include "CEGUI/Logger.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

// Start of CEGUI namespace section
namespace CEGUI
{
//----------------------------------------------------------------------------//
const float OpenGL3FBOTextureTarget::DEFAULT_SIZE = 128.0f;

//----------------------------------------------------------------------------//
OpenGL3FBOTextureTarget::OpenGL3FBOTextureTarget(OpenGL3Renderer& owner) :
    OpenGLTextureTarget(owner)
{
    // no need to initialise d_previousFrameBuffer here, it will be
    // initialised in activate()

    initialiseRenderTexture();

    // setup area and cause the initial texture to be generated.
    declareRenderSize(Sizef(DEFAULT_SIZE, DEFAULT_SIZE));
}

//----------------------------------------------------------------------------//
OpenGL3FBOTextureTarget::~OpenGL3FBOTextureTarget()
{
    Divide::GL_API::DeleteFramebuffers(1, &d_frameBuffer);
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::declareRenderSize(const Sizef& sz)
{
    setArea(Rectf(d_area.getPosition(), d_owner.getAdjustedTextureSize(sz)));
    resizeRenderTexture();
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::activate()
{
    // remember previously bound FBO to make sure we set it back
    // when deactivating
    // switch to rendering to the texture
    if (Divide::GL_API::GetStateTracker().setActiveFB(Divide::RenderTarget::Usage::RT_WRITE_ONLY, d_frameBuffer, d_previousFrameBuffer) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    OpenGLTextureTarget::activate();
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::deactivate()
{
    OpenGLTextureTarget::deactivate();

    // switch back to rendering to the previously bound framebuffer
    if (Divide::GL_API::GetStateTracker().setActiveFB(Divide::RenderTarget::Usage::RT_WRITE_ONLY, d_previousFrameBuffer) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::clear()
{
    const Sizef sz(d_area.getSize());
    // Some drivers crash when clearing a 0x0 RTT. This is a workaround for
    // those cases.
    if (sz.d_width < 1.0f || sz.d_height < 1.0f)
        return;

    static GLint clear[4] = { 0, 0, 0, 0 };
    glClearNamedFramebufferiv(d_frameBuffer, GL_COLOR, 0, &clear[0]);
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::initialiseRenderTexture()
{
    // create FBO
    glCreateFramebuffers(1, &d_frameBuffer);

    // set up the texture the FBO will draw to
    glCreateTextures(GL_TEXTURE_2D, 1, &d_texture);
    glTextureParameteri(d_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(d_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(d_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(d_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTextureStorage2D(d_texture,
                       1,
                       OpenGLInfo::getSingleton().isSizedInternalFormatSupported() ? GL_RGBA8 : GL_RGBA,
                       static_cast<GLsizei>(DEFAULT_SIZE),
                       static_cast<GLsizei>(DEFAULT_SIZE));

    glNamedFramebufferTexture(d_frameBuffer, GL_COLOR_ATTACHMENT0, d_texture, 0);

    //Check for framebuffer completeness
    checkFramebufferStatus();

    // ensure the CEGUI::Texture is wrapping the gl texture and has correct size
    d_CEGUITexture->setOpenGLTexture(d_texture, d_area.getSize());
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::resizeRenderTexture()
{
    // Some drivers (hint: Intel) segfault when glTexImage2D is called with
    // any of the dimensions being 0. The downside of this workaround is very
    // small. We waste a tiny bit of VRAM on cards with sane drivers and
    // prevent insane drivers from crashing CEGUI.
    Sizef sz(d_area.getSize());
    if (sz.d_width < 1.0f || sz.d_height < 1.0f)
    {
        sz.d_width = 1.0f;
        sz.d_height = 1.0f;
    }
    else if (sz.d_width > (1 << 13) || sz.d_height > (1 << 13))
    {
        sz.d_width = 1 << 13;
        sz.d_height = 1 << 13;
    }

    // set the texture to the required size (delete and create a new one due to immutable storage use)
    GLuint tempTexture = 0u;
    glCreateTextures(GL_TEXTURE_2D, 1, &tempTexture);
    glDeleteTextures(1, &d_texture);
    d_texture = tempTexture;
    glTextureStorage2D(d_texture, 
                       1,
                       OpenGLInfo::getSingleton().isSizedInternalFormatSupported() ? GL_RGBA8 : GL_RGBA,
                       static_cast<GLsizei>(sz.d_width),
                       static_cast<GLsizei>(sz.d_height));
    glNamedFramebufferTexture(d_frameBuffer, GL_COLOR_ATTACHMENT0, d_texture, 0);

    clear();

    // ensure the CEGUI::Texture is wrapping the gl texture and has correct size
    d_CEGUITexture->setOpenGLTexture(d_texture, sz);
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::grabTexture()
{
    Divide::GL_API::DeleteFramebuffers(1, &d_frameBuffer);
    OpenGLTextureTarget::grabTexture();
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::restoreTexture()
{
    OpenGLTextureTarget::restoreTexture();

    initialiseRenderTexture();
    resizeRenderTexture();
}

//----------------------------------------------------------------------------//
void OpenGL3FBOTextureTarget::checkFramebufferStatus()
{
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    // Check for completeness
    if(status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::stringstream stringStream;
        stringStream << "OpenGL3Renderer: Error  Framebuffer is not complete\n";

        switch(status)
        {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            stringStream << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n";
            break;
        case GL_FRAMEBUFFER_UNDEFINED:
            stringStream << "GL_FRAMEBUFFER_UNDEFINED \n";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            stringStream << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER :
            stringStream << "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER \n";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            stringStream << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            stringStream << "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n";
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            stringStream << "GL_FRAMEBUFFER_UNSUPPORTED\n";
            break;
        default:
            stringStream << "Undefined Framebuffer error\n";
            break;
        }

        if (Logger* logger = Logger::getSingletonPtr())
            logger->logEvent(stringStream.str().c_str());
        else
            Divide::Console::errorfn(stringStream.str().c_str());
    }
}

//----------------------------------------------------------------------------//



} // End of  CEGUI namespace section
