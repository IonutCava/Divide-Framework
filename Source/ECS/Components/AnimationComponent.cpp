

#include "Headers/AnimationComponent.h"

#include "Managers/Headers/ProjectManager.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Geometry/Animations/Headers/SceneAnimator.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide
{

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

    EditorComponentField playAnimationsReverseField = {};
    playAnimationsReverseField._name = "Play Animations In Reverse";
    playAnimationsReverseField._data = &_playInReverse;
    playAnimationsReverseField._type = EditorComponentFieldType::SWITCH_TYPE;
    playAnimationsReverseField._basicType = PushConstantType::BOOL;
    playAnimationsReverseField._readOnly = false;
    _editorComponent.registerField(MOV(playAnimationsReverseField));

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
    animationFrameIndexInfoField._tooltip = " [Curr - Prev - Next - Total]";
    animationFrameIndexInfoField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) noexcept { *static_cast<int4*>(dataOut) = int4{ _frameIndex._curr, _frameIndex._prev, _frameIndex._next, frameCount() }; };
    animationFrameIndexInfoField._type = EditorComponentFieldType::PUSH_TYPE;
    animationFrameIndexInfoField._basicType = PushConstantType::IVEC4;
    animationFrameIndexInfoField._readOnly = true;
    _editorComponent.registerField(MOV(animationFrameIndexInfoField));

    EditorComponentField resyncTimersField = {};
    resyncTimersField._name = "Resync All Siblings";
    resyncTimersField._range = { to_F32(resyncTimersField._name.length()) * 10, 20.0f };//dimensions
    resyncTimersField._type = EditorComponentFieldType::BUTTON;
    resyncTimersField._readOnly = false; //disabled/enabled
    _editorComponent.registerField(MOV(resyncTimersField));

    _editorComponent.onChangedCbk([this]([[maybe_unused]] std::string_view field)
    {
        if (field == "Show Skeleton")
        {
            if (_parentSGN->HasComponents(ComponentType::RENDERING) )
            {
                _parentSGN->get<RenderingComponent>()->toggleRenderOption(RenderingComponent::RenderOptions::RENDER_SKELETON, showSkeleton());
            }
        }
        else if (field == "Apply animation to entire mesh" ||
                 field == "Play Animations In Reverse" )
        {
            _animationStateChanged = true;
        }
        else if (field == "Resync All Siblings" )
        {
            _resyncAllSiblings = true;
        }
    });

    enabled(false);
}

void AnimationComponent::setAnimator(SceneAnimator* animator)
{
    _animator = animator;
    if ( _animator != nullptr )
    {
        EditorComponentField updateTypeField = {};
        updateTypeField._name = "Animation";
        updateTypeField._range = { 0u, to_U32(_animator->animations().size()) };

        bool found = false;
        for ( EditorComponentField& field : _editorComponent.fields())
        {
            if ( field._name == updateTypeField._name)
            {
                field._range = updateTypeField._range;
                found = true;
                break;
            }
        }

        if (!found)
        {
            updateTypeField._type = EditorComponentFieldType::DROPDOWN_TYPE;
            updateTypeField._readOnly = false;
            updateTypeField._userData = this;
            updateTypeField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data)
            {
                *static_cast<U32*>(dataOut) = animationIndex();
            };

            updateTypeField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) noexcept
            {
                playAnimation(*static_cast<const U32*>(data));
            };

            updateTypeField._displayNameGetter = [&](const U32 index, void* user_data) noexcept
            {
                return static_cast<AnimationComponent*>(user_data)->getAnimationByIndex(index).name().c_str();
            };

            _editorComponent.registerField(MOV(updateTypeField));

            EditorComponentField playAnimationsField = {};
            playAnimationsField._name = "Apply animation to entire mesh";
            playAnimationsField._data = &_applyAnimationChangeToAllMeshes;
            playAnimationsField._type = EditorComponentFieldType::SWITCH_TYPE;
            playAnimationsField._basicType = PushConstantType::BOOL;
            playAnimationsField._readOnly = false;
            _editorComponent.registerField(MOV(playAnimationsField));
        }

    }

    enabled(_animator != nullptr);
}

void AnimationComponent::resetTimers(const D64 parentTimeStamp) noexcept
{
    _currentTimeStamp = -1.0;
    _parentTimeStamp = parentTimeStamp;
    _frameIndex = {};
}

/// Select an animation by name
bool AnimationComponent::playAnimation(const string& name)
{
    if (!_animator)
    {
        return false;
    }

    return playAnimation(_animator->animationID(name));
}

/// Select an animation by index
bool AnimationComponent::playAnimation(U32 pAnimIndex)
{
    if (!_animator || _animator->animations().empty())
    {
        return false;
    }

    if (pAnimIndex >= _animator->animations().size() )
    {
        pAnimIndex = _animator->animations().size() - 1u;
    }

    const U32 oldIndex = animationIndex();
    if ( oldIndex == pAnimIndex )
    {
        return false;
    }

    _animationIndex = pAnimIndex;  // only set this after the checks for good data and the object was actually inserted
    resetTimers(0.0);
    _animationStateChanged = true;
    return true;
}

/// Select next available animation
bool AnimationComponent::playNextAnimation() noexcept
{
    return playAnimation((_animationIndex + 1u) % _animator->animations().size());
}

bool AnimationComponent::playPreviousAnimation() noexcept
{
    if (!_animator)
    {
        return false;
    }

    U32 oldIndex = _animationIndex;
    if (oldIndex == 0 || oldIndex == U32_MAX)
    {
        oldIndex = to_I32(_animator->animations().size());
    }

    return playAnimation(--oldIndex);
}

const vector<Line>& AnimationComponent::skeletonLines() const
{
    assert(_animator != nullptr);

    const D64 animTimeStamp = Time::MillisecondsToSeconds<D64>(std::max(_currentTimeStamp, 0.0));
    // update possible animation
    return  _animator->skeletonLines( _animationIndex, animTimeStamp, !_playInReverse);
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
