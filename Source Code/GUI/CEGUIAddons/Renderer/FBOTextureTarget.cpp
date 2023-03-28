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
#include "Headers/FBOTextureTarget.h"
#include "Headers/DVDGeometryBuffer.h"
#include "Headers/RendererBase.h"
#include "Headers/OpenGLTexture.h"

#include "CEGUI/Logger.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

// Start of CEGUI namespace section
namespace CEGUI
{
//----------------------------------------------------------------------------//
const float FBOTextureTarget::DEFAULT_SIZE = 128.0f;
const double FBOTextureTarget::d_yfov_tan = 0.267949192431123;
uint FBOTextureTarget::s_textureNumber = 0;

//----------------------------------------------------------------------------//
FBOTextureTarget::FBOTextureTarget( DivideRenderer& owner)
    : d_owner( owner )
{
    createCEGUITexture();

    // no need to initialise d_previousFrameBuffer here, it will be
    // initialised in activate()

    initialiseRenderTexture();

    // setup area and cause the initial texture to be generated.
    declareRenderSize(Sizef(DEFAULT_SIZE, DEFAULT_SIZE));
}

//----------------------------------------------------------------------------//
FBOTextureTarget::~FBOTextureTarget()
{
    d_owner.destroyTexture( *d_CEGUITexture );
    Divide::GL_API::DeleteFramebuffers(1, &d_frameBuffer);
}

void FBOTextureTarget::draw( const GeometryBuffer& buffer )
{
    buffer.draw();
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::draw( const RenderQueue& queue )
{
    queue.draw();
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::declareRenderSize(const Sizef& sz)
{
    setArea(Rectf(d_area.getPosition(), sz));
    resizeRenderTexture();
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::clear()
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
void FBOTextureTarget::initialiseRenderTexture()
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
                       GL_RGBA8,
                       static_cast<GLsizei>(DEFAULT_SIZE),
                       static_cast<GLsizei>(DEFAULT_SIZE));

    glNamedFramebufferTexture(d_frameBuffer, GL_COLOR_ATTACHMENT0, d_texture, 0);

    //Check for framebuffer completeness
    checkFramebufferStatus();

    // ensure the CEGUI::Texture is wrapping the gl texture and has correct size
    d_CEGUITexture->setOpenGLTexture(d_texture, d_area.getSize());
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::resizeRenderTexture()
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
                       GL_RGBA8,
                       static_cast<GLsizei>(sz.d_width),
                       static_cast<GLsizei>(sz.d_height));
    glNamedFramebufferTexture(d_frameBuffer, GL_COLOR_ATTACHMENT0, d_texture, 0);

    clear();

    // ensure the CEGUI::Texture is wrapping the gl texture and has correct size
    d_CEGUITexture->setOpenGLTexture(d_texture, sz);
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::grabTexture()
{
    Divide::GL_API::DeleteFramebuffers(1, &d_frameBuffer);
    if ( d_CEGUITexture )
    {
        d_owner.destroyTexture( *d_CEGUITexture );
        d_texture = 0;
        d_CEGUITexture = nullptr;
    }
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::restoreTexture()
{
    if ( !d_CEGUITexture )
        createCEGUITexture();

    initialiseRenderTexture();
    resizeRenderTexture();
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::createCEGUITexture()
{
    d_CEGUITexture = &static_cast<OpenGLTexture&>(
        d_owner.createTexture( generateTextureName(),
                               d_texture, d_area.getSize() ));
}

//----------------------------------------------------------------------------//
String FBOTextureTarget::generateTextureName()
{
    String tmp( "_ogl_tt_tex_" );
    tmp.append( PropertyHelper<uint>::toString( s_textureNumber++ ) );

    return tmp;
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::checkFramebufferStatus()
{
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    // Check for completeness
    if(status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::stringstream stringStream;
        stringStream << "DivideRenderer: Error  Framebuffer is not complete\n";

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
bool FBOTextureTarget::isImageryCache() const
{
    return true;
}

//----------------------------------------------------------------------------//
Texture& FBOTextureTarget::getTexture() const
{
    return *d_CEGUITexture;
}

//----------------------------------------------------------------------------//
bool FBOTextureTarget::isRenderingInverted() const
{
    return true;
}

void FBOTextureTarget::setArea( const Rectf& area )
{
    d_area = area;
    d_matrixValid = false;

    RenderTargetEventArgs args( this );
    TextureTarget::fireEvent( RenderTarget::EventAreaChanged, args );
}

//----------------------------------------------------------------------------//
const Rectf& FBOTextureTarget::getArea() const
{
    return d_area;
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::activate()
{
    // remember previously bound FBO to make sure we set it back
    // when deactivating
    // switch to rendering to the texture
    if ( Divide::GL_API::GetStateTracker().setActiveFB( Divide::RenderTarget::Usage::RT_WRITE_ONLY, d_frameBuffer, d_previousFrameBuffer ) == Divide::GLStateTracker::BindResult::FAILED )
    {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    Divide::GL_API::GetStateTracker().getActiveViewport( _prevViewport );
    Divide::GL_API::GetStateTracker().setViewport(
        static_cast<Divide::I32>(d_area.left()),
        static_cast<Divide::I32>(d_area.top()),
        static_cast<Divide::I32>(d_area.getWidth()),
        static_cast<Divide::I32>(d_area.getHeight()) );

    if ( !d_matrixValid )
        updateMatrix();

    d_owner.setViewProjectionMatrix( d_matrix );

    d_owner.setActiveRenderTarget( this );
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::deactivate()
{
    // switch back to rendering to the previously bound framebuffer
    if ( Divide::GL_API::GetStateTracker().setActiveFB( Divide::RenderTarget::Usage::RT_WRITE_ONLY, d_previousFrameBuffer ) == Divide::GLStateTracker::BindResult::FAILED )
    {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    Divide::GL_API::GetStateTracker().setViewport( _prevViewport[0],
                                                   _prevViewport[1],
                                                   _prevViewport[2],
                                                   _prevViewport[3] );
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::unprojectPoint( const GeometryBuffer& buff,
                                            const Vector2f& p_in, Vector2f& p_out ) const
{
    if ( !d_matrixValid )
        updateMatrix();

    const DVDGeometryBuffer& gb =
        static_cast<const DVDGeometryBuffer&>(buff);

    const GLint vp[4] = {
        static_cast<GLint>(d_area.left()),
        static_cast<GLint>(d_area.top()),
        static_cast<GLint>(d_area.getWidth()),
        static_cast<GLint>(d_area.getHeight())
    };

    GLdouble in_x = 0.0, in_y = 0.0, in_z = 0.0;

    glm::ivec4 viewPort = glm::ivec4( vp[0], vp[1], vp[2], vp[3] );
    const glm::mat4& projMatrix = d_matrix;
    const glm::mat4& modelMatrix = gb.getMatrix();

    // unproject the ends of the ray
    glm::vec3 unprojected1;
    glm::vec3 unprojected2;
    in_x = vp[2] * 0.5;
    in_y = vp[3] * 0.5;
    in_z = -d_viewDistance;
    unprojected1 = glm::unProject( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = p_in.d_x;
    in_y = vp[3] - p_in.d_y;
    in_z = 0.0;
    unprojected2 = glm::unProject( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );

    // project points to orientate them with GeometryBuffer plane
    glm::vec3 projected1;
    glm::vec3 projected2;
    glm::vec3 projected3;
    in_x = 0.0;
    in_y = 0.0;
    projected1 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = 1.0;
    in_y = 0.0;
    projected2 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = 0.0;
    in_y = 1.0;
    projected3 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );

    // calculate vectors for generating the plane
    const glm::vec3 pv1 = projected2 - projected1;
    const glm::vec3 pv2 = projected3 - projected1;
    // given the vectors, calculate the plane normal
    const glm::vec3 planeNormal = glm::cross( pv1, pv2 );
    // calculate plane
    const glm::vec3 planeNormalNormalized = glm::normalize( planeNormal );
    const double pl_d = -glm::dot( projected1, planeNormalNormalized );
    // calculate vector of picking ray
    const glm::vec3 rv = unprojected1 - unprojected2;
    // calculate intersection of ray and plane
    const double pn_dot_r1 = glm::dot( unprojected1, planeNormal );
    const double pn_dot_rv = glm::dot( rv, planeNormal );
    const double tmp1 = pn_dot_rv != 0.0 ? (pn_dot_r1 + pl_d) / pn_dot_rv : 0.0;
    const double is_x = unprojected1.x - rv.x * tmp1;
    const double is_y = unprojected1.y - rv.y * tmp1;

    p_out.d_x = static_cast<float>(is_x);
    p_out.d_y = static_cast<float>(is_y);

    p_out = p_in; // CrazyEddie wanted this
}

//----------------------------------------------------------------------------//
void FBOTextureTarget::updateMatrix() const
{
    const float w = d_area.getWidth();
    const float h = d_area.getHeight();

    // We need to check if width or height are zero and act accordingly to prevent running into issues
    // with divisions by zero which would lead to undefined values, as well as faulty clipping planes
    // This is mostly important for avoiding asserts
    const bool widthAndHeightNotZero = w != 0.0f && h != 0.0f;

    const float aspect = widthAndHeightNotZero ? w / h : 1.0f;
    const float midx = widthAndHeightNotZero ? w * 0.5f : 0.5f;
    const float midy = widthAndHeightNotZero ? h * 0.5f : 0.5f;
    d_viewDistance = midx / (aspect * d_yfov_tan);

    const glm::vec3 eye = glm::vec3( midx, midy, float( -d_viewDistance ) );
    const glm::vec3 center = glm::vec3( midx, midy, 1 );
    const glm::vec3 up = glm::vec3( 0, -1, 0 );

    //Older glm versions use degrees as parameter here by default (Unless radians are forced via GLM_FORCE_RADIANS). Newer versions of glm exlusively use radians.
#if (GLM_VERSION_MAJOR == 0 && GLM_VERSION_MINOR <= 9 && GLM_VERSION_PATCH < 6) && (!defined(GLM_FORCE_RADIANS))
    const glm::mat4 projectionMatrix = glm::perspective( 30.f, aspect, float( d_viewDistance * 0.5 ), float( d_viewDistance * 2.0 ) );
#else
    glm::mat4 projectionMatrix = glm::perspective( glm::radians( 30.f ), aspect, float( d_viewDistance * 0.5 ), float( d_viewDistance * 2.0 ) );
#endif

    // Projection matrix abuse!
    const glm::mat4 viewMatrix = glm::lookAt( eye, center, up );

    d_matrix = projectionMatrix * viewMatrix;

    d_matrixValid = true;
}

} // End of  CEGUI namespace section
