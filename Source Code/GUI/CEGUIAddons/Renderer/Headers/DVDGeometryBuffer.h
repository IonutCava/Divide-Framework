/***********************************************************************
    created:    Wed, 8th Feb 2012
    author:     Lukas E Meindl (based on code by Paul D Turner)
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
#ifndef _CEGUIDVDGeometryBuffer_h_
#define _CEGUIDVDGeometryBuffer_h_

#include "CEGUI/GeometryBuffer.h"
#include "RendererBase.h"
#include "CEGUI/Rect.h"
#include "CEGUI/Quaternion.h"

namespace CEGUI
{
class DVDShader;
class StateChangeWrapper;
class DivideRenderer;

//! GL3 based implementation of the GeometryBuffer interface.
class DVDGeometryBuffer : public GeometryBuffer
{
public:
    //! Constructor
    DVDGeometryBuffer( DivideRenderer& owner );
    virtual ~DVDGeometryBuffer();

    // implementation of abstract members from GeometryBuffer
    void setTranslation( const Vector3f& t ) override;
    void setRotation( const Quaternion& r ) override;
    void setPivot( const Vector3f& p ) override;
    void setClippingRegion( const Rectf& region ) override;
    void appendVertex( const Vertex& vertex ) override;
    void appendGeometry( const Vertex* vbuff, uint vertex_count ) override;
    void setActiveTexture( Texture* texture ) override;

    Texture* getActiveTexture() const override;
    uint getVertexCount() const override;
    uint getBatchCount() const override;
    void setRenderEffect( RenderEffect* effect ) override;
    RenderEffect* getRenderEffect() override;
    void setClippingActive( bool active ) override;
    bool isClippingActive() const override;

    //! return the GL modelview matrix used for this buffer.
    const glm::mat4& getMatrix() const;

    void initialiseOpenGLBuffers();
    void deinitialiseOpenGLBuffers();
    void updateOpenGLBuffers();

    // implementation/overrides of members from GeometryBuffer
    void draw() const override;
    void reset() override;

protected:
    //! perform batch management operations prior to adding new geometry.
    void performBatchManagement();

    //! update cached matrix
    void updateMatrix() const;

protected:
    //! internal Vertex structure used for GL based geometry.
    struct GLVertex
    {
        float tex[2];
        float colour[4];
        float position[3];
    };

    //! type to track info for per-texture sub batches of geometry
    struct BatchInfo
    {
        uint texture;
        uint vertexCount;
        bool clip;
    };

    //! DivideRenderer that owns the GeometryBuffer.
    DivideRenderer* d_owner{nullptr};
    //! last texture that was set as active
    OpenGLTexture* d_activeTexture{nullptr};
    //! type of container that tracks BatchInfos.
    typedef std::vector<BatchInfo> BatchList;
    //! list of texture batches added to the geometry buffer
    BatchList d_batches;
    //! type of container used to queue the geometry
    typedef std::vector<GLVertex> VertexList;
    //! container where added geometry is stored.
    VertexList d_vertices;
    //! rectangular clip region
    Rectf d_clipRect{0, 0, 0, 0};
    //! whether clipping will be active for the current batch
    bool d_clippingActive{true};
    //! translation vector
    Vector3f d_translation{0.f, 0.f, 0.f};
    //! rotation quaternion
    Quaternion d_rotation{Quaternion::IDENTITY};
    //! pivot point for rotation
    Vector3f d_pivot{0.f, 0.f, 0.f};
    //! RenderEffect that will be used by the GeometryBuffer
    RenderEffect* d_effect{nullptr};
    //! model matrix cache - we use double because gluUnproject takes double
    mutable glm::mat4               d_matrix;
    //! true when d_matrix is valid and up to date
    mutable bool                    d_matrixValid{false};
    //! OpenGL vao used for the vertices
    GLuint d_verticesVAO;
    //! OpenGL vbo containing all vertex data
    GLuint d_verticesVBO;
    //! Reference to the OpenGL shader inside the Renderer, that is used to render all geometry
    DVDShader*& d_shader;
    //! Position variable location inside the shader, for OpenGL
    const GLint d_shaderPosLoc;
    //! TexCoord variable location inside the shader, for OpenGL
    const GLint d_shaderTexCoordLoc;
    //! Color variable location inside the shader, for OpenGL
    const GLint d_shaderColourLoc;
    //! Matrix uniform location inside the shader, for OpenGL
    const GLint d_shaderStandardMatrixLoc;
    //! Pointer to the OpenGL state changer wrapper that was created inside the Renderer
    StateChangeWrapper* d_glStateChanger;
    //! Size of the buffer that is currently in use
    GLuint d_bufferSize{0u};
};

}

#endif

