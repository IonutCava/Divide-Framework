

#include "config.h"

#include "Headers/AnimationEvaluator.h"
#include "Headers/AnimationUtils.h"
#include "Core/Headers/ByteBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

#include "Utility/Headers/Localization.h"

#include <assimp/anim.h>

namespace Divide
{
    constexpr U16 BYTE_BUFFER_VERSION_EVALUATOR = 1u;
    constexpr F32 SCALING_TOLERANCE_VALUE = 1e-3;

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

    static const aiVector3f ANIM_UNIT(1.f, 1.f, 1.f);

    for (U32 a = 0; a < pAnim->mNumChannels; a++)
    {
        const aiNodeAnim* srcChannel = pAnim->mChannels[a];
        AnimationChannel& dstChannel = _channels[a];

        dstChannel._name = srcChannel->mNodeName.data;
        dstChannel._nameKey = _ID(dstChannel._name.c_str());

        for (U32 i(0); i < srcChannel->mNumPositionKeys; i++)
        {
            dstChannel._positionKeys.push_back(srcChannel->mPositionKeys[i]);
        }
        for (U32 i(0); i < srcChannel->mNumRotationKeys; i++)
        {
            dstChannel._rotationKeys.push_back(srcChannel->mRotationKeys[i]);
        }
        for (U32 i(0); i < srcChannel->mNumScalingKeys; i++)
        {
            const aiVectorKey scaling = srcChannel->mScalingKeys[i];
            if ( !scaling.mValue.Equal(ANIM_UNIT, SCALING_TOLERANCE_VALUE) )
            {
                dstChannel._scalingKeys.push_back(scaling);
            }
        }

        if (!dstChannel._scalingKeys.empty())
        {
            _hasScaling = true;
        }

        dstChannel._numPositionKeys = srcChannel->mNumPositionKeys;
        dstChannel._numRotationKeys = srcChannel->mNumRotationKeys;
        dstChannel._numScalingKeys = srcChannel->mNumScalingKeys;
    }

    _lastPositions.resize(pAnim->mNumChannels, uint3());
    Console::d_printfn(LOCALE_STR("CREATE_ANIMATION_END"), _name.c_str());
}

bool AnimEvaluator::initBuffers( GFXDevice& context, const bool useDualQuaternions )
{
    DIVIDE_ASSERT(boneBuffer() == nullptr);
    
    DIVIDE_ASSERT((useDualQuaternions && _transformQuaternions.size() == frameCount()) ||
                  _transformMatrices.size() == frameCount(),
                  "AnimEvaluator error: can't create bone buffer at current stage!");


    if ( frameCount() > 0u)
    {
        DIVIDE_ASSERT(_transformMatrices.front().size() <= Config::MAX_BONE_COUNT_PER_NODE, "AnimEvaluator error: Too many bones for current node! Increase MAX_BONE_COUNT_PER_NODE in Config!");

        ShaderBufferDescriptor bufferDescriptor{};
        bufferDescriptor._ringBufferLength = 1;
        Util::StringFormatTo(bufferDescriptor._name, "BONE_BUFFER_{}", name());
        bufferDescriptor._usageType = BufferUsageType::UNBOUND_BUFFER;
        bufferDescriptor._updateFrequency = BufferUpdateFrequency::ONCE;

        if ( useDualQuaternions )
        {
            vector<DualQuaternion> animationData;
            animationData.reserve(frameCount() * _transformQuaternions.size() * 2u);
            for (const BoneQuaternions& boneTransforms : _transformQuaternions)
            {
                animationData.insert(eastl::end(animationData), eastl::begin(boneTransforms), eastl::end(boneTransforms));
            }

            bufferDescriptor._elementSize = sizeof(DualQuaternion);
            bufferDescriptor._elementCount = to_U32(animationData.size());
            bufferDescriptor._initialData = { animationData.data(), animationData.size() * sizeof(DualQuaternion) };
            _boneBuffer = context.newShaderBuffer(bufferDescriptor);
        }
        else
        {
            vector<mat4<F32>> animationData;
            animationData.reserve(frameCount() * _transformMatrices.size());
            for (const BoneMatrices& boneTransforms : _transformMatrices)
            {
                animationData.insert(eastl::end(animationData), eastl::begin(boneTransforms), eastl::end(boneTransforms));
            }

            bufferDescriptor._elementSize = sizeof(mat4<F32>);
            bufferDescriptor._elementCount = to_U32(animationData.size());
            bufferDescriptor._initialData = { animationData.data(), animationData.size() * sizeof(mat4<F32>) };
            _boneBuffer = context.newShaderBuffer(bufferDescriptor);
        }

        return _boneBuffer != nullptr;
    }

    return false;
}

