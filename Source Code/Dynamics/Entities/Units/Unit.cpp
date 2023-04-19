#include "stdafx.h"

#include "Headers/Unit.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Core/Headers/Application.h"
#include "Managers/Headers/FrameListenerManager.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {
namespace TypeUtil {
    const char* UnitTypeToString(const UnitType unitType) noexcept {
        return Names::unitType[to_base(unitType)];
    }

    UnitType StringToUnitType(const string& name) {
        for (U8 i = 0; i < to_U8(UnitType::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::unitType[i]) == 0) {
                return static_cast<UnitType>(i);
            }
        }

        return UnitType::COUNT;
    }
}

Unit::Unit(const UnitType type)
    : _type(type),
      _moveSpeed(Metric::Base(1.0f)),
      _acceleration(Metric::Base(1.0f)),
      _moveTolerance(0.1f),
      _node(nullptr)
{
}

void Unit::setParentNode(SceneGraphNode* node) {
    _node = node;
    _currentPosition = _node->get<TransformComponent>()->getWorldPosition();
}

/// Pathfinding, collision detection, animation playback should all be
/// controlled from here
bool Unit::moveTo(const vec3<F32>& targetPosition, const U64 deltaTimeUS)
{
    // We should always have a node
    if (!_node) {
        return false;
    }
    LockGuard<SharedMutex> w_lock(_unitUpdateMutex);
    // We receive move request every frame for now (or every task tick)
    // Start plotting a course from our current position
    _currentPosition = _node->get<TransformComponent>()->getWorldPosition();
    _currentTargetPosition = targetPosition;

    // figure out how many milliseconds have elapsed since last move time
    const D64 timeDif = Time::MicrosecondsToSeconds<D64>( deltaTimeUS );

    // 'moveSpeed' m/s = '0.001 * moveSpeed' m / ms
    // distance = timeDif * 0.001 * moveSpeed
    F32 moveDistance = std::min(to_F32(_moveSpeed * timeDif), 0.f);

    bool returnValue = IS_TOLERANCE(moveDistance, Metric::Centi(1.0f));

    if (!returnValue) {
        F32 xDelta = _currentTargetPosition.x - _currentPosition.x;
        F32 yDelta = _currentTargetPosition.y - _currentPosition.y;
        F32 zDelta = _currentTargetPosition.z - _currentPosition.z;
        bool xTolerance = IS_TOLERANCE(xDelta, _moveTolerance);
        bool yTolerance = IS_TOLERANCE(yDelta, _moveTolerance);
        bool zTolerance = IS_TOLERANCE(zDelta, _moveTolerance);

        // Compute the destination point for current frame step
        vec3<F32> interpPosition;
        if (!yTolerance && !IS_ZERO(yDelta)) {
            interpPosition.y =
                _currentPosition.y > _currentTargetPosition.y ? -moveDistance
                    : moveDistance;
        }
        if (!xTolerance || !zTolerance) {
            // Update target
            if (IS_ZERO(xDelta)) {
                interpPosition.z =
                    _currentPosition.z > _currentTargetPosition.z
                        ? -moveDistance
                        : moveDistance;
            } else if (IS_ZERO(zDelta)) {
                interpPosition.x =
                    _currentPosition.x > _currentTargetPosition.x
                        ? -moveDistance
                        : moveDistance;
            } else if (std::fabs(xDelta) > std::fabs(zDelta)) {
                F32 value = std::fabs(zDelta / xDelta) * moveDistance;
                interpPosition.z =
                    _currentPosition.z > _currentTargetPosition.z ? -value
                        : value;
                interpPosition.x =
                    _currentPosition.x > _currentTargetPosition.x
                        ? -moveDistance
                        : moveDistance;
            } else {
                F32 value = std::fabs(xDelta / zDelta) * moveDistance;
                interpPosition.x =
                    _currentPosition.x > _currentTargetPosition.x ? -value
                        : value;
                interpPosition.z =
                    _currentPosition.z > _currentTargetPosition.z
                        ? -moveDistance
                        : moveDistance;
            }
            // commit transformations
            _node->get<TransformComponent>()->translate(interpPosition);
        }
    }

    return returnValue;
}

/// Move along the X axis
bool Unit::moveToX(const F32 targetPosition, const U64 deltaTimeUS )
{
    if (!_node) {
        return false;
    }
    {
        /// Update current position
        LockGuard<SharedMutex> w_lock(_unitUpdateMutex);
        _currentPosition = _node->get<TransformComponent>()->getWorldPosition();
    }
    return moveTo(vec3<F32>(targetPosition,
                            _currentPosition.y,
                            _currentPosition.z),
                            deltaTimeUS);
}

/// Move along the Y axis
bool Unit::moveToY(const F32 targetPosition, const U64 deltaTimeUS )
{
    if (!_node) {
        return false;
    }
    {
        /// Update current position
        LockGuard<SharedMutex> w_lock(_unitUpdateMutex);
        _currentPosition = _node->get<TransformComponent>()->getWorldPosition();
    }
    return moveTo(vec3<F32>(_currentPosition.x,
                            targetPosition,
                            _currentPosition.z),
                            deltaTimeUS);
}

/// Move along the Z axis
bool Unit::moveToZ(const F32 targetPosition, const U64 deltaTimeUS )
{
    if (!_node) {
        return false;
    }
    {
        /// Update current position
        LockGuard<SharedMutex> w_lock(_unitUpdateMutex);
        _currentPosition = _node->get<TransformComponent>()->getWorldPosition();
    }
    return moveTo(vec3<F32>(_currentPosition.x,
                            _currentPosition.y,
                            targetPosition),
                            deltaTimeUS);
}

/// Further improvements may imply a cooldown and collision detection at
/// destination (thus the if-check at the end)
bool Unit::teleportTo(const vec3<F32>& targetPosition) {
    if (!_node) {
        return false;
    }
    LockGuard<SharedMutex> w_lock(_unitUpdateMutex);
    /// We receive move request every frame for now (or every task tick)
    /// Check if the current request is already processed
    if (!_currentTargetPosition.compare(targetPosition, 0.00001f)) {
        /// Update target
        _currentTargetPosition = targetPosition;
    }
    TransformComponent* nodeTransformComponent =
        _node->get<TransformComponent>();
    /// Start plotting a course from our current position
    _currentPosition = nodeTransformComponent->getWorldPosition();
    /// teleport to desired position
    nodeTransformComponent->setPosition(_currentTargetPosition);
    /// Update current position
    _currentPosition = nodeTransformComponent->getWorldPosition();
    /// And check if we arrived
    if (_currentTargetPosition.compare(_currentPosition, 0.0001f)) {
        return true;  ///< yes
    }

    return false;  ///< no
}

void Unit::setAttribute(const U32 attributeID, const I32 initialValue) {
    _attributes[attributeID] = initialValue;
}

I32 Unit::getAttribute(const U32 attributeID) const {
    const AttributeMap::const_iterator it = _attributes.find(attributeID);
    if (it != std::end(_attributes)) {
        return it->second;
    }

    return -1;
}

}