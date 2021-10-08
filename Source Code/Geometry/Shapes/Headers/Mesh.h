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
#ifndef _MESH_H_
#define _MESH_H_

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

struct aiScene;
namespace Divide {

FWD_DECLARE_MANAGED_CLASS(SubMesh);

class SceneAnimator;
class Mesh final : public Object3D {
   public:
    explicit Mesh(GFXDevice& context,
                  ResourceCache* parentCache,
                  size_t descriptorHash,
                  const Str256& name,
                  const ResourcePath& resourceName,
                  const ResourcePath& resourceLocation);

    virtual ~Mesh() = default;

    void postLoad(SceneGraphNode* sgn) override;

    void setMaterialTpl(const Material_ptr& material) override;

    void addSubMesh(const SubMesh_ptr& subMesh);

    void sceneUpdate(U64 deltaTimeUS,
                     SceneGraphNode* sgn,
                     SceneState& sceneState) override;

    void setAnimator(const std::shared_ptr<SceneAnimator>& animator) noexcept {
        assert(getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED));
        _animator = animator;
    }

    std::shared_ptr<SceneAnimator> getAnimator() const noexcept {
        assert(getObjectFlag(ObjectFlag::OBJECT_FLAG_SKINNED));
        return _animator; 
    }

    const vector<SubMesh_ptr>& subMeshList() const noexcept {
        return _subMeshList;
    }

    void queueRecomputeBB() noexcept { _recomputeBBQueued = true; }

   protected:
    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Mesh"; }

    friend class MeshImporter;
    void postImport();

    void recomputeBB();
   protected:
    bool _visibleToNetwork = true;
    bool _recomputeBBQueued = true;
    U64 _lastTimeStamp = 0ull;
    /// Animation player to animate the mesh if necessary
    std::shared_ptr<SceneAnimator> _animator;
    vector<SubMesh_ptr> _subMeshList;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Mesh);

};  // namespace Divide

#endif