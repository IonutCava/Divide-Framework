

#include "Core/Headers/PlatformContext.h"

#include "Physics/Headers/PXDevice.h"
#include "Headers/RigidBodyComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {
    RigidBodyComponent::RigidBodyComponent(SceneGraphNode* parentSGN, PlatformContext& context)
        : Parent(parentSGN, context),
          _physicsCollisionGroup(PhysicsGroup::GROUP_STATIC)
    {
    }

    RigidBodyComponent::~RigidBodyComponent()
    {
    }

    void RigidBodyComponent::physicsCollisionGroup(const PhysicsGroup group) {
        if (_physicsCollisionGroup != group) {
            _physicsCollisionGroup = group;
            if (_rigidBody != nullptr) {
                _rigidBody->physicsCollisionGroup(group);
            }
        }
    }

    bool RigidBodyComponent::filterCollision([[maybe_unused]] const RigidBodyComponent& collider) {
        // filter by mask, type, etc
        return true;
    }

    void RigidBodyComponent::onCollision(const RigidBodyComponent& collider) {
        //handle collision
        if (_collisionCbk) {
            if (filterCollision(collider)) {
                assert(parentSGN()->getGUID() != collider.parentSGN()->getGUID());
                _collisionCbk(collider);
            }
        }
    }
} //namespace