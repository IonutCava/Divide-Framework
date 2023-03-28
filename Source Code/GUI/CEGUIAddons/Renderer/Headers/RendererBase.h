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
#pragma once
#ifndef _CEGUIRendererBase_h_
#define _CEGUIRendererBase_h_

#include "CEGUI/Base.h"
#include "CEGUI/Renderer.h"
#include "CEGUI/Size.h"
#include "CEGUI/Vector.h"
#include "CEGUI/Rect.h"
#include "CEGUI/TextureTarget.h"

#include "CEGUI/Config.h"
#include <glbinding/gl/types.h>
using namespace gl;

#include <glm/glm/mat4x4.hpp>

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4251)
#endif

namespace Divide
{
    class GFXDevice;
};

namespace CEGUI
{

class DVDShader;
class OpenGLTexture;
class DVDGeometryBuffer;
class StateChangeWrapper;

class DivideRenderer;

class DivideRenderer : public Renderer
{
public:
    static DivideRenderer& create( Divide::GFXDevice& context, int abi = CEGUI_VERSION_ABI );
    static void destroy( DivideRenderer& renderer );

    // implement Renderer interface
    RenderTarget& getDefaultRenderTarget() override;
    GeometryBuffer& createGeometryBuffer() override;
    void destroyGeometryBuffer(const GeometryBuffer& buffer) override;
    void destroyAllGeometryBuffers() override;
    TextureTarget* createTextureTarget() override;
    void destroyTextureTarget(TextureTarget* target) override;
    void destroyAllTextureTargets() override;
    Texture& createTexture(const String& name) override;
    Texture& createTexture(const String& name,
                           const String& filename,
                           const String& resourceGroup) override;
    Texture& createTexture(const String& name, const Sizef& size) override;
    void destroyTexture(Texture& texture) override;
    void destroyTexture(const String& name) override;
    void destroyAllTextures() override;
    Texture& getTexture(const String& name) const override;
    bool isTextureDefined(const String& name) const override;
    void setDisplaySize(const Sizef& sz) override;
    const Sizef& getDisplaySize() const override;
    const Vector2f& getDisplayDPI() const override;
    uint getMaxTextureSize() const override;
    const String& getIdentifierString() const override;

    /*!
    \brief
        Helper to return the reference to the pointer to the standard shader of
        the Renderer

    \return
        Reference to the pointer to the standard shader of the Renderer
    */
    DVDShader*& getShaderStandard();

    /*!
    \brief
        Helper to return the attribute location of the position variable in the
        standard shader

    \return
        Attribute location of the position variable in the standard shader
    */
    GLint getShaderStandardPositionLoc();


    /*!
    \brief
        Helper to return the attribute location of the texture coordinate
        variable in the standard shader

    \return
        Attribute location of the texture coordinate variable in the standard
        shader
    */
    GLint getShaderStandardTexCoordLoc();


    /*!
    \brief
        Helper to return the attribute location of the colour variable in the
        standard shader

    \return
        Attribute location of the colour variable in the standard shader
    */
    GLint getShaderStandardColourLoc();

    /*!
    \brief
        Helper to return the uniform location of the matrix variable in the
        standard shader

    \return
        Uniform location of the matrix variable in the standard shader
    */
    GLint getShaderStandardMatrixUniformLoc();

    /*!
    \brief
        Helper to get the wrapper used to check for redundant OpenGL state
        changes.

    \return
        The active OpenGL state change wrapper object.
    */
    StateChangeWrapper* getOpenGLStateChanger();

    /*!
    \brief
        Create a texture that uses an existing OpenGL texture with the specified
        size.  Note that it is your responsibility to ensure that the OpenGL
        texture is valid and that the specified size is accurate.

    \param sz
        Size object that describes the pixel size of the OpenGL texture
        identified by \a tex.

    \param name
        String holding the name for the new texture.  Texture names must be
        unique within the Renderer.

    \return
        Texture object that wraps the OpenGL texture \a tex, and whose size is
        specified to be \a sz.

    \exceptions
        - AlreadyExistsException - thrown if a Texture object named \a name
          already exists within the system.
    */
    Texture& createTexture(const String& name, GLuint tex, const Sizef& sz);

    /*!
    \brief
        Grabs all the loaded textures from Texture RAM and stores them in a
        local data buffer.  This function invalidates all textures, and
        restoreTextures must be called before any CEGUI rendering is done for
        predictable results.
    */
    void grabTextures();

    /*!
    \brief
        Restores all the loaded textures from the local data buffers previously
        created by 'grabTextures'
    */
    void restoreTextures();

