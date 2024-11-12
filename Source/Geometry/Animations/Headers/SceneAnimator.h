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

/*Code references:
    http://nolimitsdesigns.com/game-design/open-asset-import-library-animation-loader/
*/

#pragma once
#ifndef SCENE_ANIMATOR_H_
#define SCENE_ANIMATOR_H_

#include "AnimationEvaluator.h"
#include "Core/Math/Headers/Line.h"

struct aiMesh;
struct aiNode;
struct aiScene;

namespace Divide {

namespace Attorney {
    class SceneAnimatorMeshImporter;
};

/// Calculates the global transformation matrix for the given internal node
void CalculateBoneToWorldTransform(Bone* pInternalNode) noexcept;

class Mesh;
class ByteBuffer;
class MeshImporter;
class PlatformContext;

class AnimEvaluator;

class SceneAnimator {
    friend class Attorney::SceneAnimatorMeshImporter;
   public:
    ~SceneAnimator();
    // index = frameIndex; entry = vectorIndex;
    using LineMap = vector<I32>;
    // index = animationID;
    using LineCollection = vector<LineMap>;

    /// This must be called to fill the SceneAnimator with valid data
    /// PASS OWNERSHIP OF SKELETON (and bones) TO THE ANIMATOR!!!
    bool init(PlatformContext& context, Bone* skeleton, const vector<Bone*>& bones);
    /// Frees all memory and initializes everything to a default state
    void release(bool releaseAnimations);
    void save(PlatformContext& context, ByteBuffer& dataOut) const;
    void load(PlatformContext& context, ByteBuffer& dataIn);
    /// Lets the caller know if there is a skeleton present
    bool hasSkeleton() const noexcept { return _skeleton != nullptr; }
    /// The next two functions are good if you want to change the direction of
    /// the current animation.
    /// You could use a forward walking animation and reverse it to get a
    /// walking backwards
    inline void playAnimationForward(const U32 animationIndex)
    {
        assert(animationIndex < _animations.size());

        _animations[animationIndex]->playAnimationForward(true);
    }

    void playAnimationBackward(const U32 animationIndex)
    {
        assert( animationIndex < _animations.size() );
        _animations[animationIndex]->playAnimationForward(false);
    }
    /// This function will adjust the current animations speed by a percentage.
    /// So, passing 100, would do nothing, passing 50, would decrease the speed
    /// by half, and 150 increase it by 50%
    inline void adjustAnimationSpeedBy(const U32 animationIndex, const D64 percent)
    {
        assert( animationIndex < _animations.size() );
        _animations[animationIndex]->ticksPerSecond( animationSpeed(animationIndex) * (percent / 100.0));
    }
    /// This will set the animation speed
    inline void adjustAnimationSpeedTo(const U32 animationIndex, const D64 ticksPerSecond)
    {
        assert( animationIndex < _animations.size() );
        _animations[animationIndex]->ticksPerSecond(ticksPerSecond);
    }

    /// Get the animation speed... in ticks per second
    inline D64 animationSpeed(const U32 animationIndex) const
    {
        assert( animationIndex < _animations.size() );
        return _animations[animationIndex]->ticksPerSecond();
    }

    /// Get the transforms needed to pass to the vertex shader.
    /// This will wrap the dt value passed, so it is safe to pass 50000000 as a valid number
    inline AnimEvaluator::FrameIndex frameIndexForTimeStamp(const U32 animationIndex, const D64 dt) const
    {
        assert( animationIndex < _animations.size() );
        return _animations[animationIndex]->frameIndexAt(dt);
    }

    inline const BoneTransform& transforms(const U32 animationIndex, const U32 index) const
    {
        assert( animationIndex < _animations.size() );
        return _animations[animationIndex]->transforms(index);
    }

    inline const AnimEvaluator& animationByIndex(const U32 animationIndex) const
    {
        assert( animationIndex < _animations.size() );

        const AnimEvaluator* animation = _animations[animationIndex].get();
        assert(animation != nullptr);
        return *animation;
    }

    inline AnimEvaluator& animationByIndex(const U32 animationIndex)
    {
        assert( animationIndex < _animations.size() );

        AnimEvaluator* animation = _animations[animationIndex].get();
        assert(animation != nullptr);
        return *animation;
    }

    inline U32 frameCount(const U32 animationIndex) const
    {
        assert( animationIndex < _animations.size() );
        return _animations[animationIndex]->frameCount();
    }

