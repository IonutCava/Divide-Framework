#include "stdafx.h"

#include "Headers/SceneAnimator.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Headers/AnimationUtils.h"

#include "Core/Headers/PlatformContext.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

/// ------------------------------------------------------------------------------------------------
/// Calculates the global transformation matrix for the given internal node
void CalculateBoneToWorldTransform(Bone* pInternalNode) noexcept {
    pInternalNode->globalTransform(pInternalNode->localTransform());
    Bone* parent = pInternalNode->_parent;
    // This will climb the nodes up along through the parents concatenating all
    // the matrices to get the Object to World transform,
    // or in this case, the Bone To World transform
    while (parent) {
        pInternalNode->globalTransform() *= parent->localTransform();
        // get the parent of the bone we are working on
        parent = parent->_parent;
    }
}

SceneAnimator::~SceneAnimator()
{
    release(true);
}

void SceneAnimator::release(const bool releaseAnimations)
{
    _bones.clear();
    MemoryManager::SAFE_DELETE(_skeleton);

    // this should clean everything up
    _skeletonLines.clear();
    _skeletonLinesContainer.clear();
    _skeletonDepthCache = -2;
    if (releaseAnimations) {
        // clear all animations
        _animationNameToID.clear();
        MemoryManager::DELETE_CONTAINER(_animations);
    }
}

bool SceneAnimator::init(PlatformContext& context) {
    Console::d_printfn(Locale::Get(_ID("LOAD_ANIMATIONS_BEGIN")));

    constexpr D64 timeStep = 1. / ANIMATION_TICKS_PER_SECOND;
    BoneTransform::Container transforms = {};
    

    const size_t animationCount = _animations.size();
    _skeletonLines.resize(animationCount);

    // pre-calculate the animations
    for (size_t i = 0u; i < animationCount; ++i) {
        AnimEvaluator* crtAnimation = _animations[i];
        const D64 duration = crtAnimation->duration();
        const D64 tickStep = crtAnimation->ticksPerSecond() / ANIMATION_TICKS_PER_SECOND;
        D64 dt = 0;
        for (D64 ticks = 0; ticks < duration; ticks += tickStep) {
            dt += timeStep;
            calculate(to_I32(i), dt);
            transforms.resize(_skeletonDepthCache, MAT4_IDENTITY);
            for (I32 a = 0; a < _skeletonDepthCache; ++a) {
                Bone* bone = _bones[a];
                transforms[a] = bone->offsetMatrix() * bone->globalTransform();
                bone->boneID(a);
            }
            crtAnimation->transforms().emplace_back().matrices(transforms);
        }

        _maximumAnimationFrames = std::max(crtAnimation->frameCount(), _maximumAnimationFrames);
        _skeletonLines[i].resize(_maximumAnimationFrames, -1);
    }

    // pay the cost upfront
    for(AnimEvaluator* crtAnimation : _animations) {
        crtAnimation->initBuffers(context.gfx());
    }

     Console::d_printfn(Locale::Get(_ID("LOAD_ANIMATIONS_END")), _skeletonDepthCache);

    return _skeletonDepthCache > 0;
}

/// This will build the skeleton based on the scene passed to it and CLEAR EVERYTHING
bool SceneAnimator::init(PlatformContext& context, Bone* skeleton, const vector<Bone*>& bones) {
    release(false);

    DIVIDE_ASSERT(_bones.size() < U8_MAX, "SceneAnimator::init error: Too many bones for current node!");

    _skeleton = skeleton;
    _bones = bones;
    _skeletonDepthCache = to_I16(bones.size());

    return init(context);
}

// ------------------------------------------------------------------------------------------------
// Calculates the node transformations for the scene.
void SceneAnimator::calculate(const I32 animationIndex, const D64 pTime) {
    assert(_skeleton != nullptr);

    if (animationIndex < 0 || animationIndex >= to_I32(_animations.size())) {
        return;  // invalid animation
    }
    _animations[animationIndex]->evaluate(pTime, _skeleton);
    UpdateTransforms(_skeleton);
}

/// ------------------------------------------------------------------------------------------------
/// Recursively updates the internal node transformations from the given matrix array
void SceneAnimator::UpdateTransforms(Bone* pNode) {
    CalculateBoneToWorldTransform(pNode);  // update global transform as well
    /// continue for all children
    for (Bone* bone : pNode->_children) {
        UpdateTransforms(bone);
    }
}

Bone* SceneAnimator::boneByName(const string& name) const {
    assert(_skeleton != nullptr);

    return _skeleton->find(name);
}

I32 SceneAnimator::boneIndex(const string& bName) const {
    const Bone* bone = boneByName(bName);
    return bone != nullptr ? bone->boneID() : -1;
}

/// Renders the current skeleton pose at time index dt
const vector<Line>& SceneAnimator::skeletonLines(const I32 animationIndex, const D64 dt) {
    const I32 frameIndex = std::max(_animations[animationIndex]->frameIndexAt(dt)._curr - 1, 0);
    I32& vecIndex = _skeletonLines[animationIndex][frameIndex];

    if (vecIndex == -1) {
        vecIndex = to_I32(_skeletonLinesContainer.size());
        _skeletonLinesContainer.emplace_back();
    }

    // create all the needed points
    vector<Line>& lines = _skeletonLinesContainer[vecIndex];
    if (lines.empty()) {
        lines.reserve(boneCount());
        // Construct skeleton
        calculate(animationIndex, dt);
        // Start with identity transform
        CreateSkeleton(_skeleton, MAT4_IDENTITY, lines);
    }

    return lines;
}

/// Create animation skeleton
I32 SceneAnimator::CreateSkeleton(Bone* piNode,
                                  const mat4<F32>& parent,
                                  vector<Line>& lines) {
    static Line s_line = { VECTOR3_ZERO, VECTOR3_UNIT, DefaultColours::RED_U8, DefaultColours::RED_U8, 2.0f, 2.0f };
    const mat4<F32>& me = piNode->globalTransform();

    if (piNode->_parent) {
        Line& line = lines.emplace_back(s_line);
        line.positionStart(parent.getRow(3).xyz);
        line.positionEnd(me.getRow(3).xyz);
    }

    // render all child nodes
    for (const auto& bone : piNode->_children) {
        CreateSkeleton(bone, me, lines);
    }

    return 1;
}
};