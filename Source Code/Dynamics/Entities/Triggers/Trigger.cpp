#include "stdafx.h"

#include "Headers/Trigger.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Threading/Headers/Task.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Dynamics/Entities/Units/Headers/Unit.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {

Trigger::Trigger(ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : SceneNode(parentCache, descriptorHash, name, ResourcePath(name), {}, SceneNodeType::TYPE_TRIGGER, to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS))
{
    _taskPool = &parentResourceCache()->context().taskPool(TaskPoolType::HIGH_PRIORITY);
}

void Trigger::sceneUpdate(const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) {
    if (_drawImpostor) {
        /// update dummy position if it is so
        sgn->getChild(0)->get<TransformComponent>()->setPosition(_triggerPosition);
        sgn->context().gfx().debugDrawSphere(getGUID(), _triggerPosition, _radius, DefaultColours::RED);
        sgn->getChild(0)->setFlag(SceneGraphNode::Flags::ACTIVE);
    }
}

void Trigger::setParams(Task& triggeredTask,
                        const vec3<F32>& triggerPosition,
                        const F32 radius) noexcept {
    /// Check if position has changed
    if (!_triggerPosition.compare(triggerPosition)) {
        _triggerPosition = triggerPosition;
    }
    /// Check if radius has changed
    if (!COMPARE(_radius, radius)) {
        _radius = radius;
    }
    _triggeredTask = &triggeredTask;
}

bool Trigger::unload() {
    return SceneNode::unload();
}

bool Trigger::check(Unit* const unit, const vec3<F32>& camEyePos) const {
    if (!_enabled)
        return false;

    vec3<F32> position;
    if (!unit) {  ///< use camera position
        position = camEyePos;
    } else {  ///< use unit position
        position = unit->getCurrentPosition();
    }
    /// Should we trigger the Task?
    if (position.compare(_triggerPosition, _radius)) {
        /// Check if the Task is valid, and trigger if it is
        return trigger();
    }
    /// Not triggered
    return false;
}

bool Trigger::trigger() const {
    assert(_triggeredTask != nullptr);
    Start(*_triggeredTask, *_taskPool);
    return true;
}
}