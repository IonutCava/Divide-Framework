

#include "Core/Headers/PlatformContext.h"

#include "Physics/Headers/PXDevice.h"
#include "Headers/RigidBodyComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

    namespace
    {
        const char* g_physicsGroupNames[] =
        {
            "Static",
            "Dynamic",
            "Kinematic",
            "Ragdoll",
            "Vehicle",
            "Ignore"
        };
    }

    DVD_INSTANTIATE_SGN_COMPONENT_NODE_OPS(RigidBodyComponent);

    RigidBodyComponent::RigidBodyComponent(SceneGraphNode* parentSGN, PlatformContext& context)
        : Parent(parentSGN, context),
          _physicsCollisionGroup(PhysicsGroup::GROUP_STATIC)
    {
        EditorComponentField collisionGroupField = {};
        collisionGroupField._name = "Collision Group";
        collisionGroupField._type = EditorComponentFieldType::DROPDOWN_TYPE;
        collisionGroupField._range = { 0u, to_U32(PhysicsGroup::GROUP_COUNT) };
        collisionGroupField._readOnly = parentSGN->handle()._type == SceneNodeType::TYPE_TERRAIN;
        collisionGroupField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data)
        {
            *static_cast<U32*>(dataOut) = to_U32(_physicsCollisionGroup);
        };
        collisionGroupField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data)
        {
            physicsCollisionGroup(static_cast<PhysicsGroup>(*static_cast<const U32*>(data)));
        };
        collisionGroupField._displayNameGetter = [](const U32 index, [[maybe_unused]] void* user_data) noexcept
        {
            return index < to_U32(PhysicsGroup::GROUP_COUNT) ? g_physicsGroupNames[index] : "Invalid";
        };

        editorComponent().registerField(MOV(collisionGroupField));
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
                DIVIDE_ASSERT(parentSGN()->getGUID() != collider.parentSGN()->getGUID());
                _collisionCbk(collider);
            }
        }
    }

} //namespace Divide
