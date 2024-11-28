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
#ifndef SCENE_ANIMATOR_INL_
#define SCENE_ANIMATOR_INL_

namespace Divide
{

inline bool SceneAnimator::hasSkeleton() const noexcept
{
    return _skeleton != nullptr;
}

inline void SceneAnimator::adjustAnimationSpeedBy(const U32 animationIndex, const D64 percent)
{
    assert(animationIndex < _animations.size());
    _animations[animationIndex]->ticksPerSecond(animationSpeed(animationIndex) * (percent / 100.0));
}

inline void SceneAnimator::adjustAnimationSpeedTo(const U32 animationIndex, const D64 ticksPerSecond)
{
    assert(animationIndex < _animations.size());
    _animations[animationIndex]->ticksPerSecond(ticksPerSecond);
}

inline D64 SceneAnimator::animationSpeed(const U32 animationIndex) const
{
    assert(animationIndex < _animations.size());
    return _animations[animationIndex]->ticksPerSecond();
}

inline AnimEvaluator::FrameIndex SceneAnimator::frameIndexForTimeStamp(const U32 animationIndex, const D64 dt, const bool forward) const
{
    assert(animationIndex < _animations.size());
    return _animations[animationIndex]->frameIndexAt(dt, forward);
}

inline const BoneMatrices& SceneAnimator::transformMatrices(const U32 animationIndex, const U32 index) const
{
    assert(animationIndex < _animations.size());
    return _animations[animationIndex]->transformMatrices(index);
}

inline const AnimEvaluator& SceneAnimator::animationByIndex(const U32 animationIndex) const
{
    assert(animationIndex < _animations.size());

    const AnimEvaluator* animation = _animations[animationIndex].get();
    assert(animation != nullptr);
    return *animation;
}

inline AnimEvaluator& SceneAnimator::animationByIndex(const U32 animationIndex)
{
    assert(animationIndex < _animations.size());

    AnimEvaluator* animation = _animations[animationIndex].get();
    assert(animation != nullptr);
    return *animation;
}

inline U32 SceneAnimator::frameCount(const U32 animationIndex) const
{
    assert(animationIndex < _animations.size());
    return _animations[animationIndex]->frameCount();
}

inline const vector<std::unique_ptr<AnimEvaluator>>& SceneAnimator::animations() const noexcept
{
    return _animations;
}

inline const string& SceneAnimator::animationName(const U32 animationIndex) const
{
    assert(animationIndex < _animations.size());

    return _animations[animationIndex]->name();
}

inline U32 SceneAnimator::animationID(const string& animationName)
{
    const hashMap<U64, U32>::iterator itr = _animationNameToID.find(_ID(animationName.c_str()));
    if (itr != std::end(_animationNameToID))
    {
        return itr->second;
    }

    return U32_MAX;
}

inline const mat4<F32>& SceneAnimator::boneTransform(const U32 animationIndex, const D64 dt, const bool forward, const U64 boneNameHash)
{
    const U8 boneID = boneIndexByNameHash(boneNameHash);
    if (boneID != Bone::INVALID_BONE_IDX)
    {
        return boneTransform(animationIndex, dt, forward, boneID);
    }

    _boneTransformCache.identity();
    return _boneTransformCache;
}

inline const mat4<F32>& SceneAnimator::boneTransform(const U32 animationIndex, const D64 dt, const bool forward, const U8 bIndex)
{
    if (bIndex != Bone::INVALID_BONE_IDX)
    {
        assert(animationIndex < _animations.size());
        return _animations[animationIndex]->transformMatrices(dt, forward)[bIndex];
    }

    _boneTransformCache.identity();
    return _boneTransformCache;
}

inline const mat4<F32>& SceneAnimator::boneOffsetTransform(const U64 boneNameHash)
{
    const Bone* bone = boneByNameHash(boneNameHash);
    if (bone != nullptr)
    {
        _boneTransformCache = bone->_offsetMatrix;
    }

    return _boneTransformCache;
}

inline U32 SceneAnimator::getMaxAnimationFrames() const noexcept
{
    return _maximumAnimationFrames;
}

inline U8 SceneAnimator::boneCount() const noexcept
{
    return to_U8(_skeletonDepthCache);
}

} //namespace Divide

#endif //SCENE_ANIMATOR_INL_