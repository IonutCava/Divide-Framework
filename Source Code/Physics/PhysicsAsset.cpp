#include "stdafx.h"

#include "Headers/PhysicsAsset.h"

#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

namespace Divide {
PhysicsAsset::PhysicsAsset(RigidBodyComponent& parent) noexcept
    : _parentComponent(parent),
      _context(parent.parentSGN()->context().pfx())
{
}

void PhysicsAsset::physicsCollisionGroup([[maybe_unused]] const PhysicsGroup group) {
}

}; //namespace Divide