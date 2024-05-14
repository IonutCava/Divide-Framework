/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_ANIMATION_COMPONENT_H_
#define DVD_ANIMATION_COMPONENT_H_

#include "SGNComponent.h"
#include "Core/Math/Headers/Line.h"
#include "Geometry/Animations/Headers/AnimationEvaluator.h"

namespace Divide {

class ShaderBuffer;
class AnimEvaluator;
class SceneGraphNode;

FWD_DECLARE_MANAGED_CLASS(SceneAnimator);
BEGIN_COMPONENT(Animation, ComponentType::ANIMATION)
   public:
    explicit AnimationComponent(SceneGraphNode* parentSGN, PlatformContext& context);

    /// Select an animation by name
    bool playAnimation(const string& name);
    /// Select an animation by index
    bool playAnimation(U32 pAnimIndex);
    /// Select next available animation
    bool playNextAnimation() noexcept;
    /// Select previous available animation
    bool playPreviousAnimation() noexcept;

    [[nodiscard]] I32 frameCount(U32 animationID) const;

    [[nodiscard]] U8 boneCount() const;
    [[nodiscard]] bool frameTicked() const noexcept;

    [[nodiscard]] const vector<Line>& skeletonLines() const;
    [[nodiscard]] ShaderBuffer* getBoneBuffer() const;
    
    [[nodiscard]] AnimEvaluator& getAnimationByIndex(U32 animationID) const;

    void resetTimers() noexcept;

    [[nodiscard]] AnimEvaluator::FrameIndex frameIndex() const noexcept { return _frameIndex; }
    [[nodiscard]] I32 frameCount() const { return frameCount( animationIndex() ); }

    [[nodiscard]] AnimEvaluator& getCurrentAnimation() const { return getAnimationByIndex(animationIndex()); }

    PROPERTY_R(bool, showSkeleton, false);
    PROPERTY_RW(F32, animationSpeed, 1.f);
    /// Pointer to the mesh's animator. Owned by the mesh!
    POINTER_RW(SceneAnimator, animator, nullptr);
    /// Current animation index for the current SGN
    PROPERTY_R(U32, animationIndex, U32_MAX);
    PROPERTY_RW(U32, previousAnimationIndex, U32_MAX);

                  void playAnimations(const bool state)       noexcept { _playAnimations = state;}
    [[nodiscard]] bool playAnimations()                 const noexcept { return _playAnimations && s_globalAnimationState; }

   protected:
    static void GlobalAnimationState(const bool state) noexcept { s_globalAnimationState = state; }
    [[nodiscard]] static bool GlobalAnimationState() noexcept { return s_globalAnimationState; }

   protected:
    AnimEvaluator::FrameIndex _frameIndex = {};
    /// Current animation timestamp for the current SGN
    D64 _currentTimeStamp = -1.0;
    /// Previous animation index
    /// Parent time stamp (e.g. Mesh). 
    /// Should be identical for all nodes of the same level with the same parent
    D64 _parentTimeStamp = 0.0;

    bool _playAnimations = true;

    static bool s_globalAnimationState;
END_COMPONENT(Animation)

}  // namespace Divide

#endif //DVD_ANIMATION_COMPONENT_H_
