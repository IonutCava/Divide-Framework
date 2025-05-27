/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_OBJECT_3D_H_
#define DVD_OBJECT_3D_H_

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"

namespace Divide {

class BoundingBox;
enum class RigidBodyShape : U8;

[[nodiscard]] FORCE_INLINE constexpr PrimitiveTopology GetGeometryBufferType(const SceneNodeType type) noexcept
{
    if ( !Is3DObject( type ) ) [[unlikely]]
    {
        return PrimitiveTopology::COUNT;
    }
    if (type == SceneNodeType::TYPE_BOX_3D ||
        type == SceneNodeType::TYPE_MESH ||
        type == SceneNodeType::TYPE_SUBMESH)
    {
        return PrimitiveTopology::TRIANGLES;
    }

    return PrimitiveTopology::TRIANGLE_STRIP;
}

DEFINE_NODE_BASE_TYPE(Object3D, SceneNodeType::COUNT)
{
   public:

    explicit Object3D( const ResourceDescriptorBase& descriptor, SceneNodeType type);

    void setMaterialTpl( Handle<Material> material) override;

    [[nodiscard]] const VertexBuffer_ptr& geometryBuffer();

    inline void geometryBuffer(const VertexBuffer_ptr& vb) noexcept { _geometryBuffer = vb; }

    [[nodiscard]] U8 getGeometryPartitionCount() const noexcept {
        U8 ret = 0;
        for (auto partition : _geometryPartitionIDs) {
            if (partition == VertexBuffer::INVALID_PARTITION_ID) {
                break;
            }
            ++ret;
        }

        return ret;
    }

    [[nodiscard]] U16 getGeometryPartitionID(const U8 lodIndex) noexcept {
        return _geometryPartitionIDs[std::min(lodIndex, to_U8(_geometryPartitionIDs.size()))];
    }

    void setGeometryPartitionID(const U8 lodIndex, const U16 partitionID) noexcept {
        if (lodIndex < _geometryPartitionIDs.size()) {
            _geometryPartitionIDs[lodIndex] = partitionID;
        }
    }

    [[nodiscard]] const vector<uint3>& getTriangles(const U16 partitionID)
    {
        DIVIDE_EXPECTED_CALL( computeTriangleList(partitionID) );
        return _geometryTriangles[partitionID];
    }

    void addTriangles(const U16 partitionID, const vector<uint3>& triangles)
    {
        if (partitionID >= _geometryTriangles.size())
        {
            _geometryTriangles.resize(to_size(partitionID) + 1);
        }

        _geometryTriangles[partitionID].insert(cend(_geometryTriangles[partitionID]),
                                               cbegin(triangles),
                                               cend(triangles));
    }

    [[nodiscard]] bool saveCache(ByteBuffer& outputBuffer) const override;
    [[nodiscard]] bool loadCache(ByteBuffer& inputBuffer) override;

    void saveToXML(boost::property_tree::ptree& pt) const override;
    void loadFromXML(const boost::property_tree::ptree& pt)  override;

    PROPERTY_RW(bool, geometryDirty, true);
    PROPERTY_R_IW(bool, playAnimationsOverride, false);

   protected:
    // Create a list of triangles from the vertices + indices lists based on primitive type
    [[nodiscard]] bool computeTriangleList(U16 partitionID, bool force = false);

    virtual void rebuildInternal();

    void prepareRender(SceneGraphNode* sgn,
                    RenderingComponent& rComp,
                    RenderPackage& pkg,
                    GFX::MemoryBarrierCommand& postDrawMemCmd,
                    RenderStagePass renderStagePass,
                    const CameraSnapshot& cameraSnapshot,
                    bool refreshData) override;

    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

   protected:
    std::array<U16, 4> _geometryPartitionIDs;
    /// 3 indices, pointing to position values, that form a triangle in the mesh.
    /// used, for example, for cooking collision meshes
    /// We keep separate triangle lists per partition
    vector<vector<uint3>> _geometryTriangles;
    VertexBuffer_ptr _geometryBuffer = nullptr;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Object3D);

};  // namespace Divide

#endif //DVD_OBJECT_3D_H_
