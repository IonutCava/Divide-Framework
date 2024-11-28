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

namespace Divide
{

namespace Attorney
{
    class SceneAnimatorMeshImporter;
};

class Mesh;
class ByteBuffer;
class MeshImporter;
class PlatformContext;

FWD_DECLARE_MANAGED_CLASS( AnimEvaluator );

class SceneAnimator
{
    friend class Attorney::SceneAnimatorMeshImporter;

  public:
    // index = frameIndex; entry = vectorIndex;
    using LineMap = vector<I32>;

    // index = animationID;
    using LineCollection = vector<LineMap>;

   public:
    explicit SceneAnimator(bool useDualQuat);

    ~SceneAnimator();

    /// This must be called to fill the SceneAnimator with valid data
    [[nodiscard]] bool init(PlatformContext& context, Bone_uptr&& skeleton);

    /// Frees all memory and initializes everything to a default state
    void release(bool releaseAnimations);
    void save(PlatformContext& context, ByteBuffer& dataOut) const;
    void load(PlatformContext& context, ByteBuffer& dataIn);

    /// Lets the caller know if there is a skeleton present
    [[nodiscard]] bool hasSkeleton() const noexcept;

    /// This function will adjust the current animations speed by a percentage.
    /// So, passing 100, would do nothing, passing 50, would decrease the speed
    /// by half, and 150 increase it by 50%
    void adjustAnimationSpeedBy(const U32 animationIndex, const D64 percent);
    /// This will set the animation speed
    void adjustAnimationSpeedTo(const U32 animationIndex, const D64 ticksPerSecond);

    /// Get the animation speed... in ticks per second
    [[nodiscard]] D64 animationSpeed(const U32 animationIndex) const;

    /// Get the transforms needed to pass to the vertex shader.
    /// This will wrap the dt value passed, so it is safe to pass 50000000 as a valid number
    [[nodiscard]] AnimEvaluator::FrameIndex frameIndexForTimeStamp(const U32 animationIndex, const D64 dt, const bool forward) const;

    [[nodiscard]] const BoneMatrices& transformMatrices(const U32 animationIndex, const U32 index) const;

    [[nodiscard]] const AnimEvaluator& animationByIndex(const U32 animationIndex) const;

    [[nodiscard]] AnimEvaluator& animationByIndex(const U32 animationIndex);

    [[nodiscard]] U32 frameCount(const U32 animationIndex) const;

    [[nodiscard]] const vector<std::unique_ptr<AnimEvaluator>>& animations() const noexcept;

    [[nodiscard]] const string& animationName(const U32 animationIndex) const;

    [[nodiscard]] U32 animationID(const string& animationName);
    
    /// GetBoneTransform will return the matrix of the bone given its name and the time.
    /// Be careful with this to make sure and send the correct dt. If the dt is
    /// different from what the model is currently at, the transform will be off
    [[nodiscard]] const mat4<F32>& boneTransform(const U32 animationIndex, const D64 dt, const bool forward, const U64 boneNameHash);

    /// Same as above, except takes the index
    [[nodiscard]] const mat4<F32>& boneTransform(const U32 animationIndex, const D64 dt, const bool forward, const U8 bIndex);

    /// Get the bone's global transform
    [[nodiscard]] const mat4<F32>& boneOffsetTransform(const U64 boneNameHash);

    [[nodiscard]] Bone* boneByNameHash(U64 nameHash) const;
    /// GetBoneIndex will return the index of the bone given its name.
    /// The index can be used to index directly into the vector returned from GetTransform
    [[nodiscard]] U8 boneIndexByNameHash(U64 nameHash) const;
    [[nodiscard]] const vector<Line>& skeletonLines(U32 animationIndex, D64 dt, bool forward);

    /// Returns the frame count of the longest registered animation
    [[nodiscard]] U32 getMaxAnimationFrames() const noexcept;

    [[nodiscard]] U8 boneCount() const noexcept;

    PROPERTY_R(bool, useDualQuaternion, true);

   private:
    bool init(PlatformContext& context);
    void buildBuffers(GFXDevice& gfxDevice);

    /// I/O operations
    void  saveSkeleton(ByteBuffer& dataOut, const Bone& parentIn) const;
    [[nodiscard]] Bone* loadSkeleton(ByteBuffer& dataIn,  Bone* parentIn);

    void calculate(U32 animationIndex, D64 pTime);

   private:
    /// Frame count of the longest registered animation
    U32 _maximumAnimationFrames = 0u;
    /// Root node of the internal scene structure
    Bone_uptr _skeleton = nullptr;
    U8        _skeletonDepthCache = 0u;
    /// A vector that holds each animation
    vector<AnimEvaluator_uptr> _animations;
    /// find animations quickly
    hashMap<U64, U32> _animationNameToID;
    mat4<F32> _boneTransformCache;
    LineCollection _skeletonLines;
    vector<vector<Line>> _skeletonLinesContainer;
};

namespace Attorney
{
    class SceneAnimatorMeshImporter
    {
        /// PASS OWNERSHIP OF ANIMATIONS TO THE ANIMATOR!!!
        static void registerAnimations(Divide::SceneAnimator& animator, vector<std::unique_ptr<AnimEvaluator>>& animations)
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

        static void buildBuffers(Divide::SceneAnimator& animator, GFXDevice& gfxDevice)
        {
            animator.buildBuffers(gfxDevice);
        }

        friend class Divide::Mesh;
        friend class Divide::MeshImporter;
    };
};

};  // namespace Divide

#endif // SCENE_ANIMATOR_H_

#include "SceneAnimator.inl"
