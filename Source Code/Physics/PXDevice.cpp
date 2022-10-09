#include "stdafx.h"

#include "Headers/PXDevice.h"

#include "Utility/Headers/Localization.h"
#include "Physics/PhysX/Headers/PhysX.h"

#ifndef _PHYSICS_API_FOUND_
#error "No physics library implemented!"
#endif

namespace Divide
{

    namespace
    {
        constexpr F32 g_maxSimSpeed = 1000.f;
    };

    PXDevice::PXDevice( Kernel& parent ) noexcept
        : KernelComponent( parent ),
        PhysicsAPIWrapper()
    {
    }

    PXDevice::~PXDevice()
    {
        closePhysicsAPI();
    }

    ErrorCode PXDevice::initPhysicsAPI( const U8 targetFrameRate, const F32 simSpeed )
    {
        DIVIDE_ASSERT( _api == nullptr,
                       "PXDevice error: initPhysicsAPI called twice!" );
        switch ( _API_ID )
        {
            case PhysicsAPI::PhysX:
            {
                _api = eastl::make_unique<PhysX>();
            } break;
            case PhysicsAPI::ODE:
            case PhysicsAPI::Bullet:
            case PhysicsAPI::COUNT:
            {
                Console::errorfn( Locale::Get( _ID( "ERROR_PFX_DEVICE_API" ) ) );
                return ErrorCode::PFX_NON_SPECIFIED;
            };
        };
        _simulationSpeed = CLAMPED( simSpeed, 0.f, g_maxSimSpeed );
        return _api->initPhysicsAPI( targetFrameRate, _simulationSpeed );
    }

    bool PXDevice::closePhysicsAPI()
    {
        if ( _api == nullptr )
        {
            return false;
        }

        Console::printfn( Locale::Get( _ID( "STOP_PHYSICS_INTERFACE" ) ) );
        const bool state = _api->closePhysicsAPI();
        _api.reset();

        return state;
    }

    void PXDevice::updateTimeStep( const U8 timeStepFactor, const F32 simSpeed )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        _api->updateTimeStep( timeStepFactor, simSpeed );
    }

    void PXDevice::update( const U64 deltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        _api->update( deltaTimeUS );
    }

    void PXDevice::process( const U64 deltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        _api->process( deltaTimeUS );
    }

    void PXDevice::idle()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        _api->idle();
    }

    void PXDevice::beginFrame()
    {
        NOP();
    }

    void PXDevice::endFrame()
    {
        NOP();
    }

    bool PXDevice::initPhysicsScene( Scene& scene )
    {
        return _api->initPhysicsScene( scene );
    }

    bool PXDevice::destroyPhysicsScene( const Scene& scene )
    {
        return _api->destroyPhysicsScene( scene );
    }

    PhysicsAsset* PXDevice::createRigidActor( SceneGraphNode* node, RigidBodyComponent& parentComp )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        return _api->createRigidActor( node, parentComp );
    }

    bool PXDevice::convertActor( PhysicsAsset* actor, const PhysicsGroup newGroup )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        return _api->convertActor( actor, newGroup );
    }

    bool PXDevice::intersect( const Ray& intersectionRay, const vec2<F32> range, vector<SGNRayResult>& intersectionsOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        return _api->intersect( intersectionRay, range, intersectionsOut );
    }
}; //namespace Divide