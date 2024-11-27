

#include "Headers/Character.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/AnimationComponent.h"

namespace Divide {

Character::Character(const CharacterType type)
    : Unit(UnitType::UNIT_TYPE_CHARACTER),
      _characterType(type)
{
    _positionDirty = false;
    _velocityDirty = false;
    setRelativeLookingDirection(WORLD_Z_NEG_AXIS);
    _newVelocity.reset();
    _curVelocity.reset();
}

void Character::setParentNode(SceneGraphNode* node) {
    Unit::setParentNode(node);
    const TransformComponent* const transform = node->get<TransformComponent>();
    if (transform) {
        _newPosition.set(transform->getWorldPosition());
        _oldPosition.set(_newPosition);
        _curPosition.set(_oldPosition);
        _positionDirty = true;
    }
}

void Character::update(const U64 deltaTimeUS) {
    assert(_node != nullptr);

    if (_positionDirty) {
        _curPosition.lerp(_newPosition,
                          Time::MicrosecondsToSeconds<F32>(deltaTimeUS));
        _positionDirty = false;
    }

    if (_velocityDirty && _newVelocity.length() > 0.0f) {
        _newVelocity.y = 0.0f;
        _newVelocity.z *= -1.0f;
        _newVelocity.normalize();
        _curVelocity.lerp(_newVelocity,
                          Time::MicrosecondsToSeconds<F32>(deltaTimeUS));
        _velocityDirty = false;
    }

    TransformComponent* const nodeTransformComponent = getBoundNode()->get<TransformComponent>();
    
    float3 sourceDirection(getLookingDirection());
    sourceDirection.y = 0.0f;

    _oldPosition.set(nodeTransformComponent->getWorldPosition());
    _oldPosition.lerp(_curPosition, to_F32(GFXDevice::FrameInterpolationFactor()));
    nodeTransformComponent->setPosition(_oldPosition);
    nodeTransformComponent->rotateSlerp(nodeTransformComponent->getWorldOrientation() * RotationFromVToU(sourceDirection, _curVelocity), to_F32(GFXDevice::FrameInterpolationFactor()));
}

void Character::setPosition(const float3& newPosition) {
    _newPosition.set(newPosition);
    _positionDirty = true;
}

void Character::setVelocity(const float3& newVelocity) {
    _newVelocity.set(newVelocity);
    _velocityDirty = true;
}

float3 Character::getPosition() const {
    return _curPosition;
}

float3 Character::getLookingDirection() {
    SceneGraphNode* node(getBoundNode());

    if (node) {
        return node->get<TransformComponent>()->getWorldOrientation() * getRelativeLookingDirection();
    }

    return getRelativeLookingDirection();
}

void Character::lookAt(const float3& targetPos) {
    SceneGraphNode* node(getBoundNode());

    if (!node) {
        return;
    }

    _newVelocity.set(node->get<TransformComponent>()->getWorldPosition().direction(targetPos));
    _velocityDirty = true;
}

void Character::playAnimation(U32 index) const
{
    SceneGraphNode* node(getBoundNode());
    if (node)
    {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim)
        {
            anim->playAnimation(index);
        }
        else
        {
            const SceneGraphNode::ChildContainer& children = node->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;

            for (U32 i = 0u; i < childCount; ++i)
            {
                AnimationComponent* childAnim = children._data[i]->get<AnimationComponent>();
                if (childAnim)
                {
                    childAnim->playAnimation(index);
                }
            }
        }
    }
}

void Character::playNextAnimation() const
{
    SceneGraphNode* node(getBoundNode());
    if (node)
    {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim)
        {
            anim->playNextAnimation();
        }
        else
        {
            const SceneGraphNode::ChildContainer& children = node->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i)
            {
                AnimationComponent* childAnim = children._data[i]->get<AnimationComponent>();
                if (childAnim)
                {
                    childAnim->playNextAnimation();
                }
            }
        }
    }
}

void Character::playPreviousAnimation() const {
    SceneGraphNode* node(getBoundNode());
    if (node)
    {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim)
        {
            anim->playPreviousAnimation();
        }
        else
        {
            const SceneGraphNode::ChildContainer& children = node->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i)
            {
                AnimationComponent* childAnim = children._data[i]->get<AnimationComponent>();
                if (childAnim)
                {
                    childAnim->playPreviousAnimation();
                }
            }
        }
    }
}

void Character::pauseAnimation(bool state) const
{
    SceneGraphNode* node(getBoundNode());
    if (node)
    {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim)
        {
            anim->playAnimations(state);
        }
        else
        {
            const SceneGraphNode::ChildContainer& children = node->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i)
            {
                AnimationComponent* childAnim = children._data[i]->get<AnimationComponent>();
                if (childAnim)
                {
                    childAnim->playAnimations(state);
                }
            }
        }
    }
}

} //namespace Divide
