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
#include "ShaderManager.h"
#include "GL3Renderer.h"
#include "Texture.h"
#include "Shader.h"
#include "CEGUI/Exceptions.h"
#include "CEGUI/DynamicModule.h"
#include "GL3GeometryBuffer.h"
#include "GL3FBOTextureTarget.h"
#include "CEGUI/System.h"
#include "CEGUI/DefaultResourceProvider.h"
#include "StateChangeWrapper.h"

// Start of CEGUI namespace section
namespace CEGUI
{
//----------------------------------------------------------------------------//
// The following are some GL extension / version dependant related items.
// This is all done totally internally here; no need for external interface
// to show any of this.
//----------------------------------------------------------------------------//
// we only really need this with MSVC / Windows(?) and by now it should already
// be defined on that platform, so we just define it as empty macro so the
// compile does not break on other systems.
#ifndef APIENTRY
#   define APIENTRY
#endif

//----------------------------------------------------------------------------//
// template specialised class that does the real work for us
template<typename T>
class OGLTemplateTargetFactory : public OGLTextureTargetFactory
{
    TextureTarget* create(OpenGLRendererBase& r) const override
    { return CEGUI_NEW_AO T(static_cast<OpenGL3Renderer&>(r)); }
};

//----------------------------------------------------------------------------//
OpenGL3Renderer& OpenGL3Renderer::bootstrapSystem(const int abi)
{
    System::performVersionTest(CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME);

    if (System::getSingletonPtr())
        CEGUI_THROW(InvalidRequestException(
            "CEGUI::System object is already initialised."));

    OpenGL3Renderer& renderer(create());
    DefaultResourceProvider* rp = CEGUI_NEW_AO DefaultResourceProvider();
    System::create(renderer, rp);

    return renderer;
}

//----------------------------------------------------------------------------//
OpenGL3Renderer& OpenGL3Renderer::bootstrapSystem(const Sizef& display_size,
                                                  const int abi)
{
    System::performVersionTest(CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME);

    if (System::getSingletonPtr())
        CEGUI_THROW(InvalidRequestException(
            "CEGUI::System object is already initialised."));

    OpenGL3Renderer& renderer(create(display_size));
    DefaultResourceProvider* rp = CEGUI_NEW_AO DefaultResourceProvider();
    System::create(renderer, rp);

    return renderer;
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::destroySystem()
{
    System* sys = System::getSingletonPtr();
    if (sys == nullptr) {
        CEGUI_THROW(InvalidRequestException("CEGUI::System object is not created or was already destroyed."));
    }

    OpenGL3Renderer* renderer = static_cast<OpenGL3Renderer*>(sys->getRenderer());
    DefaultResourceProvider* rp =
        static_cast<DefaultResourceProvider*>(sys->getResourceProvider());

    System::destroy();
    CEGUI_DELETE_AO rp;
    destroy(*renderer);
}

//----------------------------------------------------------------------------//
OpenGL3Renderer& OpenGL3Renderer::create(const int abi)
{
    System::performVersionTest(CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME);

    return *CEGUI_NEW_AO OpenGL3Renderer();
}

//----------------------------------------------------------------------------//
OpenGL3Renderer& OpenGL3Renderer::create(const Sizef& display_size,
                                         const int abi)
{
    System::performVersionTest(CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME);

    return *CEGUI_NEW_AO OpenGL3Renderer(display_size);
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::destroy(OpenGL3Renderer& renderer)
{
    CEGUI_DELETE_AO &renderer;
}

//----------------------------------------------------------------------------//
OpenGL3Renderer::OpenGL3Renderer() :
    OpenGLRendererBase(true),
    d_shaderStandard(nullptr),
    d_openGLStateChanger(nullptr),
    d_shaderManager(nullptr)
{
    init();
    CEGUI_UNUSED(d_s3tcSupported);
    // d_s3tcSupported is unused, but must be preserved to avoid breaking ABI
}

//----------------------------------------------------------------------------//
OpenGL3Renderer::OpenGL3Renderer(const Sizef& display_size) :
    OpenGLRendererBase(display_size, true),
    d_shaderStandard(nullptr),
    d_openGLStateChanger(nullptr),
    d_shaderManager(nullptr)
{
    init();
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::init()
{
    initialiseRendererIDString();
    initialiseTextureTargetFactory();
    initialiseOpenGLShaders();
    d_openGLStateChanger = CEGUI_NEW_AO OpenGL3StateChangeWrapper();
}

//----------------------------------------------------------------------------//
OpenGL3Renderer::~OpenGL3Renderer()
{
    CEGUI_DELETE_AO d_textureTargetFactory;
    CEGUI_DELETE_AO d_openGLStateChanger;
    CEGUI_DELETE_AO d_shaderManager;
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::initialiseRendererIDString()
{
    d_rendererID = OpenGLInfo::getSingleton().isUsingDesktopOpengl()
        ?  "CEGUI::OpenGL3Renderer - Official OpenGL 3.2 core based "
           "renderer module."
        :  "CEGUI::OpenGL3Renderer - OpenGL ES 2 renderer module.";
}
//----------------------------------------------------------------------------//
OpenGLGeometryBufferBase* OpenGL3Renderer::createGeometryBuffer_impl()
{
    return CEGUI_NEW_AO OpenGL3GeometryBuffer(*this);
}

//----------------------------------------------------------------------------//
TextureTarget* OpenGL3Renderer::createTextureTarget_impl()
{
    return d_textureTargetFactory->create(*this);
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::beginRendering()
{
    // Deprecated OpenGL 2 client states may mess up rendering. They are not added here
    // since they are deprecated and thus do not fit in a OpenGL Core renderer. However
    // this information may be relevant for people combining deprecated and modern
    // functions. In that case disable client states like this: glDisableClientState(GL_VERTEX_ARRAY);

    Divide::GL_API::PushDebugMessage("CEGUI Begin");

    d_openGLStateChanger->reset();

    // if enabled, restores a subset of the GL state back to default values.
    if (d_initExtraStates)
        setupExtraStates();

    d_openGLStateChanger->bindDefaultState(true);
    // force set blending ops to get to a known state.
    setupRenderingBlendMode(BM_NORMAL, true);
    d_shaderStandard->bind();
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::endRendering()
{
    if (d_initExtraStates)
        setupExtraStates();

    Divide::GL_API::GetStateTracker().setBlending(Divide::BlendingProperties());

    Divide::GL_API::PopDebugMessage();
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::setupExtraStates()
{
    if (Divide::GL_API::GetStateTracker().bindTexture(0, Divide::TextureType::TEXTURE_2D, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveProgram(0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveShaderPipeline(0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    d_openGLStateChanger->blendFunc(GL_ONE, GL_ZERO);
    d_openGLStateChanger->bindVertexArray(0);
    d_openGLStateChanger->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    d_openGLStateChanger->bindBuffer(GL_ARRAY_BUFFER, 0);
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::initialiseTextureTargetFactory()
{
    //Use OGL core implementation for FBOs
    d_rendererID += "  TextureTarget support enabled via FBO OGL 3.2 core implementation.";
    d_textureTargetFactory = CEGUI_NEW_AO OGLTemplateTargetFactory<OpenGL3FBOTextureTarget>;
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::setupRenderingBlendMode(const BlendMode mode,
                                             const bool force)
{
    // exit if mode is already set up (and update not forced)
    if (d_activeBlendMode == mode && !force)
        return;

    d_activeBlendMode = mode;

    if (d_activeBlendMode == BM_RTT_PREMULTIPLIED)
    {
        Divide::BlendingProperties blend{};
        blend.enabled(true);
        blend.blendSrc(Divide::BlendProperty::ONE);
        blend.blendDest(Divide::BlendProperty::INV_SRC_ALPHA);
        blend.blendOp(Divide::BlendOperation::ADD);

        Divide::GL_API::GetStateTracker().setBlending(blend);
    }
    else
    {
        Divide::BlendingProperties blend{};
        blend.enabled(true);
        blend.blendSrc(Divide::BlendProperty::SRC_ALPHA);
        blend.blendDest(Divide::BlendProperty::INV_SRC_ALPHA);
        blend.blendOp(Divide::BlendOperation::ADD);
        blend.blendSrcAlpha(Divide::BlendProperty::INV_DEST_ALPHA);
        blend.blendDestAlpha(Divide::BlendProperty::ONE);
        blend.blendOpAlpha(Divide::BlendOperation::ADD);

        Divide::GL_API::GetStateTracker().setBlending(blend);
    }
}

//----------------------------------------------------------------------------//
Sizef OpenGL3Renderer::getAdjustedTextureSize(const Sizef& sz) const
{
    return Sizef(sz);
}

//----------------------------------------------------------------------------//
OpenGL3Shader*& OpenGL3Renderer::getShaderStandard()
{
    return d_shaderStandard;
}

//----------------------------------------------------------------------------//
GLint OpenGL3Renderer::getShaderStandardPositionLoc()
{
    return d_shaderStandardPosLoc;
}

//----------------------------------------------------------------------------//
GLint OpenGL3Renderer::getShaderStandardTexCoordLoc()
{
    return d_shaderStandardTexCoordLoc;
}

//----------------------------------------------------------------------------//
GLint OpenGL3Renderer::getShaderStandardColourLoc()
{
    return d_shaderStandardColourLoc;
}

//----------------------------------------------------------------------------//
GLint OpenGL3Renderer::getShaderStandardMatrixUniformLoc()
{
    return d_shaderStandardMatrixLoc;
}

//----------------------------------------------------------------------------//
OpenGL3StateChangeWrapper* OpenGL3Renderer::getOpenGLStateChanger()
{
    return d_openGLStateChanger;
}

//----------------------------------------------------------------------------//
void OpenGL3Renderer::initialiseOpenGLShaders()
{
    checkGLErrors();
    d_shaderManager = CEGUI_NEW_AO OpenGL3ShaderManager();
    d_shaderManager->initialiseShaders();
    d_shaderStandard = d_shaderManager->getShader(SHADER_ID_STANDARDSHADER);
    d_shaderStandardPosLoc = d_shaderStandard->getAttribLocation("inPosition");
    d_shaderStandardTexCoordLoc = d_shaderStandard->getAttribLocation("inTexCoord");
    d_shaderStandardColourLoc = d_shaderStandard->getAttribLocation("inColour");

    d_shaderStandardMatrixLoc = d_shaderStandard->getUniformLocation("modelViewPerspMatrix");
}

//----------------------------------------------------------------------------//
bool OpenGL3Renderer::isS3TCSupported() const
{
    return OpenGLInfo::getSingleton().isS3tcSupported();
}

//----------------------------------------------------------------------------//

} // End of  CEGUI namespace section
