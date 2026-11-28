#include "Headers/Jolt.h"

// Jolt includes
// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNING_PUSH
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
JPH_SUPPRESS_WARNING_POP

#include "Graphs/Headers/SceneGraphNode.h"
#include "Graphs/Headers/SceneNode.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

static void TraceImpl(const char* inFMT, ...)
{
    // Format the message
    va_list list;
    va_start(list, inFMT);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer) - 1, inFMT, list);
    va_end(list);

    Divide::Console::printfn("JoltPhysics: {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for asserts, connect this to your own assert handler if you have one
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
    DIVIDE_UNEXPECTED_CALL_MSG(Divide::Util::StringFormat("[{}:{}] : ( {} ) - {}", inFile, inLine, inExpression, (inMessage != nullptr ? inMessage : "")).c_str());
    return true;
}

#endif // JPH_ENABLE_ASSERTS

namespace Divide
{

    // Layer that objects can be in, determines which other objects it can collide with
    // Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
    // layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
    // but only if you do collision testing).
    namespace Layers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    };

    /// Class that determines if two object layers can collide
    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING; // Non moving only collides with moving
            case Layers::MOVING:
                return true; // Moving collides with everything
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS(2);
    };

    // BroadPhaseLayerInterface implementation
    // This defines a mapping between object and broadphase layers.
    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            // Create a mapping table from object to broad phase layer
            mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual JPH::uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
            return mObjectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
            default:													JPH_ASSERT(false); return "INVALID";
            }
        }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    /// Class that determines if an object layer can collide with a broadphase layer
    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
            }
        }
    };

    // An example contact listener
    class MyContactListener : public JPH::ContactListener
    {
    public:
        // See: ContactListener
        virtual JPH::ValidateResult	OnContactValidate([[maybe_unused]] const JPH::Body& inBody1, [[maybe_unused]] const JPH::Body& inBody2, [[maybe_unused]] JPH::RVec3Arg inBaseOffset, [[maybe_unused]] const JPH::CollideShapeResult& inCollisionResult) override
        {
            // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        virtual void OnContactAdded([[maybe_unused]] const JPH::Body& inBody1, [[maybe_unused]] const JPH::Body& inBody2, [[maybe_unused]] const JPH::ContactManifold& inManifold, [[maybe_unused]] JPH::ContactSettings& ioSettings) override
        {
        }

        virtual void OnContactPersisted([[maybe_unused]] const JPH::Body& inBody1, [[maybe_unused]] const JPH::Body& inBody2, [[maybe_unused]] const JPH::ContactManifold& inManifold, [[maybe_unused]] JPH::ContactSettings& ioSettings) override
        {
        }

        virtual void OnContactRemoved([[maybe_unused]] const JPH::SubShapeIDPair& inSubShapePair) override
        {
        }
    };

    // An example activation listener
    class MyBodyActivationListener : public JPH::BodyActivationListener
    {
    public:
        virtual void OnBodyActivated([[maybe_unused]] const JPH::BodyID& inBodyID, [[maybe_unused]] JPH::uint64 inBodyUserData) override
        {
        }

        virtual void OnBodyDeactivated([[maybe_unused]] const JPH::BodyID& inBodyID, [[maybe_unused]] JPH::uint64 inBodyUserData) override
        {
        }
    };

    namespace 
    {
        constexpr JPH::uint cMaxBodies = 1024;
        constexpr JPH::uint cNumBodyMutexes = 0;
        constexpr JPH::uint cMaxBodyPairs = 1024;
        constexpr JPH::uint cMaxContactConstraints = 1024;
        BPLayerInterfaceImpl broad_phase_layer_interface;
        ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
        ObjectLayerPairFilterImpl object_vs_object_layer_filter;
        MyBodyActivationListener body_activation_listener;
        MyContactListener contact_listener;

        JPH::RVec3 toJoltVec3(const float3& value)
        {
            return JPH::RVec3(value._v[0], value._v[1], value._v[2]);
        }

        JPH::Quat toJoltQuat(const quatf& value)
        {
            return JPH::Quat(value.X(), value.Y(), value.Z(), value.W());
        }

        float3 fromJoltVec3(const JPH::RVec3& value)
        {
            return float3(value.GetX(), value.GetY(), value.GetZ());
        }

        quatf fromJoltQuat(const JPH::Quat& value)
        {
            return quatf(value.GetX(), value.GetY(), value.GetZ(), value.GetW());
        }
    }

    /// Jolt-specific rigid body. Kept out of the shared headers so JPH::BodyID
    /// does not leak into engine-wide code.
    class PhysicsAssetJolt final : public PhysicsAsset
    {
    public:
        PhysicsAssetJolt(RigidBodyComponent& parent, JPH::PhysicsSystem& physicsSystem, const JPH::BodyID bodyID) noexcept
            : PhysicsAsset(parent)
            , _physicsSystem(&physicsSystem)
            , _bodyID(bodyID)
        {
        }

        ~PhysicsAssetJolt() override
        {
            if (_physicsSystem != nullptr && !_bodyID.IsInvalid())
            {
                JPH::BodyInterface& bodyInterface = _physicsSystem->GetBodyInterface();
                bodyInterface.RemoveBody(_bodyID);
                bodyInterface.DestroyBody(_bodyID);
            }
        }

        void physicsCollisionGroup(const PhysicsGroup group) override
        {
            if (_physicsSystem == nullptr || _bodyID.IsInvalid())
            {
                return;
            }

            JPH::BodyInterface& bodyInterface = _physicsSystem->GetBodyInterface();

            switch (group)
            {
                case PhysicsGroup::GROUP_STATIC:
                case PhysicsGroup::GROUP_IGNORE:
                {
                    // Become static first (deactivates the body), then leave the moving layer
                    bodyInterface.SetMotionType(_bodyID, JPH::EMotionType::Static, JPH::EActivation::DontActivate);
                    bodyInterface.SetObjectLayer(_bodyID, Layers::NON_MOVING);
                } break;
                case PhysicsGroup::GROUP_KINEMATIC:
                {
                    bodyInterface.SetObjectLayer(_bodyID, Layers::MOVING);
                    bodyInterface.SetMotionType(_bodyID, JPH::EMotionType::Kinematic, JPH::EActivation::DontActivate);
                } break;
                case PhysicsGroup::GROUP_DYNAMIC:
                case PhysicsGroup::GROUP_RAGDOL:
                case PhysicsGroup::GROUP_VEHICLE:
                {
                    // Ragdoll and vehicle bodies are simulated as plain dynamic bodies for now
                    bodyInterface.SetObjectLayer(_bodyID, Layers::MOVING);
                    bodyInterface.SetMotionType(_bodyID, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
                } break;
                default:
                case PhysicsGroup::GROUP_COUNT:
                    break;
            }
        }

        [[nodiscard]] JPH::BodyID bodyID() const noexcept { return _bodyID; }
        [[nodiscard]] bool isValid() const noexcept { return _physicsSystem != nullptr && !_bodyID.IsInvalid(); }

    private:
        JPH::PhysicsSystem* _physicsSystem = nullptr;
        JPH::BodyID _bodyID;
    };

    PhysicsJolt::PhysicsJolt( PlatformContext& context )
        : PhysicsAPIWrapper(context)
    {
    }

    ErrorCode PhysicsJolt::initPhysicsAPI( [[maybe_unused]] const U8 targetFrameRate, [[maybe_unused]] const F32 simSpeed )
    {
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl);

        JPH::RegisterDefaultAllocator();

        // Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
        // It is not directly used in this example but still required.
        _factory = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = _factory.get();

        JPH::RegisterTypes();

        _allocator = std::make_unique<JPH::TempAllocatorImpl>(Bytes::Mega(10u));

        _threadPool = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        _physicsSystem = std::make_unique<JPH::PhysicsSystem>();

        _physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);

        _physicsSystem->SetBodyActivationListener(&body_activation_listener);

        _physicsSystem->SetContactListener(&contact_listener);

        // Jolt already defaults to (0, -9.81, 0); made explicit so it can move to
        // per-scene configuration read from the scene XML later.
        _physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        return ErrorCode::NO_ERR;
    }

    bool PhysicsJolt::closePhysicsAPI()
    {
        _physicsSystem.reset();
        _threadPool.reset();
        _allocator.reset();
        JPH::UnregisterTypes();
        _factory.reset();
        JPH::Factory::sInstance = nullptr;

        return true;
    }

    /// Process results
    void PhysicsJolt::frameStartedInternal(const U64 deltaTimeGameUS )
    {
        // If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
        const I32 cCollisionSteps = to_I32(std::ceil(std::max(60.f / _simulationFrameRate, 1.f)));

        _physicsSystem->OptimizeBroadPhase();
        _physicsSystem->Update(Time::MicrosecondsToSeconds<F32>(deltaTimeGameUS), cCollisionSteps, _allocator.get(), _threadPool.get());
    }

    /// Update actors: copy dynamic body transforms back into the scene graph
    void PhysicsJolt::frameEndedInternal([[maybe_unused]] const U64 deltaTimeGameUS )
    {
        if (_physicsSystem == nullptr)
        {
            return;
        }

        JPH::BodyIDVector activeBodies;
        _physicsSystem->GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);

        const JPH::BodyInterface& bodyInterface = _physicsSystem->GetBodyInterface();
        for (const JPH::BodyID& bodyID : activeBodies)
        {
            // Only the simulation owns dynamic bodies; kinematic and the rest are deferred
            if (bodyInterface.GetMotionType(bodyID) != JPH::EMotionType::Dynamic)
            {
                continue;
            }

            auto* asset = reinterpret_cast<PhysicsAssetJolt*>(bodyInterface.GetUserData(bodyID));
            if (asset == nullptr || !asset->isValid())
            {
                continue;
            }

            SceneGraphNode* node = asset->getParent().parentSGN();
            TransformComponent* tComp = node->get<TransformComponent>();
            if (tComp == nullptr)
            {
                continue;
            }

            const float3 worldPosition = fromJoltVec3(bodyInterface.GetPosition(bodyID));
            const quatf worldOrientation = fromJoltQuat(bodyInterface.GetRotation(bodyID));

            const TransformComponent* parentComp = node->parent() != nullptr ? node->parent()->get<TransformComponent>() : nullptr;
            if (parentComp != nullptr)
            {
                const mat4<F32> worldMatrix = GetMatrix(TransformValues{ worldOrientation, worldPosition, tComp->getWorldScale() });
                tComp->setTransforms(parentComp->getWorldMatrix().getInverse() * worldMatrix);
            }
            else
            {
                tComp->setPosition(worldPosition);
                tComp->setRotation(worldOrientation);
            }
        }
    }

    void PhysicsJolt::idle()
    {
    }

    bool PhysicsJolt::initPhysicsScene([[maybe_unused]] Scene& scene )
    {
       return true;
    }

    bool PhysicsJolt::destroyPhysicsScene([[maybe_unused]] const Scene& scene )
    {
        return true;
    }

    PhysicsAsset* PhysicsJolt::createRigidActor(SceneGraphNode* node, RigidBodyComponent& parentComp )
    {
        if (node == nullptr || _physicsSystem == nullptr)
        {
            return nullptr;
        }

        const TransformComponent* tComp = node->get<TransformComponent>();
        if (tComp == nullptr)
        {
            return nullptr;
        }

        const BoundingBox localBounds = node->getNode().getBounds();
        const float3 halfExtent = localBounds.getHalfExtent();
        const float3& scale = tComp->getWorldScale();

        JPH::Shape* shape = nullptr;
        switch (node->handle()._type)
        {
            case SceneNodeType::TYPE_SPHERE_3D:
            {
                const F32 radius = std::max(halfExtent._v[0], std::max(halfExtent._v[1], halfExtent._v[2]));
                const F32 uniformScale = std::max(scale._v[0], std::max(scale._v[1], scale._v[2]));
                if (radius > 0.001f && uniformScale > 0.001f)
                {
                    shape = new JPH::SphereShape(radius * uniformScale);
                }
            } break;
            case SceneNodeType::TYPE_BOX_3D:
            case SceneNodeType::TYPE_QUAD_3D:
            {
                // Clamp thin extents (e.g. quads) so the box stays valid
                constexpr F32 minHalfExtent = 0.01f;
                shape = new JPH::BoxShape(JPH::Vec3(std::max(halfExtent._v[0] * scale._v[0], minHalfExtent),
                                                    std::max(halfExtent._v[1] * scale._v[1], minHalfExtent),
                                                    std::max(halfExtent._v[2] * scale._v[2], minHalfExtent)));
            } break;
            default:
                // Meshes (convex hulls), terrain (height fields) and water are handled in later steps
                break;
        }

        if (shape == nullptr)
        {
            return nullptr;
        }

        // Nodes whose bounds are not centred on the pivot (e.g. quads) need the
        // shape offset so that it lines up with the rendered geometry
        const float3 boundsCenter = localBounds.getCenter();
        if (std::abs(boundsCenter._v[0]) > 0.001f ||
            std::abs(boundsCenter._v[1]) > 0.001f ||
            std::abs(boundsCenter._v[2]) > 0.001f)
        {
            const float3 scaledCenter{ boundsCenter._v[0] * scale._v[0],
                                       boundsCenter._v[1] * scale._v[1],
                                       boundsCenter._v[2] * scale._v[2] };
            shape = new JPH::RotatedTranslatedShape(toJoltVec3(scaledCenter), JPH::Quat::sIdentity(), shape);
        }

        JPH::BodyCreationSettings settings(shape,
                                           toJoltVec3(tComp->getWorldPosition()),
                                           toJoltQuat(tComp->getWorldOrientation()),
                                           JPH::EMotionType::Static,
                                           Layers::NON_MOVING);
        // Static bodies keep mass properties around so they can be switched to dynamic later
        settings.mAllowDynamicOrKinematic = true;

        JPH::BodyInterface& bodyInterface = _physicsSystem->GetBodyInterface();
        const JPH::BodyID bodyID = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (bodyID.IsInvalid())
        {
            return nullptr;
        }

        auto* asset = new PhysicsAssetJolt(parentComp, *_physicsSystem, bodyID);
        bodyInterface.SetUserData(bodyID, reinterpret_cast<JPH::uint64>(asset));
        asset->physicsCollisionGroup(parentComp.physicsCollisionGroup());

        return asset;
    }

    bool PhysicsJolt::convertActor(PhysicsAsset* actor, const PhysicsGroup newGroup )
    {
        auto* asset = static_cast<PhysicsAssetJolt*>(actor);
        if (asset == nullptr || !asset->isValid())
        {
            return false;
        }

        asset->physicsCollisionGroup(newGroup);
        return true;
    }

    bool PhysicsJolt::intersect([[maybe_unused]] const Ray& intersectionRay, [[maybe_unused]] const float2 range, [[maybe_unused]] vector<SGNRayResult>& intersectionsOut ) const
    {
        return false;
    }

};
