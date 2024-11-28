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
#ifndef DVD_MESH_H_
#define DVD_MESH_H_

/**
DIVIDE-Engine: 21.10.2010 (Ionut Cava)

Mesh class. This class wraps all of the renderable geometry drawn by the engine.

Meshes are composed of at least 1 submesh that contains vertex data, texture
info and so on.
A mesh has a name, position, rotation, scale and a Boolean value that enables or
disables rendering
across the network and one that disables rendering altogether;

Note: all transformations applied to the mesh affect every submesh that compose
the mesh.
*/

#include "Object3D.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"

struct aiScene;
namespace Divide {

FWD_DECLARE_MANAGED_CLASS(SubMesh);
FWD_DECLARE_MANAGED_CLASS(SceneAnimator);

namespace Attorney {
    class MeshImporter;
}

class MeshImporter;

struct MeshNodeData
{
    mat4<F32> _transform;
    vector<U32> _meshIndices; ///<Index into Mesh::MeshData
    vector<MeshNodeData> _children;
    string _name;

    bool serialize(ByteBuffer& dataOut) const;
    bool deserialize(ByteBuffer& dataIn);
};

DEFINE_3D_OBJECT_TYPE(Mesh, SceneNodeType::TYPE_MESH)
{
    friend class Attorney::MeshImporter;

   public:
    struct MeshData
    {
        Handle<SubMesh> _mesh{INVALID_HANDLE<SubMesh>};
        U32 _index{0u};
    };

   public:
    explicit Mesh( const ResourceDescriptor<Mesh>& descriptor );

    void postLoad(SceneGraphNode* sgn) override;
    bool postLoad() override;
    bool load( PlatformContext& context ) override;
    bool unload() override;
    void setMaterialTpl(Handle<Material> material) override;

    void setAnimationCount(size_t count, bool useDualQuaternions);

    [[nodiscard]] SceneAnimator* getAnimator() const noexcept;

    PROPERTY_R(size_t, animationCount, 0u);
    PROPERTY_R(bool, dualQuaternionAnimations, true);

   protected:
    void addSubMesh(Handle<SubMesh> subMesh, U32 index);
    void processNode(SceneGraphNode* rootMeshNode, SceneGraphNode* parentNode, const MeshNodeData& node);
    SceneGraphNode* addSubMeshNode(SceneGraphNode* rootMeshNode, SceneGraphNode* parentNode, const U32 meshIndex);
    void setNodeData(const MeshNodeData& nodeStructure);

   protected:
    U64 _lastTimeStamp = 0ull;
    SceneAnimator_uptr _animator;
    vector<MeshData> _subMeshList;
    MeshNodeData _nodeStructure;
};

namespace Attorney
{
    class MeshImporter
    {
        static void setNodeData(Mesh& parentMesh, const MeshNodeData& nodeStructure)
        {
            parentMesh.setNodeData(nodeStructure);
        }

        static void addSubMesh(Mesh& parentMesh, const Handle<SubMesh> subMesh, const U32 index)
        {
            parentMesh.addSubMesh(subMesh, index);
        }

        friend class Divide::MeshImporter;
    };
} //namespace Attorney

TYPEDEF_SMART_POINTERS_FOR_TYPE(Mesh);

};  // namespace Divide

#endif //DVD_MESH_H_
