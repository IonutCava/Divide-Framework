#include "stdafx.h"

#include "Headers/SceneAnimator.h"

#include "Utility/Headers/Localization.h"

#include <assimp/anim.h>

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION_EVALUATOR = 1u;
    constexpr U16 BYTE_BUFFER_VERSION_ANIMATOR = 1u;
    constexpr U16 BYTE_BUFFER_VERSION_SKELETON = 1u;

void AnimEvaluator::save(const AnimEvaluator& evaluator, ByteBuffer& dataOut) {
    dataOut << BYTE_BUFFER_VERSION_EVALUATOR;

    // The animation name;
    dataOut << evaluator._name;
    // the duration
    dataOut << evaluator._duration;
    // the number of ticks per second
    dataOut << evaluator._ticksPerSecond;
    // number of animation channels,
    dataOut << static_cast<uint32_t>(evaluator._channels.size());  
    // for each channel
    for (const auto& channel : evaluator._channels) {
        // the channel name
        dataOut << channel._name;
        dataOut << channel._nameKey;
        // the number of position keys
        uint32_t nsize = static_cast<uint32_t>(channel._positionKeys.size());
        dataOut << nsize;
        // for each position key;
        for (size_t i = 0; i < nsize; i++) {
            // position key
            dataOut << channel._positionKeys[i].mTime;
            // position key
            dataOut << channel._positionKeys[i].mValue.x;
            dataOut << channel._positionKeys[i].mValue.y;
            dataOut << channel._positionKeys[i].mValue.z;
        }

        nsize = static_cast<uint32_t>(channel._rotationKeys.size());
        // the number of rotation keys
        dataOut << nsize;
        // for each channel
        for (size_t i = 0; i < nsize; i++) {
            // rotation key
            dataOut << channel._rotationKeys[i].mTime;
            // rotation key
            dataOut << channel._rotationKeys[i].mValue.x;
            dataOut << channel._rotationKeys[i].mValue.y;
            dataOut << channel._rotationKeys[i].mValue.z;
            dataOut << channel._rotationKeys[i].mValue.w;
        }

        nsize = static_cast<uint32_t>(channel._scalingKeys.size());
        // the number of scaling keys
        dataOut << nsize;
        // for each channel
        for (size_t i = 0; i < nsize; i++) {
            // scale key
            dataOut << channel._scalingKeys[i].mTime;
            // scale key
            dataOut << channel._scalingKeys[i].mValue.x;
            dataOut << channel._scalingKeys[i].mValue.y;
            dataOut << channel._scalingKeys[i].mValue.z;
        }
    }
}

