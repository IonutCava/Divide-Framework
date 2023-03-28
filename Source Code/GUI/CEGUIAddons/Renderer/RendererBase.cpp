#include "stdafx.h"

/***********************************************************************
    created:    Tue Apr 30 2013
    authors:    Paul D Turner <paul@cegui.org.uk>
                Lukas E Meindl
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2013 Paul D Turner & The CEGUI Development Team
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
#include "Headers/RendererBase.h"
#include "Headers/OpenGLTexture.h"
#include "Headers/StateChangeWrapper.h"
#include "Headers/DVDShader.h"
#include "Headers/DVDGeometryBuffer.h"
#include "Headers/FBOTextureTarget.h"

#include "CEGUI/Exceptions.h"
#include "CEGUI/ImageCodec.h"
#include "CEGUI/Logger.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace CEGUI
{

//----------------------------------------------------------------------------//
String DivideRenderer::d_rendererID("Divide CEGUI Renderer");

//----------------------------------------------------------------------------//
DivideRenderer::DivideRenderer( Divide::GFXDevice& context ) :
    _context(context)
{
    init();
    initialiseDisplaySizeWithViewportSize();
    d_defaultTarget = CEGUI_NEW_AO FBOTextureTarget(*this);
}

//----------------------------------------------------------------------------//
void DivideRenderer::init(bool init_glew, bool set_glew_experimental)
{
    d_displayDPI.d_x = d_displayDPI.d_y = 96;
    d_activeBlendMode = BM_INVALID;
    d_viewProjectionMatrix = {};

    CEGUI_UNUSED(init_glew);
    CEGUI_UNUSED(set_glew_experimental);

    initialiseOpenGLShaders();
    d_openGLStateChanger = CEGUI_NEW_AO StateChangeWrapper();
}

//----------------------------------------------------------------------------//
DivideRenderer::~DivideRenderer()
{

    CEGUI_DELETE_AO d_defaultTarget;
    CEGUI_DELETE_AO d_openGLStateChanger;
    CEGUI_DELETE_AO d_shaderStandard;

    destroyAllGeometryBuffers();
    destroyAllTextureTargets();
    destroyAllTextures();
}

//----------------------------------------------------------------------------//
DivideRenderer& DivideRenderer::create( Divide::GFXDevice& context, const int abi )
{
    System::performVersionTest( CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME );

    return *CEGUI_NEW_AO DivideRenderer(context);
}

void DivideRenderer::destroy( DivideRenderer& renderer )
{
    CEGUI_DELETE_AO& renderer;
}
//----------------------------------------------------------------------------//
void DivideRenderer::initialiseDisplaySizeWithViewportSize()
{
    GLint vp[4];
    Divide::GL_API::GetStateTracker().getActiveViewport(vp);

    d_displaySize = Sizef(static_cast<float>(vp[2]),
                          static_cast<float>(vp[3]));
}

//----------------------------------------------------------------------------//
RenderTarget& DivideRenderer::getDefaultRenderTarget()
{
    return *d_defaultTarget;
}

//----------------------------------------------------------------------------//
GeometryBuffer& DivideRenderer::createGeometryBuffer()
{
    GeometryBuffer* b = CEGUI_NEW_AO DVDGeometryBuffer( *this );
    d_geometryBuffers.push_back(b);
    return *b;
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyGeometryBuffer(const GeometryBuffer& buffer)
{
    const GeometryBufferList::iterator i = std::find(d_geometryBuffers.begin(),
                                                     d_geometryBuffers.end(),
                                                     &buffer);

    if (d_geometryBuffers.end() != i)
    {
        d_geometryBuffers.erase(i);
        CEGUI_DELETE_AO &buffer;
    }
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyAllGeometryBuffers()
{
    while (!d_geometryBuffers.empty())
        destroyGeometryBuffer(**d_geometryBuffers.begin());
}

//----------------------------------------------------------------------------//
TextureTarget* DivideRenderer::createTextureTarget()
{
    TextureTarget* t = CEGUI_NEW_AO FBOTextureTarget( *this );

    if (t)
        d_textureTargets.push_back(t);

    return t;
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyTextureTarget(TextureTarget* target)
{
    const TextureTargetList::iterator i = std::find(d_textureTargets.begin(),
                                                    d_textureTargets.end(),
                                                    target);

    if (d_textureTargets.end() != i)
    {
        d_textureTargets.erase(i);
        CEGUI_DELETE_AO target;
    }
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyAllTextureTargets()
{
    while (!d_textureTargets.empty())
        destroyTextureTarget(*d_textureTargets.begin());
}

//----------------------------------------------------------------------------//
Texture& DivideRenderer::createTexture(const String& name)
{
    if (d_textures.find(name) != d_textures.end())
        CEGUI_THROW(AlreadyExistsException(
            "A texture named '" + name + "' already exists."));

    OpenGLTexture* tex = CEGUI_NEW_AO OpenGLTexture(*this, name);
    d_textures[name] = tex;

    logTextureCreation(name);

    return *tex;
}

//----------------------------------------------------------------------------//
Texture& DivideRenderer::createTexture(const String& name,
                                       const String& filename,
                                       const String& resourceGroup)
{
    if (d_textures.find(name) != d_textures.end())
        CEGUI_THROW(AlreadyExistsException(
            "A texture named '" + name + "' already exists."));

    OpenGLTexture* tex = CEGUI_NEW_AO OpenGLTexture(*this, name, filename, resourceGroup);
    d_textures[name] = tex;

    logTextureCreation(name);

    return *tex;
}

//----------------------------------------------------------------------------//
Texture& DivideRenderer::createTexture(const String& name, const Sizef& size)
{
    if (d_textures.find(name) != d_textures.end())
        CEGUI_THROW(AlreadyExistsException(
            "A texture named '" + name + "' already exists."));

    OpenGLTexture* tex = CEGUI_NEW_AO OpenGLTexture(*this, name, size);
    d_textures[name] = tex;

    logTextureCreation(name);

    return *tex;
}

//----------------------------------------------------------------------------//
void DivideRenderer::logTextureCreation(const String& name)
{
    Logger* logger = Logger::getSingletonPtr();
    if (logger)
        logger->logEvent("[OpenGLRenderer] Created texture: " + name);
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyTexture(Texture& texture)
{
    destroyTexture(texture.getName());
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyTexture(const String& name)
{
    TextureMap::iterator i = d_textures.find(name);

    if (d_textures.end() != i)
    {
        logTextureDestruction(name);
        CEGUI_DELETE_AO i->second;
        d_textures.erase(i);
    }
}

//----------------------------------------------------------------------------//
void DivideRenderer::logTextureDestruction(const String& name)
{
    Logger* logger = Logger::getSingletonPtr();
    if (logger)
        logger->logEvent("[OpenGLRenderer] Destroyed texture: " + name);
}

//----------------------------------------------------------------------------//
void DivideRenderer::destroyAllTextures()
{
    while (!d_textures.empty())
        destroyTexture(d_textures.begin()->first);
}

//----------------------------------------------------------------------------//
Texture& DivideRenderer::getTexture(const String& name) const
{
    const TextureMap::const_iterator i = d_textures.find(name);
    
    if (i == d_textures.end())
        CEGUI_THROW(UnknownObjectException(
            "No texture named '" + name + "' is available."));

    return *i->second;
}

//----------------------------------------------------------------------------//
bool DivideRenderer::isTextureDefined(const String& name) const
{
    return d_textures.find(name) != d_textures.end();
}

//----------------------------------------------------------------------------//
const Sizef& DivideRenderer::getDisplaySize() const
{
    return d_displaySize;
}

//----------------------------------------------------------------------------//
const Vector2f& DivideRenderer::getDisplayDPI() const
{
    return d_displayDPI;
}

//----------------------------------------------------------------------------//
uint DivideRenderer::getMaxTextureSize() const
{
    return Divide::GFXDevice::GetDeviceInformation()._maxTextureSize;
}

//----------------------------------------------------------------------------//
const String& DivideRenderer::getIdentifierString() const
{
    return d_rendererID;
}

//----------------------------------------------------------------------------//
Texture& DivideRenderer::createTexture(const String& name, GLuint tex,
                                       const Sizef& sz)
{
    if (d_textures.find(name) != d_textures.end())
        CEGUI_THROW(AlreadyExistsException(
            "A texture named '" + name + "' already exists."));

    OpenGLTexture* t = CEGUI_NEW_AO OpenGLTexture(*this, name, tex, sz);
    d_textures[name] = t;

    logTextureCreation(name);

    return *t;
}

//----------------------------------------------------------------------------//
void DivideRenderer::setDisplaySize(const Sizef& sz)
{
    if (sz != d_displaySize)
    {
        d_displaySize = sz;

        // update the default target's area
        Rectf area(d_defaultTarget->getArea());
        area.setSize(sz);
        d_defaultTarget->setArea(area);
    }
}

//----------------------------------------------------------------------------//
void DivideRenderer::grabTextures()
{
    // perform grab operations for texture targets
    TextureTargetList::iterator target_iterator = d_textureTargets.begin();
    for (; target_iterator != d_textureTargets.end(); ++target_iterator)
        static_cast<FBOTextureTarget*>(*target_iterator)->grabTexture();

    // perform grab on regular textures
    TextureMap::iterator texture_iterator = d_textures.begin();
    for (; texture_iterator != d_textures.end(); ++texture_iterator)
        texture_iterator->second->grabTexture();
}

//----------------------------------------------------------------------------//
void DivideRenderer::restoreTextures()
{
    // perform restore on textures
    TextureMap::iterator texture_iterator = d_textures.begin();
    for (; texture_iterator != d_textures.end(); ++texture_iterator)
        texture_iterator->second->restoreTexture();

    // perform restore operations for texture targets
    TextureTargetList::iterator target_iterator = d_textureTargets.begin();
    for (; target_iterator != d_textureTargets.end(); ++target_iterator)
        static_cast<FBOTextureTarget*>(*target_iterator)->restoreTexture();
}

//----------------------------------------------------------------------------//
float DivideRenderer::getNextPOTSize(const float f)
{
    uint size = static_cast<uint>(f);

    // if not power of 2
    if (size & size - 1 || !size)
    {
        int log = 0;

        // get integer log of 'size' to base 2
        while (size >>= 1)
            ++log;

        // use log to calculate value to use as size.
        size = 2 << log;
    }

    return static_cast<float>(size);
}

//----------------------------------------------------------------------------//
const glm::mat4& DivideRenderer::getViewProjectionMatrix()
{
    return d_viewProjectionMatrix;
}

//----------------------------------------------------------------------------//
void DivideRenderer::setViewProjectionMatrix(const glm::mat4& viewProjectionMatrix)
{
    d_viewProjectionMatrix = viewProjectionMatrix;
}

//----------------------------------------------------------------------------//
const Rectf& DivideRenderer::getActiveViewPort()
{
    return d_activeRenderTarget->getArea();
}

//----------------------------------------------------------------------------//
void DivideRenderer::setActiveRenderTarget(RenderTarget* renderTarget)
{
    d_activeRenderTarget = renderTarget;
}

//----------------------------------------------------------------------------//
RenderTarget* DivideRenderer::getActiveRenderTarget()
{
    return d_activeRenderTarget;
}

//----------------------------------------------------------------------------//
void DivideRenderer::beginRendering()
{
    // Deprecated OpenGL 2 client states may mess up rendering. They are not added here
    // since they are deprecated and thus do not fit in a OpenGL Core renderer. However
    // this information may be relevant for people combining deprecated and modern
    // functions. In that case disable client states like this: glDisableClientState(GL_VERTEX_ARRAY);

    Divide::GL_API::PushDebugMessage( "CEGUI Begin" );

    d_openGLStateChanger->reset();

    d_openGLStateChanger->bindDefaultState( true );
    // force set blending ops to get to a known state.
    setupRenderingBlendMode( BM_NORMAL, true );
    d_shaderStandard->bind();
}

//----------------------------------------------------------------------------//
void DivideRenderer::endRendering()
{
    Divide::GL_API::GetStateTracker().setBlending( Divide::BlendingSettings() );

    Divide::GL_API::PopDebugMessage();
}

//----------------------------------------------------------------------------//
void DivideRenderer::setupRenderingBlendMode( const BlendMode mode,
                                               const bool force )
{
    // exit if mode is already set up (and update not forced)
    if ( d_activeBlendMode == mode && !force )
        return;

    d_activeBlendMode = mode;

    if ( d_activeBlendMode == BM_RTT_PREMULTIPLIED )
    {
        Divide::BlendingSettings blend{};
        blend.enabled( true );
        blend.blendSrc( Divide::BlendProperty::ONE );
        blend.blendDest( Divide::BlendProperty::INV_SRC_ALPHA );
        blend.blendOp( Divide::BlendOperation::ADD );

        Divide::GL_API::GetStateTracker().setBlending( blend );
    }
    else
    {
        Divide::BlendingSettings blend{};
        blend.enabled( true );
        blend.blendSrc( Divide::BlendProperty::SRC_ALPHA );
        blend.blendDest( Divide::BlendProperty::INV_SRC_ALPHA );
        blend.blendOp( Divide::BlendOperation::ADD );
        blend.blendSrcAlpha( Divide::BlendProperty::INV_DEST_ALPHA );
        blend.blendDestAlpha( Divide::BlendProperty::ONE );
        blend.blendOpAlpha( Divide::BlendOperation::ADD );

        Divide::GL_API::GetStateTracker().setBlending( blend );
    }
}

//----------------------------------------------------------------------------//
DVDShader*& DivideRenderer::getShaderStandard()
{
    return d_shaderStandard;
}

//----------------------------------------------------------------------------//
GLint DivideRenderer::getShaderStandardPositionLoc()
{
    return d_shaderStandardPosLoc;
}

//----------------------------------------------------------------------------//
GLint DivideRenderer::getShaderStandardTexCoordLoc()
{
    return d_shaderStandardTexCoordLoc;
}

//----------------------------------------------------------------------------//
GLint DivideRenderer::getShaderStandardColourLoc()
{
    return d_shaderStandardColourLoc;
}

//----------------------------------------------------------------------------//
GLint DivideRenderer::getShaderStandardMatrixUniformLoc()
{
    return d_shaderStandardMatrixLoc;
}

//----------------------------------------------------------------------------//
StateChangeWrapper* DivideRenderer::getOpenGLStateChanger()
{
    return d_openGLStateChanger;
}

//----------------------------------------------------------------------------//
void DivideRenderer::initialiseOpenGLShaders()
{
    checkGLErrors();
    if ( d_shaderStandard == nullptr )
    {
        constexpr const char* vertexShader = "#version 150 core\n"
            "uniform mat4 modelViewPerspMatrix;\n"
            "in vec3 inPosition;\n"
            "in vec2 inTexCoord;\n"
            "in vec4 inColour;\n"
            "out vec2 exTexCoord;\n"
            "out vec4 exColour;\n"

            "void main(void)\n"
            "{\n"
            "exTexCoord = inTexCoord;\n"
            "exColour = inColour;\n"
            "gl_Position = modelViewPerspMatrix * vec4(inPosition, 1.0);\n"
            "}";

        constexpr const char* fragmentShader = "#version 420 core\n"
            "layout(binding = 0) uniform sampler2D texture0;\n"
            "in vec2 exTexCoord;\n"
            "in vec4 exColour;\n"
            "out vec4 out0;\n"

            "void main(void)\n"
            "{\n"
            "out0 = texture(texture0, exTexCoord) * exColour;\n"
            "}";

        d_shaderStandard = CEGUI_NEW_AO DVDShader( vertexShader, fragmentShader );
        d_shaderStandard->link();
        if ( !d_shaderStandard->isCreatedSuccessfully() )
        {
            const String errorString( "Critical Error - One or "
                                      "multiple shader programs weren't created successfully" );
            CEGUI_THROW( RendererException( errorString ) );
        }
        else
        {
            d_shaderStandardPosLoc = d_shaderStandard->getAttribLocation( "inPosition" );
            d_shaderStandardTexCoordLoc = d_shaderStandard->getAttribLocation( "inTexCoord" );
            d_shaderStandardColourLoc = d_shaderStandard->getAttribLocation( "inColour" );
            d_shaderStandardMatrixLoc = d_shaderStandard->getUniformLocation( "modelViewPerspMatrix" );

            const String notify( "DivideRenderer: Notification - Successfully initialised DivideRenderer shader programs." );
            if ( Logger* logger = Logger::getSingletonPtr() )
                logger->logEvent( notify );
        }
    }
}


}

