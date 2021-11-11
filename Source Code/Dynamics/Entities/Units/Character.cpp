#include "stdafx.h"

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
        _newPosition.set(transform->getPosition());
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
    
    vec3<F32> sourceDirection(getLookingDirection());
    sourceDirection.y = 0.0f;

    _oldPosition.set(nodeTransformComponent->getPosition());
    _oldPosition.lerp(_curPosition, to_F32(GFXDevice::FrameInterpolationFactor()));
    nodeTransformComponent->setPosition(_oldPosition);
    nodeTransformComponent->rotateSlerp(nodeTransformComponent->getOrientation() * RotationFromVToU(sourceDirection, _curVelocity), to_F32(GFXDevice::FrameInterpolationFactor()));
}

void Character::setPosition(const vec3<F32>& newPosition) {
    _newPosition.set(newPosition);
    _positionDirty = true;
}

void Character::setVelocity(const vec3<F32>& newVelocity) {
    _newVelocity.set(newVelocity);
    _velocityDirty = true;
}

vec3<F32> Character::getPosition() const {
    return _curPosition;
}

vec3<F32> Character::getLookingDirection() {
    SceneGraphNode* node(getBoundNode());

    if (node) {
        return node->get<TransformComponent>()->getOrientation() * getRelativeLookingDirection();
    }

    return getRelativeLookingDirection();
}

void Character::lookAt(const vec3<F32>& targetPos) {
    SceneGraphNode* node(getBoundNode());

    if (!node) {
        return;
    }

    _newVelocity.set(
        node->get<TransformComponent>()->getPosition().direction(
            targetPos));
    _velocityDirty = true;
}

void Character::playAnimation(I32 index) const {
    SceneGraphNode* node(getBoundNode());
    if (node) {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim) {
            anim->playAnimation(index);
        } else {
            node->forEachChild([index](const SceneGraphNode* child, I32 /*childIdx*/) {
                AnimationComponent* childAnim = child->get<AnimationComponent>();
                if (childAnim) {
                    childAnim->playAnimation(index);
                }
                return true;
            });
        }
    }
}

void Character::playNextAnimation() const {
    SceneGraphNode* node(getBoundNode());
    if (node) {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim) {
            anim->playNextAnimation();
        } else {
            node->forEachChild([](const SceneGraphNode* child, I32 /*childIdx*/) {
                AnimationComponent* childAnim = child->get<AnimationComponent>();
                if (childAnim) {
                    childAnim->playNextAnimation();
                }
                return true;
            });
        }
    }
}

void Character::playPreviousAnimation() const {
    SceneGraphNode* node(getBoundNode());
    if (node) {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim) {
            anim->playPreviousAnimation();
        } else {
            node->forEachChild([](const SceneGraphNode* child, I32 /*childIdx*/) {
                AnimationComponent* childAnim = child->get<AnimationComponent>();
                if (childAnim) {
                    childAnim->playPreviousAnimation();
                }
                return true;
            });
        }
    }
}

void Character::pauseAnimation(bool state) const {
    SceneGraphNode* node(getBoundNode());
    if (node) {
        AnimationComponent* anim = node->get<AnimationComponent>();
        if (anim) {
            anim->playAnimations(state);
        } else {
            node->forEachChild([state](const SceneGraphNode* child, I32 /*childIdx*/) {
                AnimationComponent* childAnim = child->get<AnimationComponent>();
                if (childAnim) {
                    childAnim->playAnimations(state);
                }
                return true;
            });
        }
    }
}
}