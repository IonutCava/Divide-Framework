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
#ifndef ANIMATION_EVALUATOR_H_
#define ANIMATION_EVALUATOR_H_

#include "Bone.h"
#include <assimp/anim.h>
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide
{

namespace Attorney
{
    class AnimEvaluatorSceneAnimator;
}

struct AnimationChannel
{
    vector<aiVectorKey> _positionKeys;
    vector<aiQuatKey>   _rotationKeys;
    vector<aiVectorKey> _scalingKeys;
    U64 _nameKey = 0ULL;
    string _name = "";
    /** The number of position keys */
    U32 _numPositionKeys = 0u;
    U32 _numRotationKeys = 0u;
    U32 _numScalingKeys = 0u;
};

struct DualQuaternion
{
    Quaternion<F32> a;
    Quaternion<F32> b;
};

using BoneQuaternions = vector<DualQuaternion>;
using BoneMatrices   = vector<mat4<F32>>;

class GFXDevice;
class ByteBuffer;
class SceneAnimator;
class AnimEvaluator
{
    friend class Attorney::AnimEvaluatorSceneAnimator;

   public:
    struct FrameIndex
    {
        I32 _curr = 0;
        I32 _prev = 0;
        I32 _next = 0;
    };

   public:
    AnimEvaluator() = default;

    explicit AnimEvaluator(const aiAnimation* pAnim, U32 idx) noexcept;

    void evaluate(D64 dt, Bone& skeleton);

    [[nodiscard]] FrameIndex frameIndexAt(D64 elapsedTimeS, bool forward) const noexcept;

    [[nodiscard]]       vector<BoneMatrices>&   transformMatrices()       noexcept;
    [[nodiscard]] const vector<BoneMatrices>&   transformMatrices() const noexcept;
    [[nodiscard]]       vector<BoneQuaternions>& transformQuaternions()    noexcept;
    [[nodiscard]] const vector<BoneQuaternions>& transQuaternions()  const noexcept;

    [[nodiscard]]       BoneMatrices& transformMatrices(const U32 frameIndex);
    [[nodiscard]] const BoneMatrices& transformMatrices(const U32 frameIndex) const;

    [[nodiscard]] BoneMatrices& transformMatrices(const D64 elapsedTime, const bool forward);
    [[nodiscard]] BoneMatrices& transformMatrices(const D64 elapsedTime, const bool forward, I32& resultingFrameIndex);

    [[nodiscard]] const BoneMatrices& transformMatrices(const D64 elapsedTime, const bool forward) const;
    [[nodiscard]] const BoneMatrices& transformMatrices(const D64 elapsedTime, const bool forward, I32& resultingFrameIndex) const;

    [[nodiscard]] bool initBuffers(GFXDevice& context, bool useDualQuaternions);

    static void save(const AnimEvaluator& evaluator, ByteBuffer& dataOut);
    static void load(AnimEvaluator& evaluator, ByteBuffer& dataIn);

    PROPERTY_RW(D64, ticksPerSecond, 0.0);
    PROPERTY_R_IW(D64, duration, 0.0);
    PROPERTY_R_IW(string, name, "");
    PROPERTY_R(bool, hasScaling, false);
    PROPERTY_R(U32, frameCount, 0u);

    [[nodiscard]] inline ShaderBuffer* boneBuffer() const { return _boneBuffer.get(); }

   protected:
    /// Array to return transformations results inside.
    vector<BoneMatrices>   _transformMatrices;
    vector<BoneQuaternions> _transformQuaternions;

    vector<uint3> _lastPositions;
    /// vector that holds all bone channels
    vector<AnimationChannel> _channels;
    /// GPU buffer to hold bone transforms
    ShaderBuffer_uptr _boneBuffer = nullptr;
    D64 _lastTime = 0.0;
};

namespace Attorney
{
    class AnimEvaluatorSceneAnimator
    {
        static void frameCount(AnimEvaluator& animation, const U32 frameCount)
        {
            DIVIDE_ASSERT(frameCount == animation._transformMatrices.size());

            animation._frameCount = frameCount;
        }

        friend class Divide::SceneAnimator;
    };
}

};  // namespace Divide

#endif  //ANIMATION_EVALUATOR_H_

#include "AnimationEvaluator.inl"
