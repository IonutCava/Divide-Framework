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
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "GL3GeometryBuffer.h"
#include "GL3Renderer.h"
#include "CEGUI/RenderEffect.h"
#include "CEGUI/Vertex.h"
#include "StateChangeWrapper.h"
#include "GlmPimpl.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

// Start of CEGUI namespace section
namespace CEGUI
{
//----------------------------------------------------------------------------//
OpenGL3GeometryBuffer::OpenGL3GeometryBuffer(OpenGL3Renderer& owner) :
    OpenGLGeometryBufferBase(owner),
    d_shader(owner.getShaderStandard()),
    d_shaderPosLoc(owner.getShaderStandardPositionLoc()),
    d_shaderTexCoordLoc(owner.getShaderStandardTexCoordLoc()),
    d_shaderColourLoc(owner.getShaderStandardColourLoc()),
    d_shaderStandardMatrixLoc(owner.getShaderStandardMatrixUniformLoc()),
    d_glStateChanger(owner.getOpenGLStateChanger()),
    d_bufferSize(0)
{
    initialiseOpenGLBuffers();
}

//----------------------------------------------------------------------------//
OpenGL3GeometryBuffer::~OpenGL3GeometryBuffer()
{
    deinitialiseOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::draw() const
{
    const Rectf viewPort = d_owner->getActiveViewPort();

    d_glStateChanger->scissor(static_cast<GLint>(d_clipRect.left()),
              static_cast<GLint>(viewPort.getHeight() - d_clipRect.bottom()),
              static_cast<GLint>(d_clipRect.getWidth()),
              static_cast<GLint>(d_clipRect.getHeight()));

    // apply the transformations we need to use.
    if (!d_matrixValid)
        updateMatrix();

    // Send ModelViewProjection matrix to shader
    glm::mat4 modelViewProjectionMatrix = d_owner->getViewProjectionMatrix()->d_matrix * d_matrix->d_matrix;
    glUniformMatrix4fv(d_shaderStandardMatrixLoc, 1, GL_FALSE, glm::value_ptr(modelViewProjectionMatrix));

    // activate desired blending mode
    d_owner->setupRenderingBlendMode(d_blendMode);

    // Bind our vao
    d_glStateChanger->bindVertexArray(d_verticesVAO);

    const int pass_count = d_effect ? d_effect->getPassCount() : 1;
     size_t pos = 0;
    for (int pass = 0; pass < pass_count; ++pass)
    {
        // set up RenderEffect
        if (d_effect)
            d_effect->performPreRenderFunctions(pass);

        // draw the batches
       
        BatchList::const_iterator i = d_batches.begin();
        for ( ; i != d_batches.end(); ++i)
        {
            const BatchInfo& currentBatch = *i;

            d_glStateChanger->bindDefaultState(currentBatch.clip);

            if (Divide::GL_API::GetStateTracker()->bindTexture(0, currentBatch.texture) == Divide::GLStateTracker::BindResult::FAILED) {
                Divide::DIVIDE_UNEXPECTED_CALL();
            }

            // draw the geometry
            const unsigned int numVertices = currentBatch.vertexCount;
            glDrawArrays(GL_TRIANGLES, (GLint)pos, numVertices);

            pos += numVertices;
        }
    }

    // clean up RenderEffect
    if (d_effect)
        d_effect->performPostRenderFunctions();
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::appendGeometry(const Vertex* const vbuff,
    uint vertex_count)
{
    OpenGLGeometryBufferBase::appendGeometry(vbuff, vertex_count);
    updateOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::reset()
{
    OpenGLGeometryBufferBase::reset();
    updateOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::initialiseOpenGLBuffers()
{
    glCreateVertexArrays(1, &d_verticesVAO);

    // Generate position vbo
    glCreateBuffers(1, &d_verticesVBO);
    glNamedBufferData(d_verticesVBO, 0, nullptr, GL_DYNAMIC_DRAW);
    // This binds and sets up a vbo. The 
    const GLsizei stride = 9 * sizeof(GLfloat);

    if (Divide::GL_API::GetStateTracker()->setActiveVAO(d_verticesVAO) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker()->setActiveBuffer(GL_ARRAY_BUFFER, d_verticesVBO) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker()->setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    glVertexAttribPointer(d_shaderTexCoordLoc, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(d_shaderTexCoordLoc);

    glVertexAttribPointer(d_shaderColourLoc, 4, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(d_shaderColourLoc);

    glVertexAttribPointer(d_shaderPosLoc, 3, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(d_shaderPosLoc);

    // Unbind Vertex Attribute Array (VAO)
    if (Divide::GL_API::GetStateTracker()->setActiveVAO(0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    // Unbind array and element array buffers
    if (Divide::GL_API::GetStateTracker()->setActiveBuffer(GL_ARRAY_BUFFER, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::deinitialiseOpenGLBuffers()
{
    Divide::GL_API::DeleteVAOs(1, &d_verticesVAO);
    Divide::GL_API::DeleteBuffers(1, &d_verticesVBO);
}

//----------------------------------------------------------------------------//
void OpenGL3GeometryBuffer::updateOpenGLBuffers()
{
    bool needNewBuffer = false;
    const unsigned int vertexCount = (unsigned int)d_vertices.size();

    if(d_bufferSize < vertexCount)
    {
        needNewBuffer = true;
        d_bufferSize = vertexCount;
    }

    const GLsizei dataSize = vertexCount * sizeof(GLVertex);

    GLVertex* data;
    if(d_vertices.empty())
        data = nullptr;
    else
        data = &d_vertices[0];

    if(needNewBuffer)
    {
        glInvalidateBufferData(d_verticesVBO);
        glNamedBufferData(d_verticesVBO, dataSize, data, GL_DYNAMIC_DRAW);
    }
    else
    {
        glInvalidateBufferSubData(d_verticesVBO, 0, dataSize);
        glNamedBufferSubData(d_verticesVBO, 0, dataSize, data);
    }
}

//----------------------------------------------------------------------------//

} // End of  CEGUI namespace section

