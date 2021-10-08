#include "stdafx.h"

#include "Headers/AnimationComponent.h"

#include "Managers/Headers/SceneManager.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {

bool AnimationComponent::s_globalAnimationState = true;

AnimationComponent::AnimationComponent(SceneGraphNode* parentSGN, PlatformContext& context)
    : BaseComponentType<AnimationComponent, ComponentType::ANIMATION>(parentSGN, context)
{
    EditorComponentField vskelField = {};
    vskelField._name = "Show Skeleton";
    vskelField._data = &_showSkeleton;
    vskelField._type = EditorComponentFieldType::SWITCH_TYPE;
    vskelField._basicType = GFX::PushConstantType::BOOL;
    vskelField._readOnly = false;
    _editorComponent.registerField(MOV(vskelField));

    EditorComponentField playAnimationsField = {};
    playAnimationsField._name = "Play Animations";
    playAnimationsField._data = &_playAnimations;
    playAnimationsField._type = EditorComponentFieldType::SWITCH_TYPE;
    playAnimationsField._basicType = GFX::PushConstantType::BOOL;
    playAnimationsField._readOnly = false;
    _editorComponent.registerField(MOV(playAnimationsField));

    EditorComponentField animationSpeedField = {};
    animationSpeedField._name = "Animation Speed";
    animationSpeedField._data = &_animationSpeed;
    animationSpeedField._type = EditorComponentFieldType::PUSH_TYPE;
    animationSpeedField._basicType = GFX::PushConstantType::FLOAT;
    animationSpeedField._range = { 0.01f, 100.0f };
    animationSpeedField._readOnly = false;
    _editorComponent.registerField(MOV(animationSpeedField));

    EditorComponentField animationFrameIndexInfoField = {};
    animationFrameIndexInfoField._name = "Animation Frame Index";
    animationFrameIndexInfoField._tooltip = " [Curr - Prev - Next]";
    animationFrameIndexInfoField._dataGetter = [this](void* dataOut) { *static_cast<vec3<I32>*>(dataOut) = vec3<I32>{ _frameIndex._curr, _frameIndex._prev, _frameIndex._next }; };
    animationFrameIndexInfoField._type = EditorComponentFieldType::PUSH_TYPE;
    animationFrameIndexInfoField._basicType = GFX::PushConstantType::IVEC3;
    animationFrameIndexInfoField._readOnly = true;
    _editorComponent.registerField(MOV(animationFrameIndexInfoField));


    _editorComponent.onChangedCbk([this](std::string_view field) {
        ACKNOWLEDGE_UNUSED(field);

        RenderingComponent* const rComp = _parentSGN->get<RenderingComponent>();
        if (rComp != nullptr) {
            rComp->toggleRenderOption(RenderingComponent::RenderOptions::RENDER_SKELETON, showSkeleton());
        }
    });
}

void AnimationComponent::resetTimers() noexcept {
    _currentTimeStamp = _parentTimeStamp = 0UL;
    _frameIndex = {};
}

/// Select an animation by name
bool AnimationComponent::playAnimation(const string& name) {
    if (!_animator) {
        return false;
    }

    return playAnimation(_animator->animationID(name));
}

/// Select an animation by index
bool AnimationComponent::playAnimation(const I32 pAnimIndex) {
    if (!_animator) {
        return false;
    }

    if (pAnimIndex >= to_I32(_animator->animations().size())) {
        return false;  // no change, or the animations data is out of bounds
    }

    const I32 oldIndex = _currentAnimIndex;
    _currentAnimIndex = pAnimIndex;  // only set this after the checks for good
                                     // data and the object was actually
                                     // inserted

    if (_currentAnimIndex == -1) {
        _currentAnimIndex = 0;
    }

    resetTimers();

    if (oldIndex != _currentAnimIndex) {
        _parentSGN->getNode<Object3D>().onAnimationChange(_parentSGN, _currentAnimIndex);
        return true;
    }

    return false;
}

/// Select next available animation
bool AnimationComponent::playNextAnimation() {
    if (!_animator) {
        return false;
    }

    const I32 oldIndex = _currentAnimIndex;
    _currentAnimIndex = (_currentAnimIndex + 1 ) % _animator->animations().size();

    resetTimers();

    return oldIndex != _currentAnimIndex;
}

bool AnimationComponent::playPreviousAnimation() {
    if (!_animator) {
        return false;
    }

    const I32 oldIndex = _currentAnimIndex;
    if (_currentAnimIndex == 0) {
        _currentAnimIndex = to_I32(_animator->animations().size());
    }
    --_currentAnimIndex;

    resetTimers();

    return oldIndex != _currentAnimIndex;
}

const vector<Line>& AnimationComponent::skeletonLines() const {
    assert(_animator != nullptr);

    const D64 animTimeStamp = Time::MillisecondsToSeconds<D64>(_currentTimeStamp);
    // update possible animation
    return  _animator->skeletonLines(_currentAnimIndex, animTimeStamp);
}

AnimationComponent::AnimData AnimationComponent::getAnimationData() const {
    AnimData ret = {};

    if (playAnimations()) {
        if (_previousAnimationIndex != -1) {
            const AnimEvaluator& anim = getAnimationByIndex(_previousAnimationIndex);

            ret._boneBufferRange.set(_frameIndex._curr, 1);
            ret._boneBuffer = anim.boneBuffer();
            ret._prevBoneBufferRange.set(_frameIndex._prev, 1);
        }
    }

    return ret;
}

I32 AnimationComponent::frameCount(const U32 animationID) const {
    return _animator != nullptr ? _animator->frameCount(animationID) : -1;
}

U8 AnimationComponent::boneCount() const {
    return _animator != nullptr ? _animator->boneCount() : to_U8(0);
}

bool AnimationComponent::frameTicked() const noexcept {
    return _frameIndex._prev != _frameIndex._curr;
}

AnimEvaluator& AnimationComponent::getAnimationByIndex(const I32 animationID) const {
    assert(_animator != nullptr);
    return _animator->animationByIndex(animationID);
}

} //namespace Divide