AnimEvaluator::FrameIndex AnimEvaluator::frameIndexAt(const D64 elapsedTimeS, const bool forward) const noexcept
{
    FrameIndex ret = {};

    if (frameCount() > 0u)
    {
        D64 time = 0.0;

        if (duration() > 0.0)
        {
            // get a [0.f ... 1.f) value by allowing the percent to wrap around 1
            time = std::fmod(elapsedTimeS * ticksPerSecond(), duration());
        }

        const D64 percent = time / duration();

        // this will invert the percent so the animation plays backwards
        if (forward)
        {
            ret._curr = std::min(to_I32(frameCount() * percent), to_I32(frameCount() - 1));
            ret._prev = ret._curr > 0 ? ret._curr - 1 : to_I32(frameCount()) - 1;
            ret._next = to_I32((ret._curr + 1) % frameCount());
        }
        else
        {
            ret._curr = std::min(to_I32(frameCount() * ((percent - 1.0f) * -1.0f)), to_I32(frameCount() - 1));
            ret._prev = to_I32((ret._curr + 1) % frameCount());
            ret._next = ret._curr > 0 ? ret._curr - 1 : to_I32(frameCount()) - 1;
        }
    }
    return ret;
}

// ------------------------------------------------------------------------------------------------
// Evaluates the animation tracks for a given time stamp.
void AnimEvaluator::evaluate(const D64 dt, Bone& skeleton)
{
    const D64 pTime = dt * ticksPerSecond();

    D64 time = 0.0f;
    if (duration() > 0.0)
    {
        time = std::fmod(pTime, duration());
    }

    const aiQuaternion presentRotationDefault(1, 0, 0, 0);

    aiVector3D presentPosition(0, 0, 0);
    aiQuaternion presentRotation(1, 0, 0, 0);
    
    // calculate the transformations for each animation channel
    const size_t channelCount = _channels.size();
    for (size_t a = 0u; a < channelCount; ++a)
    {
        const AnimationChannel* channel = &_channels[a];
        Bone* boneNode = skeleton.find(channel->_nameKey);

        if (boneNode == nullptr)
        {
            Console::d_errorfn(LOCALE_STR("ERROR_BONE_FIND"), channel->_name.c_str());
            continue;
        }

        // ******** Position *****
        if (!channel->_positionKeys.empty())
        {
            // Look for present frame number. Search from last position if time
            // is after the last time, else from beginning
            // Should be much quicker than always looking from start for the
            // average use case.
            U32 frame = time >= _lastTime ? _lastPositions[a].x : 0u;
            while (frame < channel->_positionKeys.size() - 1)
            {
                if (time < channel->_positionKeys[frame + 1].mTime)
                {
                    break;
                }
                ++frame;
            }

            // interpolate between this frame's value and next frame's value
            const U32 nextFrame = (frame + 1) % channel->_positionKeys.size();

            const aiVectorKey& key = channel->_positionKeys[frame];
            const aiVectorKey& nextKey = channel->_positionKeys[nextFrame];
            D64 diffTime = nextKey.mTime - key.mTime;
            if (diffTime < 0.0)
            {
                diffTime += _duration;
            }
            if (diffTime > 0)
            {
                const F32 factor = to_F32((time - key.mTime) / diffTime);
                presentPosition = key.mValue + (nextKey.mValue - key.mValue) * factor;
            }
            else
            {
                presentPosition = key.mValue;
            }

            _lastPositions[a].x = frame;
        }
        else
        {
            presentPosition.Set(0.f, 0.f, 0.f);
        }

        // ******** Rotation *********
        if (!channel->_rotationKeys.empty())
        {
            U32 frame = time >= _lastTime ? _lastPositions[a].y : 0;
            while (frame < channel->_rotationKeys.size() - 1)
            {
                if (time < channel->_rotationKeys[frame + 1].mTime)
                {
                    break;
                }

                ++frame;
            }

            // interpolate between this frame's value and next frame's value
            const U32 nextFrame = (frame + 1) % channel->_rotationKeys.size();

            const aiQuatKey& key = channel->_rotationKeys[frame];
            const aiQuatKey& nextKey = channel->_rotationKeys[nextFrame];
            D64 diffTime = nextKey.mTime - key.mTime;
            if (diffTime < 0.0) diffTime += duration();
            if (diffTime > 0)
            {
                const F32 factor = to_F32((time - key.mTime) / diffTime);
                presentRotation = presentRotationDefault;

                aiQuaternion::Interpolate(presentRotation, key.mValue, nextKey.mValue, factor);
            }
            else
            {
                presentRotation = key.mValue;
            }

            _lastPositions[a].y = frame;
        }
        else
        {
            presentRotation = presentRotationDefault;
        }

        aiMatrix4x4 mat(presentRotation.GetMatrix());
        mat.a4 = presentPosition.x;
        mat.b4 = presentPosition.y;
        mat.c4 = presentPosition.z;

        // ******** Scaling **********
        aiVector3D presentScaling(1, 1, 1);
        if (!channel->_scalingKeys.empty())
        {
            U32 frame = time >= _lastTime ? _lastPositions[a].z : 0;
            while (frame < channel->_scalingKeys.size() - 1)
            {
                if (time < channel->_scalingKeys[frame + 1].mTime)
                {
                    break;
                }

                ++frame;
            }

            presentScaling = channel->_scalingKeys[frame].mValue;
            _lastPositions[a].z = frame;

            mat.a1 *= presentScaling.x;
            mat.b1 *= presentScaling.x;
            mat.c1 *= presentScaling.x;
            mat.a2 *= presentScaling.y;
            mat.b2 *= presentScaling.y;
            mat.c2 *= presentScaling.y;
            mat.a3 *= presentScaling.z;
            mat.b3 *= presentScaling.z;
            mat.c3 *= presentScaling.z;
        }

        AnimUtils::TransformMatrix(mat, boneNode->_localTransform);
    }

    _lastTime = time;
}