void AnimEvaluator::load(AnimEvaluator& evaluator, ByteBuffer& dataIn) {
    Console::d_printfn(Locale::Get(_ID("CREATE_ANIMATION_BEGIN")), evaluator._name.c_str());

    auto tempVer = decltype(BYTE_BUFFER_VERSION_EVALUATOR){0};
    dataIn >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION_EVALUATOR) {
        // the animation name
        dataIn >> evaluator._name;
        // the duration
        dataIn >> evaluator._duration;
        // the number of ticks per second
        dataIn >> evaluator._ticksPerSecond;
        // the number animation channels
        uint32_t nsize = 0;
        dataIn >> nsize;
        evaluator._channels.resize(nsize);
        evaluator._lastPositions.resize(nsize, vec3<U32>());
        // for each channel
        for (AnimationChannel& channel : evaluator._channels) {
            //the channel name
            dataIn >> channel._name;
            dataIn >> channel._nameKey;
            // the number of position keys
            dataIn >> nsize;
            channel._positionKeys.resize(nsize);
            // for each position key
            for (size_t i = 0; i < nsize; i++) {
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
            for (size_t i = 0; i < nsize; i++) {
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
            for (size_t i = 0; i < nsize; i++) {
                aiVectorKey& scale = channel._scalingKeys[i];
                // scale key
                dataIn >> scale.mTime;
                // scale key
                dataIn >> scale.mValue.x;
                dataIn >> scale.mValue.y;
                dataIn >> scale.mValue.z;
            }
        }
        evaluator._lastPositions.resize(evaluator._channels.size(), vec3<U32>());
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void SceneAnimator::save(PlatformContext& context, ByteBuffer& dataOut) const {
    ACKNOWLEDGE_UNUSED(context);

    // first recursively save the skeleton
    if (_skeleton) {
        saveSkeleton(dataOut, _skeleton);
    }

    dataOut << BYTE_BUFFER_VERSION_ANIMATOR;
    dataOut << to_U32(_bones.size());
    for (Bone* bone : _bones) {
        dataOut << bone->name();
    }

    // the number of animations
    const uint32_t nsize = static_cast<uint32_t>(_animations.size());
    dataOut << nsize;

    for (uint32_t i(0); i < nsize; i++) {
        AnimEvaluator::save(*_animations[i], dataOut);
    }
}

void SceneAnimator::load(PlatformContext& context, ByteBuffer& dataIn) {
    // make sure to clear this before writing new data
    release();

    _skeleton = loadSkeleton(dataIn, nullptr);

    auto tempVer = decltype(BYTE_BUFFER_VERSION_ANIMATOR){0};
    dataIn >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION_ANIMATOR) {

        string boneName;
        uint32_t nsize = 0;
        dataIn >> nsize;
        _bones.resize(nsize);
        for (uint32_t i(0); i < nsize; i++) {
            dataIn >> boneName;
            _bones[i] = _skeleton->find(boneName);
        }
        _skeletonDepthCache = to_I16(nsize);
        // the number of animations
        dataIn >> nsize;
        _animations.resize(nsize);

        uint32_t idx = 0;
        for (std::shared_ptr<AnimEvaluator>& anim : _animations) {
            anim = std::make_shared<AnimEvaluator>();
            AnimEvaluator::load(*anim, dataIn);
            // get all the animation names so I can reference them by name and get the correct id
            insert(_animationNameToID, _ID(anim->name().c_str()), idx++);
        }

        init(context);
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void SceneAnimator::saveSkeleton(ByteBuffer& dataOut, Bone* parent) const {
    dataOut << BYTE_BUFFER_VERSION_SKELETON;

    // the name of the bone
    dataOut << parent->name();
    // the bone offsets
    dataOut << parent->_offsetMatrix;
    // original bind pose
    dataOut << parent->_originalLocalTransform;

    // number of children
    const uint32_t nsize = static_cast<uint32_t>(parent->_children.size());
    dataOut << nsize;
    // continue for all children
    for (vector<Bone*>::iterator it = std::begin(parent->_children); it != std::end(parent->_children); ++it) {
        saveSkeleton(dataOut, *it);
    }
}

Bone* SceneAnimator::loadSkeleton(ByteBuffer& dataIn, Bone* parent) {

    auto tempVer = decltype(BYTE_BUFFER_VERSION_SKELETON){0};
    dataIn >> tempVer;
    if (tempVer == BYTE_BUFFER_VERSION_SKELETON) {

        string tempString;
        // create a node
        Bone* internalNode = MemoryManager_NEW Bone();
        // set the parent, in the case this is the root node, it will be null
        internalNode->_parent = parent;
        // the name of the bone
        dataIn >> tempString;
        internalNode->name(tempString);
        // the bone offsets
        dataIn >> internalNode->_offsetMatrix;
        // original bind pose
        dataIn >> internalNode->_originalLocalTransform;

        // a copy saved
        internalNode->_localTransform = internalNode->_originalLocalTransform;
        CalculateBoneToWorldTransform(internalNode);
        // the number of children
        U32 nsize = 0;
        dataIn >> nsize;

        // recursively call this function on all children
        // continue for all child nodes and assign the created internal nodes as our children
        for (U32 a = 0; a < nsize; a++) {
            internalNode->_children.push_back(loadSkeleton(dataIn, internalNode));
        }
        return internalNode;
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }

    return nullptr;
}
};