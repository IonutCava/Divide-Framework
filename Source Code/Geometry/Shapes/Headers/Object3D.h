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
#ifndef _OBJECT_3D_H_
#define _OBJECT_3D_H_

#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"

namespace Divide {

class BoundingBox;
enum class RigidBodyShape : U8;

enum class ObjectType : U8 {
    SPHERE_3D,
    BOX_3D,
    QUAD_3D,
    PATCH_3D,
    MESH,
    SUBMESH,
    TERRAIN,
    DECAL,
    COUNT
};

namespace Names {
    static const char* objectType[] = {
        "SPHERE_3D", "BOX_3D", "QUAD_3D", "PATCH_3D", "MESH", "SUBMESH", "TERRAIN", "DECAL", "UNKNOW",
    };
};

static_assert(ArrayCount(Names::objectType) == to_base(ObjectType::COUNT) + 1, "ObjectType name array out of sync!");

FORCE_INLINE constexpr bool IsPrimitive(const ObjectType type) noexcept {
    return type == ObjectType::BOX_3D ||
           type == ObjectType::QUAD_3D ||
           type == ObjectType::PATCH_3D ||
           type == ObjectType::SPHERE_3D;
}

FORCE_INLINE [[nodiscard]] constexpr PrimitiveTopology GetGeometryBufferType(const ObjectType type) noexcept {
    switch (type) {
        case ObjectType::BOX_3D:
        case ObjectType::MESH:
        case ObjectType::SUBMESH: return PrimitiveTopology::TRIANGLES;
    }

    return PrimitiveTopology::TRIANGLE_STRIP;
}

namespace TypeUtil {
    const char* ObjectTypeToString(const ObjectType objectType) noexcept;
    ObjectType StringToObjectType(const string& name);
};

class Object3D : public SceneNode {
   public:

    enum class ObjectFlag : U8 {
        OBJECT_FLAG_NONE = 0,
        OBJECT_FLAG_SKINNED = toBit(1),
        OBJECT_FLAG_NO_VB = toBit(2),
        COUNT = 3
    };

    explicit Object3D(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, ObjectType type, U32 flagMask);
    explicit Object3D(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str256& name, const ResourcePath& resourceName, const ResourcePath& resourceLocation, ObjectType type, ObjectFlag flag);

    virtual ~Object3D() = default;


    void setObjectFlag(const ObjectFlag flag) noexcept {
        SetBit(_geometryFlagMask, flag);
    }

    void clearObjectFlag(const ObjectFlag flag) noexcept {
        ClearBit(_geometryFlagMask, flag);
    }

    bool getObjectFlag(const ObjectFlag flag) const noexcept {
        return BitCompare(_geometryFlagMask, flag);
    }

    U32 getObjectFlagMask() const noexcept {
        return _geometryFlagMask;
    }

    [[nodiscard]] const VertexBuffer_ptr& geometryBuffer();

    inline void geometryBuffer(const VertexBuffer_ptr& vb) { _geometryBuffer = vb; }

    virtual void onAnimationChange([[maybe_unused]] SceneGraphNode* sgn, [[maybe_unused]] I32 newIndex) {}

    /// Use playAnimations() to toggle animation playback for the current object
    /// (and all subobjects) on or off
    virtual void playAnimations(SceneGraphNode* sgn, bool state);

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

    [[nodiscard]] const vector<vec3<U32>>& getTriangles(const U16 partitionID) {
         if (!computeTriangleList(partitionID)) {
             DIVIDE_UNEXPECTED_CALL();
         }
         return _geometryTriangles[partitionID];
    }

    void addTriangles(const U16 partitionID, const vector<vec3<U32>>& triangles) {
        if (partitionID >= _geometryTriangles.size()) {
            _geometryTriangles.resize(to_size(partitionID) + 1);
        }

        _geometryTriangles[partitionID].insert(cend(_geometryTriangles[partitionID]),
                                               cbegin(triangles),
                                               cend(triangles));
    }

    [[nodiscard]] static vector<SceneGraphNode*> filterByType(const vector<SceneGraphNode*>& nodes, ObjectType filter);

    [[nodiscard]] bool saveCache(ByteBuffer& outputBuffer) const override;
    [[nodiscard]] bool loadCache(ByteBuffer& inputBuffer) override;

    void saveToXML(boost::property_tree::ptree& pt) const override;
    void loadFromXML(const boost::property_tree::ptree& pt)  override;

    PROPERTY_RW(bool, geometryDirty, true);
    PROPERTY_R(ObjectType, geometryType, ObjectType::COUNT);

   protected:
    // Create a list of triangles from the vertices + indices lists based on primitive type
    [[nodiscard]] bool computeTriangleList(U16 partitionID, bool force = false);

    virtual void rebuildInternal();

    void buildDrawCommands(SceneGraphNode* sgn, vector_fast<GFX::DrawCommand>& cmdsOut, PrimitiveTopology& topologyOut, AttributeMap& vertexFormatInOut) override;

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Object3D"; }

   protected:
    GFXDevice& _context;
    std::array<U16, 4> _geometryPartitionIDs;
    U32 _geometryFlagMask = 0u;
    /// 3 indices, pointing to position values, that form a triangle in the mesh.
    /// used, for example, for cooking collision meshes
    /// We keep separate triangle lists per partition
    vector<vector<vec3<U32>>> _geometryTriangles;
    VertexBuffer_ptr _geometryBuffer = nullptr;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Object3D);

};  // namespace Divide
#endif