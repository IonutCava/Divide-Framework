

#include "Headers/SceneAnimator.h"

#include "Core/Headers/ByteBuffer.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{
    constexpr U16 BYTE_BUFFER_VERSION_EVALUATOR = 1u;
    constexpr U16 BYTE_BUFFER_VERSION_ANIMATOR = 1u;
    constexpr U16 BYTE_BUFFER_VERSION_SKELETON = 2u;

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
    if (tempVer != BYTE_BUFFER_VERSION_EVALUATOR)
    {
        DIVIDE_UNEXPECTED_CALL();
    }
        
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

void SceneAnimator::save([[maybe_unused]] PlatformContext& context, ByteBuffer& dataOut) const
{
    // first recursively save the skeleton
    if (_skeleton != nullptr)
    {
        saveSkeleton(dataOut, *_skeleton);
    }

    dataOut << BYTE_BUFFER_VERSION_ANIMATOR;

    // the number of animations
    const U32 nsize = to_U32(_animations.size());
    dataOut << nsize;

    for (U32 i = 0u; i < nsize; i++)
    {
        AnimEvaluator::save(*_animations[i], dataOut);
    }
}

void SceneAnimator::load(PlatformContext& context, ByteBuffer& dataIn)
{
    // make sure to clear this before writing new data
    release(true);
    assert(_animations.empty());

    _skeleton.reset( loadSkeleton(dataIn, nullptr) );
    _skeletonDepthCache = to_U8(std::min(_skeleton->hierarchyDepth(), to_size(U8_MAX)));

    auto tempVer = decltype(BYTE_BUFFER_VERSION_ANIMATOR){0};
    dataIn >> tempVer;
    if (tempVer != BYTE_BUFFER_VERSION_ANIMATOR)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    // the number of animations
    U32 nsize = 0u;
    dataIn >> nsize;
    _animations.resize(nsize);

    for (U32 idx = 0u; idx < nsize; ++idx)
    {
        _animations[idx] = std::make_unique<AnimEvaluator>();
        AnimEvaluator::load(*_animations[idx], dataIn);
        // get all the animation names so I can reference them by name and get the correct id
        insert(_animationNameToID, _ID(_animations[idx]->name().c_str()), idx);
    }

    init(context);
}

void SceneAnimator::saveSkeleton(ByteBuffer& dataOut, const Bone& parent) const
{
    dataOut << BYTE_BUFFER_VERSION_SKELETON;

    // the name of the bone
    dataOut << parent.name();
    // the bone offsets
    dataOut << parent._offsetMatrix;
    // original bind pose
    dataOut << parent._localTransform;

    // number of children
    const U32 nsize = to_U32(parent.children().size());
    dataOut << nsize;
    // continue for all children
    for (const Bone_uptr& child : parent.children())
    {
        saveSkeleton(dataOut, *child);
    }
}

Bone* SceneAnimator::loadSkeleton(ByteBuffer& dataIn, Bone* parentIn)
{
    auto tempVer = decltype(BYTE_BUFFER_VERSION_SKELETON){0};
    dataIn >> tempVer;
    if (tempVer != BYTE_BUFFER_VERSION_SKELETON)
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    string tempString;
    // the name of the bone
    dataIn >> tempString;
    // create a node and set the parent, in the case this is the root node, it will be null
    Bone* internalNode = new Bone(tempString, parentIn);
    
    // the bone offsets
    dataIn >> internalNode->_offsetMatrix;

    // original bind pose
    dataIn >> internalNode->_localTransform;

    // the number of children
    U32 nsize = 0u;
    dataIn >> nsize;

    // recursively call this function on all children
    // continue for all child nodes and assign the created internal nodes as our children
    for (U32 a = 0u; a < nsize; a++)
    {
        loadSkeleton(dataIn, internalNode);
    }

    return internalNode;
}

} //namespace Divide