void AnimEvaluator::save(const AnimEvaluator& evaluator, ByteBuffer& dataOut)
{
    dataOut << BYTE_BUFFER_VERSION_EVALUATOR;

    // The animation name;
    dataOut << evaluator._name;
    // the duration
    dataOut << evaluator._duration;
    // the number of ticks per second
    dataOut << evaluator._ticksPerSecond;
    // number of animation channels,
    dataOut << to_U32(evaluator._channels.size());
    // for each channel
    for (const auto& channel : evaluator._channels)
    {
        // the channel name
        dataOut << channel._name;
        dataOut << channel._nameKey;
        // the number of position keys
        U32 nsize = to_U32(channel._positionKeys.size());
        dataOut << nsize;
        // for each position key;
        for (size_t i = 0u; i < nsize; i++)
        {
            // position key
            dataOut << channel._positionKeys[i].mTime;
            // position key
            dataOut << channel._positionKeys[i].mValue.x;
            dataOut << channel._positionKeys[i].mValue.y;
            dataOut << channel._positionKeys[i].mValue.z;
        }

        nsize = to_U32(channel._rotationKeys.size());
        // the number of rotation keys
        dataOut << nsize;
        // for each channel
        for (size_t i = 0u; i < nsize; i++)
        {
            // rotation key
            dataOut << channel._rotationKeys[i].mTime;
            // rotation key
            dataOut << channel._rotationKeys[i].mValue.x;
            dataOut << channel._rotationKeys[i].mValue.y;
            dataOut << channel._rotationKeys[i].mValue.z;
            dataOut << channel._rotationKeys[i].mValue.w;
        }

        nsize = to_U32(channel._scalingKeys.size());
        // the number of scaling keys
        dataOut << nsize;
        // for each channel
        for (size_t i = 0u; i < nsize; i++)
        {
            // scale key
            dataOut << channel._scalingKeys[i].mTime;
            // scale key
            dataOut << channel._scalingKeys[i].mValue.x;
            dataOut << channel._scalingKeys[i].mValue.y;
            dataOut << channel._scalingKeys[i].mValue.z;
        }
    }
}

void AnimEvaluator::load(AnimEvaluator& evaluator, ByteBuffer& dataIn)
{
    Console::d_printfn(LOCALE_STR("CREATE_ANIMATION_BEGIN"), evaluator._name.c_str());

    auto tempVer = decltype(BYTE_BUFFER_VERSION_EVALUATOR){0};
    dataIn >> tempVer;
    DIVIDE_EXPECTED_CALL(tempVer == BYTE_BUFFER_VERSION_EVALUATOR);

    // the animation name
    dataIn >> evaluator._name;
    // the duration
    dataIn >> evaluator._duration;
    // the number of ticks per second
    dataIn >> evaluator._ticksPerSecond;
    // the number animation channels
    U32 nsize = 0u;
    dataIn >> nsize;
    evaluator._channels.resize(nsize);
    evaluator._lastPositions.resize(nsize, uint3());
    // for each channel
    for (AnimationChannel& channel : evaluator._channels)
    {
        //the channel name
        dataIn >> channel._name;
        dataIn >> channel._nameKey;
        // the number of position keys
        dataIn >> nsize;
        channel._positionKeys.resize(nsize);
        // for each position key
        for (size_t i = 0u; i < nsize; i++)
        {
            aiVectorKey& pos = channel._positionKeys[i];
            // position key
            dataIn >> pos.mTime;
            // position key
            dataIn >> pos.mValue.x;
            dataIn >> pos.mValue.y;
            dataIn >> pos.mValue.z;
        }

        // the number of rotation keys
        dataIn >> nsize;
        channel._rotationKeys.resize(nsize);
        // for each rotation key
        for (size_t i = 0u; i < nsize; i++)
        {
            aiQuatKey& rot = channel._rotationKeys[i];
            // rotation key
            dataIn >> rot.mTime;
            // rotation key
            dataIn >> rot.mValue.x;
            dataIn >> rot.mValue.y;
            dataIn >> rot.mValue.z;
            dataIn >> rot.mValue.w;
        }

        // the number of scaling keys
        dataIn >> nsize;
        if (nsize > 0u)
        {
            evaluator._hasScaling = true;
        }

        channel._scalingKeys.resize(nsize);
        // for each scaling key
        for (size_t i = 0u; i < nsize; i++)
        {
            aiVectorKey& scale = channel._scalingKeys[i];
            // scale key
            dataIn >> scale.mTime;
            // scale key
            dataIn >> scale.mValue.x;
            dataIn >> scale.mValue.y;
            dataIn >> scale.mValue.z;
        }

    }

    evaluator._lastPositions.resize(evaluator._channels.size(), uint3());
}

} //namespace Divide
