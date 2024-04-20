

#include "Headers/AnimationComponent.h"

#include "Managers/Headers/ProjectManager.h"
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
    vskelField._basicType = PushConstantType::BOOL;
    vskelField._readOnly = false;
    _editorComponent.registerField(MOV(vskelField));

    EditorComponentField playAnimationsField = {};
    playAnimationsField._name = "Play Animations";
    playAnimationsField._data = &_playAnimations;
    playAnimationsField._type = EditorComponentFieldType::SWITCH_TYPE;
    playAnimationsField._basicType = PushConstantType::BOOL;
    playAnimationsField._readOnly = false;
    _editorComponent.registerField(MOV(playAnimationsField));

    EditorComponentField animationSpeedField = {};
    animationSpeedField._name = "Animation Speed";
    animationSpeedField._data = &_animationSpeed;
    animationSpeedField._type = EditorComponentFieldType::PUSH_TYPE;
    animationSpeedField._basicType = PushConstantType::FLOAT;
    animationSpeedField._range = { 0.01f, 100.0f };
    animationSpeedField._readOnly = false;
    _editorComponent.registerField(MOV(animationSpeedField));

    EditorComponentField animationFrameIndexInfoField = {};
    animationFrameIndexInfoField._name = "Animation Frame Index";
    animationFrameIndexInfoField._tooltip = " [Curr - Prev - Next]";
    animationFrameIndexInfoField._dataGetter = [this](void* dataOut) noexcept { *static_cast<vec3<I32>*>(dataOut) = vec3<I32>{ _frameIndex._curr, _frameIndex._prev, _frameIndex._next }; };
    animationFrameIndexInfoField._type = EditorComponentFieldType::PUSH_TYPE;
    animationFrameIndexInfoField._basicType = PushConstantType::IVEC3;
    animationFrameIndexInfoField._readOnly = true;
    _editorComponent.registerField(MOV(animationFrameIndexInfoField));


    _editorComponent.onChangedCbk([this]([[maybe_unused]] std::string_view field) {
        if (_parentSGN->HasComponents(ComponentType::RENDERING)) {
            _parentSGN->get<RenderingComponent>()->toggleRenderOption(RenderingComponent::RenderOptions::RENDER_SKELETON, showSkeleton());
        }
    });
}

void AnimationComponent::resetTimers() noexcept {
    _currentTimeStamp = -1.0;
    _parentTimeStamp = 0.0;
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
bool AnimationComponent::playAnimation(const U32 pAnimIndex)
{
    if (!_animator)
    {
        return false;
    }

    if (pAnimIndex >= _animator->animations().size() && pAnimIndex != U32_MAX)
    {
        return false;  // no change, or the animations data is out of bounds
    }

    const U32 oldIndex = animationIndex();
    _animationIndex = pAnimIndex;  // only set this after the checks for good data and the object was actually inserted

    if ( _animationIndex == U32_MAX)
    {
        _animationIndex = 0;
    }

    resetTimers();

    if (oldIndex != _animationIndex )
    {
        _parentSGN->getNode<Object3D>().onAnimationChange(_parentSGN, _animationIndex );
        return true;
    }

    return false;
}

/// Select next available animation
bool AnimationComponent::playNextAnimation() noexcept
{
    if (!_animator)
    {
        return false;
    }

    const U32 oldIndex = animationIndex();
    if ( _animationIndex == U32_MAX)
    {
        _animationIndex = 0u;
    }

    _animationIndex = (_animationIndex + 1u) % _animator->animations().size();

    resetTimers();

    return oldIndex != _animationIndex;
}

bool AnimationComponent::playPreviousAnimation() noexcept
{
    if (!_animator)
    {
        return false;
    }

    const U32 oldIndex = _animationIndex;
    if ( _animationIndex == 0 || _animationIndex == U32_MAX)
    {
        _animationIndex = to_I32(_animator->animations().size());
    }

    --_animationIndex;

    resetTimers();

    return oldIndex != _animationIndex;
}

const vector<Line>& AnimationComponent::skeletonLines() const
{
    assert(_animator != nullptr);

    const D64 animTimeStamp = Time::MillisecondsToSeconds<D64>(std::max(_currentTimeStamp, 0.0));
    // update possible animation
    return  _animator->skeletonLines( _animationIndex, animTimeStamp);
}

ShaderBuffer* AnimationComponent::getBoneBuffer() const
{
    const AnimEvaluator& anim = getAnimationByIndex( std::min( _previousAnimationIndex, to_U32(_animator->animations().size()) ) );
    return anim.boneBuffer();
}

I32 AnimationComponent::frameCount(const U32 animationID) const
{
    assert(_animator != nullptr);

    return _animator->frameCount(animationID);
}

U8 AnimationComponent::boneCount() const
{
    assert(_animator != nullptr);

    return  _animator->boneCount();
}

bool AnimationComponent::frameTicked() const noexcept
{
    return _playAnimations ? _frameIndex._prev != _frameIndex._curr : false;
}

AnimEvaluator& AnimationComponent::getAnimationByIndex(const U32 animationID) const
{
    assert(_animator != nullptr && animationID != U32_MAX);

    return _animator->animationByIndex(animationID);
}

} //namespace Divide
