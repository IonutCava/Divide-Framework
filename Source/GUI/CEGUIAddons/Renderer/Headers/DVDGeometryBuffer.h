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
#include "CEGUI/Rect.h"
#include "CEGUI/Quaternion.h"

#include <glm/mat4x4.hpp>
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide
{
    class Texture;
    FWD_DECLARE_MANAGED_CLASS(GenericVertexData);
}

namespace CEGUI
{

class DVDTexture;
class CEGUIRenderer;
class DVDGeometryBuffer final : public GeometryBuffer
{
public:
    struct DVDVertex
    {
        float tex[2];
        float colour[4];
        float position[3];
    };

private:
    //! type to track info for per-texture sub batches of geometry
    struct BatchInfo
    {
        Divide::Texture* texture{ nullptr };
        uint vertexCount{0u};
        bool clip{false};
    };

    //! type of container that tracks BatchInfos.
    using BatchList = Divide::vector_fast<BatchInfo>;
    //! type of container used to queue the geometry
    using VertexList = Divide::vector_fast<DVDVertex>;

public:
    DVDGeometryBuffer( CEGUIRenderer& owner );
    virtual ~DVDGeometryBuffer() = default;

    [[nodiscard]] const glm::mat4& getMatrix() const;

    void updateBuffers();

#pragma region GeometryBuffer Interface
    void draw() const override;
    void reset() override;

    void setTranslation( const Vector3f& t ) override;
    void setRotation( const Quaternion& r ) override;
    void setPivot( const Vector3f& p ) override;
    void setClippingRegion( const Rectf& region ) override;
    void appendVertex( const Vertex& vertex ) override;
    void appendGeometry( const Vertex* vbuff, uint vertex_count ) override;
    void setActiveTexture( Texture* texture ) override;
    void setRenderEffect( RenderEffect* effect ) override;
    void setClippingActive( bool active ) override;

    [[nodiscard]] Texture* getActiveTexture() const override;
    [[nodiscard]] uint getVertexCount() const override;
    [[nodiscard]] uint getBatchCount() const override;
    [[nodiscard]] RenderEffect* getRenderEffect() override;
    [[nodiscard]] bool isClippingActive() const override;
#pragma endregion

protected:
    //! perform batch management operations prior to adding new geometry.
    void performBatchManagement();
    //! update cached matrix
    void updateMatrix() const;
    //! recreates the Divide specific geometry buffer. Usually called if "initialDataSize" is larger than the current buffer size
    void recreateBuffer(Divide::Byte* initialData, size_t intialDataSize);

protected:
    //! CEGUIRenderer that owns the GeometryBuffer.
    CEGUIRenderer* _owner{nullptr};
    //! last texture that was set as active
    DVDTexture* _activeTexture{nullptr};
    //! list of texture batches added to the geometry buffer
    BatchList _batches;
    //! container where added geometry is stored.
    VertexList _vertices;
    //! rectangular clip region
    Rectf _clipRect{0, 0, 0, 0};
    //! whether clipping will be active for the current batch
    bool _clippingActive{true};
    //! translation vector
    Vector3f _translation{0.f, 0.f, 0.f};
    //! rotation quaternion
    Quaternion _rotation{Quaternion::IDENTITY};
    //! pivot point for rotation
    Vector3f _pivot{0.f, 0.f, 0.f};
    //! RenderEffect that will be used by the GeometryBuffer
    RenderEffect* _effect{nullptr};
    //! Size of the buffer that is currently in use
    uint _bufferSize{0u};
    //! Sampler hash to use if the current batch needs a texture bound
    Divide::SamplerDescriptor _sampler{};
    //! Divide specific geometry buffer
    Divide::GenericVertexData_ptr _gvd;
    //! model matrix cache - we use double because gluUnproject takes double
    mutable glm::mat4 _matrix;
    //! true when d_matrix is valid and up to date
    mutable bool _matrixValid{false};
};

}

#endif

#include "DVDGeometryBuffer.inl"
