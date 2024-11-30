#include "Headers/Jolt.h"

#include "Core/Headers/Console.h"

#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNINGS

// All Jolt symbols are in the JPH namespace
using namespace JPH;

// If you want your code to compile using single or double precision write 0.0_r to get a Real value that compiles to double or float depending if JPH_DOUBLE_PRECISION is set or not.
using namespace JPH::literals;

static void TraceImpl(const char* inFMT, Args&&... args)
{
    Console::printfn(inFMT, FWD(args)...);
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for asserts, connect this to your own assert handler if you have one
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
{
    std::stringstream ss;
    // Print to the TTY
    ss << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << endl;

    DIVIDE_UNEXPECTED_CALL_MSG(ss.string());

    // Breakpoint
    DebugBreak();

    return true;
};

#endif // JPH_ENABLE_ASSERTS
namespace Divide
{

    namespace 
    {
        // 10 Megs
        TempAllocatorImpl temp_allocator(Bytes::Mega(10u));

        constexpr uint cMaxBodies = 1024;
        constexpr uint cNumBodyMutexes = 0;
        constexpr uint cMaxBodyPairs = 1024;
        constexpr uint cMaxContactConstraints = 1024;
        BPLayerInterfaceImpl broad_phase_layer_interface;
        ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
        ObjectLayerPairFilterImpl object_vs_object_layer_filter;
        MyBodyActivationListener body_activation_listener;
        MyContactListener contact_listener;

    }

    PhysicsJolt::PhysicsJolt( PlatformContext& context )
        : PhysicsAPIWrapper(context)
    {
        Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
    }

    ErrorCode PhysicsJolt::initPhysicsAPI( [[maybe_unused]] const U8 targetFrameRate, [[maybe_unused]] const F32 simSpeed )
    {
        // Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
           // It is not directly used in this example but still required.
        _factory = std::make_unique<Factory>();
        RegisterTypes();

        _threadPool = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);

        _physicsSystem = std::make_unique<PhysicsSystem>();

        _physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);

        _physicsSystem->SetBodyActivationListener(&body_activation_listener);

        _physicsSystem->SetContactListener(&contact_listener);

        return ErrorCode::NO_ERR;
    }

    bool PhysicsJolt::closePhysicsAPI()
    {
        _physicsSystem.reset();
        _threadPool.reset();
        UnregisterTypes();
        _factory.reste();

        return true;
    }

    /// Process results
    void PhysicsJolt::frameStartedInternal(const U64 deltaTimeGameUS )
    {
        _physicsSystem->OptimizeBroadPhase();
        _physicsSystem->Update(Time::MicrosecondsToSeconds<F32>(deltaTimeGameUS), cCollisionSteps, &temp_allocator, &job_system);
    }

    /// Update actors
    void PhysicsJolt::frameEndedInternal([[maybe_unused]] const U64 deltaTimeGameUS )
    {
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

    PhysicsAsset* PhysicsJolt::createRigidActor([[maybe_unused]] SceneGraphNode* node, [[maybe_unused]] RigidBodyComponent& parentComp )
    {
        return nullptr;
    }

    bool PhysicsJolt::convertActor([[maybe_unused]] PhysicsAsset* actor, [[maybe_unused]] const PhysicsGroup newGroup )
    {
        return true;
    }

    bool PhysicsJolt::intersect([[maybe_unused]] const Ray& intersectionRay, [[maybe_unused]] const float2 range, [[maybe_unused]] vector<SGNRayResult>& intersectionsOut ) const
    {
        return false;
    }

};
