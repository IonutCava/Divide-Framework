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


#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "Headers/DVDGeometryBuffer.h"
#include "Headers/OpenGLTexture.h"
#include "Headers/RendererBase.h"
#include "Headers/StateChangeWrapper.h"
#include "CEGUI/RenderEffect.h"
#include "CEGUI/Vertex.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

// Start of CEGUI namespace section
namespace CEGUI
{
//----------------------------------------------------------------------------//
DVDGeometryBuffer::DVDGeometryBuffer( DivideRenderer& owner) :
    d_owner( &owner ),
    d_shader(owner.getShaderStandard()),
    d_shaderPosLoc(owner.getShaderStandardPositionLoc()),
    d_shaderTexCoordLoc(owner.getShaderStandardTexCoordLoc()),
    d_shaderColourLoc(owner.getShaderStandardColourLoc()),
    d_shaderStandardMatrixLoc(owner.getShaderStandardMatrixUniformLoc()),
    d_glStateChanger(owner.getOpenGLStateChanger())
{
    initialiseOpenGLBuffers();
}

//----------------------------------------------------------------------------//
DVDGeometryBuffer::~DVDGeometryBuffer()
{
    deinitialiseOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::draw() const
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
    glm::mat4 modelViewProjectionMatrix = d_owner->getViewProjectionMatrix() * d_matrix;
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

            if (Divide::GL_API::GetStateTracker().bindTexture(0, currentBatch.texture) == Divide::GLStateTracker::BindResult::FAILED) {
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
void DVDGeometryBuffer::appendGeometry(const Vertex* const vbuff, uint vertex_count)
{
    performBatchManagement();

    // update size of current batch
    d_batches.back().vertexCount += vertex_count;

    // buffer these vertices
    GLVertex vd;
    const Vertex* vs = vbuff;
    for ( uint i = 0; i < vertex_count; ++i, ++vs )
    {
        // copy vertex info the buffer, converting from CEGUI::Vertex to
        // something directly usable by OpenGL as needed.
        vd.tex[0] = vs->tex_coords.d_x;
        vd.tex[1] = vs->tex_coords.d_y;
        vd.colour[0] = vs->colour_val.getRed();
        vd.colour[1] = vs->colour_val.getGreen();
        vd.colour[2] = vs->colour_val.getBlue();
        vd.colour[3] = vs->colour_val.getAlpha();
        vd.position[0] = vs->position.d_x;
        vd.position[1] = vs->position.d_y;
        vd.position[2] = vs->position.d_z;
        d_vertices.push_back( vd );
    }

    updateOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::reset()
{
    d_batches.clear();
    d_vertices.clear();
    d_activeTexture = nullptr;

    updateOpenGLBuffers();
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::initialiseOpenGLBuffers()
{
    glCreateVertexArrays(1, &d_verticesVAO);

    // Generate position vbo
    glCreateBuffers(1, &d_verticesVBO);
    glNamedBufferData(d_verticesVBO, 0, nullptr, GL_DYNAMIC_DRAW);
    // This binds and sets up a vbo. The 
    const GLsizei stride = 9 * sizeof(GLfloat);

    if (Divide::GL_API::GetStateTracker().setActiveVAO(d_verticesVAO) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ARRAY_BUFFER, d_verticesVBO) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    glVertexAttribPointer(d_shaderTexCoordLoc, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(d_shaderTexCoordLoc);

    glVertexAttribPointer(d_shaderColourLoc, 4, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(d_shaderColourLoc);

    glVertexAttribPointer(d_shaderPosLoc, 3, GL_FLOAT, GL_FALSE, stride, BUFFER_OFFSET(6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(d_shaderPosLoc);

    // Unbind Vertex Attribute Array (VAO)
    if (Divide::GL_API::GetStateTracker().setActiveVAO(0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    // Unbind array and element array buffers
    if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ARRAY_BUFFER, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::deinitialiseOpenGLBuffers()
{
    Divide::GL_API::DeleteVAOs(1, &d_verticesVAO);
    Divide::GL_API::DeleteBuffers(1, &d_verticesVBO);
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::updateOpenGLBuffers()
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
void DVDGeometryBuffer::appendVertex( const Vertex& vertex )
{
    appendGeometry( &vertex, 1 );
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setTranslation( const Vector3f& v )
{
    d_translation = v;
    d_matrixValid = false;
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setRotation( const Quaternion& r )
{
    d_rotation = r;
    d_matrixValid = false;
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setPivot( const Vector3f& p )
{
    d_pivot = Vector3f( p.d_x, p.d_y, p.d_z );
    d_matrixValid = false;
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setClippingRegion( const Rectf& region )
{
    d_clipRect.top( ceguimax( 0.0f, region.top() ) );
    d_clipRect.left( ceguimax( 0.0f, region.left() ) );
    d_clipRect.bottom( ceguimax( 0.0f, region.bottom() ) );
    d_clipRect.right( ceguimax( 0.0f, region.right() ) );
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setActiveTexture( Texture* texture )
{
    d_activeTexture = static_cast<OpenGLTexture*>(texture);
}

//----------------------------------------------------------------------------//
Texture* DVDGeometryBuffer::getActiveTexture() const
{
    return d_activeTexture;
}

//----------------------------------------------------------------------------//
uint DVDGeometryBuffer::getVertexCount() const
{
    return (uint)d_vertices.size();
}

//----------------------------------------------------------------------------//
uint DVDGeometryBuffer::getBatchCount() const
{
    return (uint)d_batches.size();
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::performBatchManagement()
{
    const GLuint gltex = d_activeTexture ?
        d_activeTexture->getOpenGLTexture() : 0;

    // create a new batch if there are no batches yet, or if the active texture
    // differs from that used by the current batch.
    if ( d_batches.empty() ||
         gltex != d_batches.back().texture ||
         d_clippingActive != d_batches.back().clip )
    {
        const BatchInfo batch = { gltex, 0, d_clippingActive };
        d_batches.push_back( batch );
    }
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setRenderEffect( RenderEffect* effect )
{
    d_effect = effect;
}

//----------------------------------------------------------------------------//
RenderEffect* DVDGeometryBuffer::getRenderEffect()
{
    return d_effect;
}

//----------------------------------------------------------------------------//
const glm::mat4& DVDGeometryBuffer::getMatrix() const
{
    if ( !d_matrixValid )
        updateMatrix();

    return d_matrix;
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::updateMatrix() const
{
    glm::mat4& modelMatrix = d_matrix;
    modelMatrix = glm::mat4( 1.f );

    const glm::vec3 final_trans( d_translation.d_x + d_pivot.d_x,
                                 d_translation.d_y + d_pivot.d_y,
                                 d_translation.d_z + d_pivot.d_z );

    modelMatrix = glm::translate( modelMatrix, final_trans );

    const glm::quat rotationQuat = glm::quat( d_rotation.d_w, d_rotation.d_x, d_rotation.d_y, d_rotation.d_z );
    const glm::mat4 rotation_matrix = glm::mat4_cast( rotationQuat );

    modelMatrix = modelMatrix * rotation_matrix;

    const glm::vec3 transl = glm::vec3( -d_pivot.d_x, -d_pivot.d_y, -d_pivot.d_z );
    const glm::mat4 translMatrix = glm::translate( glm::mat4( 1.f ), transl );
    modelMatrix = modelMatrix * translMatrix;

    d_matrixValid = true;
}

//----------------------------------------------------------------------------//
void DVDGeometryBuffer::setClippingActive( const bool active )
{
    d_clippingActive = active;
}

//----------------------------------------------------------------------------//
bool DVDGeometryBuffer::isClippingActive() const
{
    return d_clippingActive;
}

//----------------------------------------------------------------------------//

} // End of  CEGUI namespace section