    /*!
    \brief
        Utility function that will return \a f if it's a power of two, or the
        next power of two up from \a f if it's not.
    */
    static float getNextPOTSize(float f);


    /*!
    \brief
        Helper to return view projection matrix.

    \return
        The view projection matrix.
    */
    virtual const glm::mat4& getViewProjectionMatrix();

    /*!
    \brief
        Helper to set the view projection matrix.

    \param viewProjectionMatrix
        The view projection matrix.
    */
    virtual void setViewProjectionMatrix(const glm::mat4& viewProjectionMatrix);

    /*!
    \brief
        Helper to get the viewport.

    \return
        The viewport.
    */
    const Rectf& getActiveViewPort();

    /*!
    \brief
        Helper to set the active render target.

    \param renderTarget
        The active RenderTarget.
    */
    void setActiveRenderTarget(RenderTarget* renderTarget);
        
    /*!
    \brief
        Helper to get the active render target.

    \return
        The active RenderTarget.
    */
    RenderTarget* getActiveRenderTarget();

    /*!
    \brief
        Returns if the texture coordinate system is vertically flipped or not. The original of a
        texture coordinate system is typically located either at the the top-left or the bottom-left.
        CEGUI, Direct3D and most rendering engines assume it to be on the top-left. OpenGL assumes it to
        be at the bottom left.        
 
        This function is intended to be used when generating geometry for rendering the TextureTarget
        onto another surface. It is also intended to be used when trying to use a custom texture (RTT)
        inside CEGUI using the Image class, in order to determine the Image coordinates correctly.

    \return
        - true if flipping is required: the texture coordinate origin is at the bottom left
        - false if flipping is not required: the texture coordinate origin is at the top left
    */
    bool isTexCoordSystemFlipped() const { return true; }


    // base class overrides / abstract function implementations
    void beginRendering() override;
    void endRendering() override;

    void setupRenderingBlendMode( BlendMode mode, bool force = false );

    inline Divide::GFXDevice& context() { return _context; }
protected:


    void initialiseOpenGLShaders();

protected:

    DivideRenderer( Divide::GFXDevice& context );

    void init (bool init_glew=false, bool set_glew_experimental=false);

    //! Destructor!
    virtual ~DivideRenderer();

    //! helper to safely log the creation of a named texture
    static void logTextureCreation(const String& name);
    //! helper to safely log the destruction of a named texture
    static void logTextureDestruction(const String& name);

    //! helper to set display size with current viewport size.
    void initialiseDisplaySizeWithViewportSize();

    //! The OpenGL shader we will use usually
    DVDShader* d_shaderStandard{nullptr};
    //! Position variable location inside the shader, for OpenGL
    GLint d_shaderStandardPosLoc;
    //! TexCoord variable location inside the shader, for OpenGL
    GLint d_shaderStandardTexCoordLoc;
    //! Color variable location inside the shader, for OpenGL
    GLint d_shaderStandardColourLoc;
    //! Matrix uniform location inside the shader, for OpenGL
    GLint d_shaderStandardMatrixLoc;
    //! The wrapper we use for OpenGL calls, to detect redundant state changes and prevent them
    StateChangeWrapper* d_openGLStateChanger;
 
    //! String holding the renderer identification text.
    static String d_rendererID;
    //! What the renderer considers to be the current display size.
    Sizef d_displaySize;
    //! What the renderer considers to be the current display DPI resolution.
    Vector2f d_displayDPI;
    //! The default RenderTarget
    RenderTarget* d_defaultTarget;
    //! container type used to hold TextureTargets we create.
    typedef std::vector<TextureTarget*> TextureTargetList;
    //! Container used to track texture targets.
    TextureTargetList d_textureTargets;
    //! container type used to hold GeometryBuffers created.
    typedef std::vector<GeometryBuffer*> GeometryBufferList;
    //! Container used to track geometry buffers.
    GeometryBufferList d_geometryBuffers;
    //! container type used to hold Textures we create.
    typedef std::map<String, OpenGLTexture*, StringFastLessCompare
                     CEGUI_MAP_ALLOC(String, OpenGLTexture*)> TextureMap;
    //! Container used to track textures.
    TextureMap d_textures;

    //! What blend mode we think is active.
    BlendMode d_activeBlendMode;
    //! View projection matrix
    glm::mat4 d_viewProjectionMatrix;
    //! The active RenderTarget
    RenderTarget* d_activeRenderTarget{nullptr};

    Divide::GFXDevice& _context;
};


}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif

