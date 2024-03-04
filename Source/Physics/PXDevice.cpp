

#include "Headers/PXDevice.h"

#include "Utility/Headers/Localization.h"
#include "Physics/PhysX/Headers/PhysX.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

#ifndef _PHYSICS_API_FOUND_
#error "No physics library implemented!"
#endif

namespace Divide
{

    namespace
    {
        constexpr F32 g_maxSimSpeed = 1000.f;
    };

    PXDevice::PXDevice( PlatformContext& context ) noexcept
        : PhysicsAPIWrapper(context )
        , FrameListener( "PXDevice", context.kernel().frameListenerMgr(), 2u )
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
                _api = eastl::make_unique<PhysX>( _context );
            } break;
            case PhysicsAPI::ODE:
            case PhysicsAPI::Bullet:
            case PhysicsAPI::COUNT:
            {
                Console::errorfn( LOCALE_STR( "ERROR_PFX_DEVICE_API" ) );
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

        Console::printfn( LOCALE_STR( "STOP_PHYSICS_INTERFACE" ) );
        const bool state = _api->closePhysicsAPI();
        _api.reset();

        return state;
    }

    void PXDevice::updateTimeStep( const U8 timeStepFactor, const F32 simSpeed )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        _api->updateTimeStep( timeStepFactor, simSpeed );
    }

    bool PXDevice::frameEnded( const FrameEvent& evt ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        
        frameEnded( evt._time._game._deltaTimeUS );
        return true;
    }

    void PXDevice::frameEnded( const U64 deltaTimeGameUS ) noexcept
    {
        _api->frameEnded( deltaTimeGameUS );
    }

    bool PXDevice::frameStarted( const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        frameStarted( evt._time._game._deltaTimeUS );
        return true;
    }

    void PXDevice::frameStarted( const U64 deltaTimeGameUS )
    {
        _api->frameStarted( deltaTimeGameUS );
    }
    
    void PXDevice::idle()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        _api->idle();
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