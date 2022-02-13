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
#ifndef _SKINNED_SUBMESH_H_
#define _SKINNED_SUBMESH_H_

#include "SubMesh.h"
#include "Platform/Threading/Headers/Task.h"
namespace Divide {

class AnimationComponent;
class SkinnedSubMesh final : public SubMesh {
    enum class BoundingBoxState : U8 {
        Computing = 0,
        Computed,
        COUNT
    };

    using BoundingBoxPerAnimation = vector<BoundingBox>;
    using BoundingBoxPerAnimationStatus = vector<BoundingBoxState>;

   public:
    explicit SkinnedSubMesh(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const Str256& name);

    void postLoad(SceneGraphNode* sgn) override;

   protected:
    void onAnimationChange(SceneGraphNode* sgn, I32 newIndex) override;
    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "SkinnedSubMesh"; }

   private:
    void computeBBForAnimation(SceneGraphNode* sgn, I32 animIndex);
    void buildBoundingBoxesForAnim(const Task& parentTask,
                                   I32 animationIndex,
                                   const AnimationComponent* const animComp);

    void updateBB(I32 animIndex);

   private:
    std::shared_ptr<SceneAnimator> _parentAnimatorPtr;
    /// Build status of bounding boxes for each animation
    mutable SharedMutex _bbStateLock;
    BoundingBoxPerAnimationStatus _boundingBoxesState;

    /// store a map of bounding boxes for every animation. This should be large enough to fit all frames
    mutable SharedMutex _bbLock;
    BoundingBoxPerAnimation _boundingBoxes;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(SkinnedSubMesh);

};  // namespace Divide

#endif