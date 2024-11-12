

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


#include "Headers/DVDGeometryBuffer.h"
#include "Headers/DVDTexture.h"
#include "Headers/CEGUIRenderer.h"

#include "CEGUI/RenderEffect.h"
#include "CEGUI/Vertex.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"

// Start of CEGUI namespace section
namespace CEGUI
{

//----------------------------------------------------------------------------//
DVDGeometryBuffer::DVDGeometryBuffer( CEGUIRenderer& owner)
    : _owner( &owner )
    , _bufferSize(1 << 8u)
{
    thread_local size_t BUFFER_IDX = 0u;

    _gvd = owner.context().newGVD(Divide::Config::MAX_FRAMES_IN_FLIGHT + 1u, Divide::Util::StringFormat("IMGUI_{}", BUFFER_IDX++).c_str());

    recreateBuffer(nullptr, 0u);

    _sampler._wrapU = Divide::TextureWrap::CLAMP_TO_EDGE;
    _sampler._wrapV = Divide::TextureWrap::CLAMP_TO_EDGE;
    _sampler._wrapW = Divide::TextureWrap::CLAMP_TO_EDGE;
    _sampler._anisotropyLevel = 0u;
}

void DVDGeometryBuffer::draw() const
{
    using namespace Divide;

    const CEGUI::Rectf viewPort = _owner->getActiveViewPort();

    GFX::CommandBuffer* cmdBuffer = _owner->cmdBuffer();
    Divide::Rect<I32>& clipRect = GFX::EnqueueCommand<GFX::SetScissorCommand>( *cmdBuffer )->_rect;

    // setup clip region
    clipRect.offsetX = to_I32( _clipRect.left() );
    clipRect.offsetY = to_I32( viewPort.getHeight() - _clipRect.bottom() );
    clipRect.sizeX = to_I32( _clipRect.getWidth() );
    clipRect.sizeY = to_I32( _clipRect.getHeight() );

    if ( _owner->flipClippingHeight() )
    {
        clipRect.offsetY = to_I32(_clipRect.top());
    }    

    // apply the transformations we need to use.
    if (!_matrixValid)
    {
        updateMatrix();
    }

    // Send ModelViewProjection matrix to shader
    const glm::mat4 modelViewProjectionMatrix = _owner->getViewProjectionMatrix() * _matrix;

    const int pass_count = _effect ? _effect->getPassCount() : 1;

    Divide::U32 pos = 0u;
    Divide::GenericDrawCommand drawCmd{};
    drawCmd._sourceBuffer = _gvd->handle();

    for (int pass = 0; pass < pass_count; ++pass)
    {
        // set up RenderEffect
        if (_effect)
        {
            _effect->performPreRenderFunctions(pass);
        }

        // draw the batches
        for (const BatchInfo& currentBatch : _batches)
        {
            if ( currentBatch.vertexCount == 0u )
            {
                continue;
            }

            _owner->bindDefaultState( currentBatch.clip, d_blendMode, modelViewProjectionMatrix );

            if (currentBatch.texture != Divide::INVALID_HANDLE<Divide::Texture> )
            {
                auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( *cmdBuffer );
                cmd->_usage = DescriptorSetUsage::PER_DRAW;

                DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
                Set( binding._data, currentBatch.texture, _sampler );
            }

            drawCmd._cmd.baseVertex = pos;
            drawCmd._cmd.vertexCount = currentBatch.vertexCount;

            GFX::EnqueueCommand( *cmdBuffer, GFX::DrawCommand{ drawCmd });

            pos += currentBatch.vertexCount;
        }
    }

    // clean up RenderEffect
    if (_effect)
    {
        _effect->performPostRenderFunctions();
    }
}

void DVDGeometryBuffer::appendGeometry(const Vertex* const vbuff, uint vertex_count)
{
    performBatchManagement();

    // update size of current batch
    _batches.back().vertexCount += vertex_count;

    // buffer these vertices
    DVDVertex vd{};
    const Vertex* vs = vbuff;
    for ( uint i = 0u; i < vertex_count; ++i, ++vs )
    {
        // copy vertex info the buffer, converting from CEGUI::Vertex to something directly usable by the rendering API as needed.
        vd.tex[0] = vs->tex_coords.d_x;
        vd.tex[1] = vs->tex_coords.d_y;
        vd.colour[0] = vs->colour_val.getRed();
        vd.colour[1] = vs->colour_val.getGreen();
        vd.colour[2] = vs->colour_val.getBlue();
        vd.colour[3] = vs->colour_val.getAlpha();
        vd.position[0] = vs->position.d_x;
        vd.position[1] = vs->position.d_y;
        vd.position[2] = vs->position.d_z;
        _vertices.push_back( vd );
    }

    updateBuffers();
}

void DVDGeometryBuffer::reset()
{
    _batches.clear();
    _vertices.clear();
    _activeTexture = nullptr;

    updateBuffers();
}

void DVDGeometryBuffer::recreateBuffer( Divide::Byte* initialData, const size_t intialDataSize )
{
    using namespace Divide;

    GenericVertexData::SetBufferParams params = {};
    params._bindConfig = { 0u, 0u };
    params._useRingBuffer = true;
    params._initialData = { initialData, intialDataSize };

    params._bufferParams._elementCount = _bufferSize;
    params._bufferParams._elementSize = sizeof( DVDVertex );
    params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;

    const BufferLock lock = _gvd->setBuffer( params );

    if ( _owner->memCmd() )
    {
        _owner->memCmd()->_bufferLocks.push_back( lock );
    }
}

void DVDGeometryBuffer::updateBuffers()
{
    const Divide::U32 vertexCount = Divide::to_U32(_vertices.size());
    if ( vertexCount > 0u )
    {
        bool needNewBuffer = false;
        if(_bufferSize < vertexCount)
        {
            needNewBuffer = true;
            _bufferSize = vertexCount;
        }

        Divide::Byte* data = (Divide::Byte*)_vertices.data();
        if( needNewBuffer )
        {
            recreateBuffer(data, vertexCount * sizeof( DVDVertex ) );
        }
        else
        {
            _gvd->incQueue();
            _owner->memCmd()->_bufferLocks.push_back(_gvd->updateBuffer(0u, 0u, vertexCount, data));
        }
    }
}

void DVDGeometryBuffer::performBatchManagement()
{
    Divide::Handle<Divide::Texture> tex = _activeTexture ? _activeTexture->getDVDTexture() : Divide::INVALID_HANDLE<Divide::Texture>;

    // create a new batch if there are no batches yet, or if the active texture differs from that used by the current batch.
    if ( _batches.empty() ||
         tex != _batches.back().texture ||
         _clippingActive != _batches.back().clip )
    {
        _batches.emplace_back(BatchInfo
        {
            .texture = tex,
            .vertexCount = 0,
            .clip = _clippingActive
        });
    }
}

const glm::mat4& DVDGeometryBuffer::getMatrix() const
{
    if ( !_matrixValid )
    {
        updateMatrix();
    }

    return _matrix;
}

void DVDGeometryBuffer::updateMatrix() const
{
    glm::mat4& modelMatrix = _matrix;
    modelMatrix = glm::mat4( 1.f );

    const glm::vec3 final_trans( _translation.d_x + _pivot.d_x,
                                 _translation.d_y + _pivot.d_y,
                                 _translation.d_z + _pivot.d_z );

    modelMatrix = glm::translate( modelMatrix, final_trans );

    const glm::quat rotationQuat = glm::quat( _rotation.d_w, _rotation.d_x, _rotation.d_y, _rotation.d_z );
    const glm::mat4 rotation_matrix = glm::mat4_cast( rotationQuat );

    modelMatrix = modelMatrix * rotation_matrix;

    const glm::vec3 transl = glm::vec3( -_pivot.d_x, -_pivot.d_y, -_pivot.d_z );
    const glm::mat4 translMatrix = glm::translate( glm::mat4( 1.f ), transl );
    modelMatrix = modelMatrix * translMatrix;

    _matrixValid = true;
}

} // End of  CEGUI namespace section

