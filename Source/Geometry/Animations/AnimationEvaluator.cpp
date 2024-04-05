

#include "config.h"

#include "Headers/AnimationEvaluator.h"
#include "Headers/AnimationUtils.h"
#include "Platform/Video/Headers/GFXDevice.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

#include <assimp/anim.h>

namespace Divide {

// ------------------------------------------------------------------------------------------------
// Constructor on a given animation.
AnimEvaluator::AnimEvaluator(const aiAnimation* pAnim, U32 idx) noexcept 
{
    _lastTime = 0.0;
    ticksPerSecond(!IS_ZERO(pAnim->mTicksPerSecond)
                          ? pAnim->mTicksPerSecond
                          : ANIMATION_TICKS_PER_SECOND);

    duration(pAnim->mDuration);
    name(pAnim->mName.length > 0 ? pAnim->mName.data : Util::StringFormat("unnamed_anim_{}", idx));

    Console::d_printfn(LOCALE_STR("CREATE_ANIMATION_BEGIN"), name().c_str());

    _channels.resize(pAnim->mNumChannels);
    for (U32 a = 0; a < pAnim->mNumChannels; a++) {
        const aiNodeAnim* srcChannel = pAnim->mChannels[a];
        AnimationChannel& dstChannel = _channels[a];

        dstChannel._name = srcChannel->mNodeName.data;
        dstChannel._nameKey = _ID(dstChannel._name.c_str());

        for (U32 i(0); i < srcChannel->mNumPositionKeys; i++) {
            dstChannel._positionKeys.push_back(srcChannel->mPositionKeys[i]);
        }
        for (U32 i(0); i < srcChannel->mNumRotationKeys; i++) {
            dstChannel._rotationKeys.push_back(srcChannel->mRotationKeys[i]);
        }
        for (U32 i(0); i < srcChannel->mNumScalingKeys; i++) {
            dstChannel._scalingKeys.push_back(srcChannel->mScalingKeys[i]);
        }
        dstChannel._numPositionKeys = srcChannel->mNumPositionKeys;
        dstChannel._numRotationKeys = srcChannel->mNumRotationKeys;
        dstChannel._numScalingKeys = srcChannel->mNumScalingKeys;
    }

    _lastPositions.resize(pAnim->mNumChannels, vec3<U32>());

    Console::d_printfn(LOCALE_STR("CREATE_ANIMATION_END"), _name.c_str());
}

bool AnimEvaluator::initBuffers(GFXDevice& context)
{
    DIVIDE_ASSERT(boneBuffer() == nullptr && !_transforms.empty(),
                  "AnimEvaluator error: can't create bone buffer at current stage!");

    DIVIDE_ASSERT(_transforms.size() <= Config::MAX_BONE_COUNT_PER_NODE,
        "AnimEvaluator error: Too many bones for current node! "
        "Increase MAX_BONE_COUNT_PER_NODE in Config!");

    vector<mat4<F32>> animationData;

    animationData.reserve( frameCount() * _transforms.size());
    for (const auto& transform : _transforms)
    {
        for (const mat4<F32>& mat : transform.matrices())
        {
            animationData.push_back(mat);
        }
    }

    if (!animationData.empty())
    {
        ShaderBufferDescriptor bufferDescriptor{};
        bufferDescriptor._ringBufferLength = 1;
        bufferDescriptor._name = Util::StringFormat("BONE_BUFFER_{}", name().c_str());
        bufferDescriptor._bufferParams._elementCount = to_U32(animationData.size());
        bufferDescriptor._bufferParams._elementSize = sizeof(mat4<F32>);
        bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::ONCE;
        bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
        bufferDescriptor._initialData = { animationData.data(), animationData.size() * sizeof(mat4<F32>) };

        _boneBuffer = context.newSB(bufferDescriptor);
        return true;
    }

    return false;
}

AnimEvaluator::FrameIndex AnimEvaluator::frameIndexAt(const D64 elapsedTimeS) const noexcept {
    D64 time = 0.0;
    if (duration() > 0.0) {
        // get a [0.f ... 1.f) value by allowing the percent to wrap around 1
        time = std::fmod(elapsedTimeS * ticksPerSecond(), duration());
    }

    const D64 percent = time / duration();

    FrameIndex ret = {};
    if (!_transforms.empty()) {
        // this will invert the percent so the animation plays backwards
        if (playAnimationForward()) {
            ret._curr = std::min(to_I32(_transforms.size() * percent), to_I32(_transforms.size() - 1));
            ret._prev = ret._curr > 0 ? ret._curr - 1 : to_I32(_transforms.size()) - 1;
            ret._next = (ret._curr + 1) % _transforms.size();
        } else {
            ret._curr = std::min(to_I32(_transforms.size() * ((percent - 1.0f) * -1.0f)), to_I32(_transforms.size() - 1));
            ret._prev = (ret._curr + 1) % _transforms.size();
            ret._next = ret._curr > 0 ? ret._curr - 1 : to_I32(_transforms.size()) - 1;
        }
    }
    return ret;
}

// ------------------------------------------------------------------------------------------------
// Evaluates the animation tracks for a given time stamp.
void AnimEvaluator::evaluate(const D64 dt, Bone* skeleton) {
    const D64 pTime = dt * ticksPerSecond();

    D64 time = 0.0f;
    if (duration() > 0.0) {
        time = std::fmod(pTime, duration());
    }

    const aiQuaternion presentRotationDefault(1, 0, 0, 0);

    aiVector3D presentPosition(0, 0, 0);
    aiQuaternion presentRotation(1, 0, 0, 0);
    aiVector3D presentScaling(1, 1, 1);
    
    // calculate the transformations for each animation channel
    for (U32 a = 0; a < _channels.size(); a++) {
        
        const AnimationChannel* channel = &_channels[a];
        Bone* bonenode = skeleton->find(channel->_nameKey);

        if (bonenode == nullptr) {
            Console::d_errorfn(LOCALE_STR("ERROR_BONE_FIND"), channel->_name.c_str());
            continue;
        }

        // ******** Position *****
        if (!channel->_positionKeys.empty()) {
            // Look for present frame number. Search from last position if time
            // is after the last time, else from beginning
            // Should be much quicker than always looking from start for the
            // average use case.
            U32 frame = time >= _lastTime ? _lastPositions[a].x : 0;
            while (frame < channel->_positionKeys.size() - 1) {
                if (time < channel->_positionKeys[frame + 1].mTime) {
                    break;
                }
                frame++;
            }

            // interpolate between this frame's value and next frame's value
            const U32 nextFrame = (frame + 1) % channel->_positionKeys.size();

            const aiVectorKey& key = channel->_positionKeys[frame];
            const aiVectorKey& nextKey = channel->_positionKeys[nextFrame];
            D64 diffTime = nextKey.mTime - key.mTime;
            if (diffTime < 0.0) diffTime += duration();
            if (diffTime > 0) {
                const F32 factor = to_F32((time - key.mTime) / diffTime);
                presentPosition =
                    key.mValue + (nextKey.mValue - key.mValue) * factor;
            } else {
                presentPosition = key.mValue;
            }
            _lastPositions[a].x = frame;
        } else {
            presentPosition.Set(0.0f, 0.0f, 0.0f);
        }

        // ******** Rotation *********
        if (!channel->_rotationKeys.empty()) {
            U32 frame = time >= _lastTime ? _lastPositions[a].y : 0;
            while (frame < channel->_rotationKeys.size() - 1) {
                if (time < channel->_rotationKeys[frame + 1].mTime) break;
                frame++;
            }

            // interpolate between this frame's value and next frame's value
            const U32 nextFrame = (frame + 1) % channel->_rotationKeys.size();

            const aiQuatKey& key = channel->_rotationKeys[frame];
            const aiQuatKey& nextKey = channel->_rotationKeys[nextFrame];
            D64 diffTime = nextKey.mTime - key.mTime;
            if (diffTime < 0.0) diffTime += duration();
            if (diffTime > 0) {
                const F32 factor = to_F32((time - key.mTime) / diffTime);
                presentRotation = presentRotationDefault;
                aiQuaternion::Interpolate(presentRotation, key.mValue,
                                          nextKey.mValue, factor);
            } else {
                presentRotation = key.mValue;
            }
            _lastPositions[a].y = frame;
        } else {
            presentRotation = presentRotationDefault;
        }

        // ******** Scaling **********
        if (!channel->_scalingKeys.empty()) {
            U32 frame = time >= _lastTime ? _lastPositions[a].z : 0;
            while (frame < channel->_scalingKeys.size() - 1) {
                if (time < channel->_scalingKeys[frame + 1].mTime) break;
                frame++;
            }

            presentScaling = channel->_scalingKeys[frame].mValue;
            _lastPositions[a].z = frame;
        } else {
            presentScaling.Set(1.0f, 1.0f, 1.0f);
        }

        aiMatrix4x4 mat(presentRotation.GetMatrix());
        mat.a1 *= presentScaling.x;
        mat.b1 *= presentScaling.x;
        mat.c1 *= presentScaling.x;
        mat.a2 *= presentScaling.y;
        mat.b2 *= presentScaling.y;
        mat.c2 *= presentScaling.y;
        mat.a3 *= presentScaling.z;
        mat.b3 *= presentScaling.z;
        mat.c3 *= presentScaling.z;
        mat.a4  = presentPosition.x;
        mat.b4  = presentPosition.y;
        mat.c4  = presentPosition.z;
        mat4<F32> out;
        AnimUtils::TransformMatrix(mat, out);
        bonenode->localTransform(out);
    }
    _lastTime = time;
}
};