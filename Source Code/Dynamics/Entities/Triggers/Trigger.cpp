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

void Trigger::setCallback(Task& triggeredTask) noexcept {
    _triggeredTask = &triggeredTask;
}

bool Trigger::unload() {
    return SceneNode::unload();
}

bool Trigger::trigger() const {
    assert(_triggeredTask != nullptr);
    Start(*_triggeredTask, *_taskPool);
    return true;
}
}