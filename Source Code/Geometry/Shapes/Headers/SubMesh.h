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
#ifndef _SUB_MESH_H_
#define _SUB_MESH_H_

#include "Object3D.h"

/*
DIVIDE-Engine: 21.10.2010 (Ionut Cava)

A SubMesh is a geometry wrapper used to build a mesh. Just like a mesh, it can
be rendered locally or across
the server or disabled from rendering alltogheter.

Objects created from this class have theyr position in relative space based on
the parent mesh position.
(Same for scale,rotation and so on).

The SubMesh is composed of a VB object that contains vertx,normal and textcoord
data, a vector of materials,
and a name.
*/

#include "Mesh.h"

namespace Divide {

namespace Attorney {
    class SubMeshMesh;
    class SubMeshMeshImporter;
};

class MeshImporter;

class AnimationComponent;
class SubMesh final : public Object3D {
    friend class Attorney::SubMeshMesh;
    friend class Attorney::SubMeshMeshImporter;

    enum class BoundingBoxState : U8 {
        Computing = 0,
        Computed,
        COUNT
    };

    using BoundingBoxPerAnimation = vector<BoundingBox>;
    using BoundingBoxPerAnimationStatus = vector<BoundingBoxState>;

   public:
    explicit SubMesh(GFXDevice& context,
                     ResourceCache* parentCache,
                     size_t descriptorHash,
                     const Str256& name);

    virtual ~SubMesh() = default;

    void postLoad(SceneGraphNode* sgn) override;

    POINTER_R_IW(Mesh, parentMesh, nullptr);
    PROPERTY_RW(U32, id, 0u);

protected:
    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "SubMesh"; }

private:
    void computeBBForAnimation(SceneGraphNode* sgn, I32 animIndex);
    void buildBoundingBoxesForAnim(const Task& parentTask, I32 animationIndex, const AnimationComponent* const animComp);

    void updateBB(I32 animIndex);

protected:
    void onAnimationChange(SceneGraphNode* sgn, I32 newIndex) override;

private:
    /// Build status of bounding boxes for each animation
    mutable SharedMutex _bbStateLock;
    BoundingBoxPerAnimationStatus _boundingBoxesState;

    /// store a map of bounding boxes for every animation. This should be large enough to fit all frames
    mutable SharedMutex _bbLock;
    BoundingBoxPerAnimation _boundingBoxes;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(SubMesh);

namespace Attorney {
class SubMeshMesh {
    static void setParentMesh(SubMesh& subMesh, Mesh* const parentMesh) {
        subMesh.parentMesh(parentMesh);
        subMesh.geometryBuffer(parentMesh->geometryBuffer());
    }

    friend class Divide::Mesh;
};

class SubMeshMeshImporter {
    static void setBoundingBox(SubMesh& subMesh,
                               const vec3<F32>& min,
                               const vec3<F32>& max) noexcept {
        subMesh._boundingBox.set(min, max);
    }

    friend class Divide::MeshImporter;
};
};  // namespace Attorney
};  // namespace Divide

#endif