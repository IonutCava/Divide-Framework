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
#ifndef ANIMATION_EVALUATOR_INL_
#define ANIMATION_EVALUATOR_INL_
namespace Divide
{
    inline       vector<BoneMatrices>&    AnimEvaluator::transformMatrices()          noexcept { return _transformMatrices; }
    inline const vector<BoneMatrices>&    AnimEvaluator::transformMatrices()    const noexcept { return _transformMatrices; }
    inline       vector<BoneQuaternions>& AnimEvaluator::transformQuaternions()       noexcept { return _transformQuaternions; }
    inline const vector<BoneQuaternions>& AnimEvaluator::transQuaternions()     const noexcept { return _transformQuaternions; }

    inline BoneMatrices& AnimEvaluator::transformMatrices(const U32 frameIndex)
    {
        assert(frameIndex < to_U32(_transformMatrices.size()));
        return _transformMatrices[frameIndex];
    }

    inline const BoneMatrices& AnimEvaluator::transformMatrices(const U32 frameIndex) const
    {
        assert(frameIndex < to_U32(_transformMatrices.size()));
        return _transformMatrices[frameIndex];
    }

    inline BoneMatrices& AnimEvaluator::transformMatrices(const D64 elapsedTime, const bool forward, I32& resultingFrameIndex)
    {
        resultingFrameIndex = frameIndexAt(elapsedTime, forward)._curr;
        return transformMatrices(to_U32(resultingFrameIndex));
    }

    inline BoneMatrices& AnimEvaluator::transformMatrices(const D64 elapsedTime, const bool forward)
    {
        I32 resultingFrameIndex = 0;
        return transformMatrices(elapsedTime, forward, resultingFrameIndex);
    }

    inline const BoneMatrices& AnimEvaluator::transformMatrices(const D64 elapsedTime, const bool forward, I32& resultingFrameIndex) const
    {
        resultingFrameIndex = frameIndexAt(elapsedTime, forward)._curr;
        return transformMatrices(to_U32(resultingFrameIndex));
    }

    inline const BoneMatrices& AnimEvaluator::transformMatrices(const D64 elapsedTime, const bool forward) const
    {
        I32 resultingFrameIndex = 0;
        return transformMatrices(elapsedTime, forward, resultingFrameIndex);
    }

};  // namespace Divide

#endif  //ANIMATION_EVALUATOR_INL_