    inline const vector<std::unique_ptr<AnimEvaluator>>& animations() const noexcept
    {
        return _animations;
    }

    inline const string& animationName(const U32 animationIndex) const
    {
        assert( animationIndex < _animations.size() );

        return _animations[animationIndex]->name();
    }

    U32 animationID(const string& animationName)
    {
        const hashMap<U64, U32>::iterator itr = _animationNameToID.find(_ID(animationName.c_str()));
        if (itr != std::end(_animationNameToID))
        {
            return itr->second;
        }

        return U32_MAX;
    }
    /// GetBoneTransform will return the matrix of the bone given its name and the time.
    /// Be careful with this to make sure and send the correct dt. If the dt is
    /// different from what the model is currently at, the transform will be off
    inline const mat4<F32>& boneTransform(const U32 animationIndex, const D64 dt, const string& bName)
    {
        const I32 boneID = boneIndex(bName);
        if (boneID != -1)
        {
            return boneTransform(animationIndex, dt, boneID);
        }

        _boneTransformCache.identity();
        return _boneTransformCache;
    }

    /// Same as above, except takes the index
    inline const mat4<F32>& boneTransform(const U32 animationIndex, const D64 dt, const I32 bIndex)
    {
        if (bIndex != -1)
        {
            assert( animationIndex < _animations.size() );
            return _animations[animationIndex]->transforms(dt).matrices()[bIndex];
        }

        _boneTransformCache.identity();
        return _boneTransformCache;
    }

    /// Get the bone's global transform
    inline const mat4<F32>& boneOffsetTransform(const string& bName)
    {
        const Bone* bone = boneByName(bName);
        if (bone != nullptr)
        {
            _boneTransformCache = bone->offsetMatrix();
        }

        return _boneTransformCache;
    }

    Bone* boneByName(const string& name) const;
    /// GetBoneIndex will return the index of the bone given its name.
    /// The index can be used to index directly into the vector returned from
    /// GetTransform
    I32 boneIndex(const string& bName) const;
    const vector<Line>& skeletonLines(U32 animationIndex, D64 dt);

    /// Returns the frame count of the longest registered animation
    inline U32 getMaxAnimationFrames() const noexcept
    {
        return _maximumAnimationFrames;
    }

    inline U8 boneCount() const noexcept
    {
        return _skeletonDepthCache > -1 ? to_U8(_skeletonDepthCache) : U8_ZERO;
    }

   private:
    bool init(PlatformContext& context);
    void buildBuffers(GFXDevice& gfxDevice);

    /// I/O operations
    void saveSkeleton(ByteBuffer& dataOut, Bone* parent) const;
    Bone* loadSkeleton(ByteBuffer& dataIn, Bone* parent);

    static void UpdateTransforms(Bone* pNode);
    void calculate(U32 animationIndex, D64 pTime);
    static I32 CreateSkeleton(Bone* piNode,
                              const mat4<F32>& parent,
                              vector<Line>& lines);

   private:
    /// Frame count of the longest registered animation
    U32 _maximumAnimationFrames = 0u;
    /// Root node of the internal scene structure
    Bone* _skeleton = nullptr;
    I16   _skeletonDepthCache = -1;
    vector<Bone*> _bones;
    /// A vector that holds each animation
    vector<std::unique_ptr<AnimEvaluator>> _animations;
    /// find animations quickly
    hashMap<U64, U32> _animationNameToID;
    mat4<F32> _boneTransformCache;
    LineCollection _skeletonLines;
    vector<vector<Line>> _skeletonLinesContainer;
};

namespace Attorney {
    class SceneAnimatorMeshImporter {
        /// PASS OWNERSHIP OF ANIMATIONS TO THE ANIMATOR!!!
        static void registerAnimations(SceneAnimator& animator, vector<std::unique_ptr<AnimEvaluator>>& animations)
        {
            const size_t animationCount = animations.size();
            animator._animations.reserve(animationCount);
            for (size_t i = 0; i < animationCount; ++i)
            {
                animator._animations.emplace_back(std::move(animations[i]));
                insert(animator._animationNameToID, _ID(animator._animations[i]->name().c_str()), to_U32(i));
            }
            animations.clear();
        }

        static void buildBuffers(SceneAnimator& animator, GFXDevice& gfxDevice) {
            animator.buildBuffers(gfxDevice);
        }

        friend class Divide::Mesh;
        friend class Divide::MeshImporter;
    };
};

};  // namespace Divide

#endif // SCENE_ANIMATOR_H_
