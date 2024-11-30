#include "Headers/None.h"

namespace Divide
{
    PhysicsNone::PhysicsNone(PlatformContext& context)
        : PhysicsAPIWrapper(context)
    {
    }

    ErrorCode PhysicsNone::initPhysicsAPI([[maybe_unused]] const U8 targetFrameRate, [[maybe_unused]] const F32 simSpeed)
    {
        return ErrorCode::NO_ERR;
    }

    bool PhysicsNone::closePhysicsAPI()
    {
        return true;
    }

    /// Process results
    void PhysicsNone::frameStartedInternal([[maybe_unused]] const U64 deltaTimeGameUS)
    {
    }

    /// Update actors
    void PhysicsNone::frameEndedInternal([[maybe_unused]] const U64 deltaTimeGameUS)
    {
    }

    void PhysicsNone::idle()
    {
    }

    bool PhysicsNone::initPhysicsScene([[maybe_unused]] Scene& scene)
    {
        return true;
    }

    bool PhysicsNone::destroyPhysicsScene([[maybe_unused]] const Scene& scene)
    {
        return true;
    }

    PhysicsAsset* PhysicsNone::createRigidActor([[maybe_unused]] SceneGraphNode* node, [[maybe_unused]] RigidBodyComponent& parentComp)
    {
        return nullptr;
    }

    bool PhysicsNone::convertActor([[maybe_unused]] PhysicsAsset* actor, [[maybe_unused]] const PhysicsGroup newGroup)
    {
        return true;
    }

    bool PhysicsNone::intersect([[maybe_unused]] const Ray& intersectionRay, [[maybe_unused]] const float2 range, [[maybe_unused]] vector<SGNRayResult>& intersectionsOut) const
    {
        return false;
    }

};
