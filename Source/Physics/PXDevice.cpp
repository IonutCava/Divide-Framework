

#include "Headers/PXDevice.h"

#if defined(IS_WINDOWS_BUILD)
#include "Physics/PhysX/Headers/PhysX.h"
#endif //IS_WINDOWS_BUILD

#include "Physics/Jolt/Headers/Jolt.h"
#include "Physics/None/Headers/None.h"
#include "Utility/Headers/Localization.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

#ifndef DVD_PHYSICS_API_FOUND_
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
        DIVIDE_ASSERT( _api == nullptr, "PXDevice error: initPhysicsAPI called twice!" );
        switch ( _apiID )
        {
            case PhysicsAPI::PhysX:
            {
#           if !defined(IS_MACOS_BUILD)
                    _api = std::make_unique<PhysX>( _context );
#           else
                    Console::errorfn(LOCALE_STR("ERROR_PFX_DEVICE_API"));
                    return ErrorCode::PFX_NON_SPECIFIED;
#           endif
            } break;

            case PhysicsAPI::Jolt:
            {
                _api = std::make_unique<PhysicsJolt>(_context);
            } break;
            case PhysicsAPI::None:
            {
                _api = std::make_unique<PhysicsNone>(_context);
            } break;

            default:
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

    void PXDevice::updateTimeStep( const U8 simulationFrameRate, const F32 simSpeed )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        _api->updateTimeStep(simulationFrameRate, simSpeed );
    }

    bool PXDevice::frameEnded( const FrameEvent& evt ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        
        frameEndedInternal( evt._time._game._deltaTimeUS );
        return true;
    }

    void PXDevice::frameEndedInternal( const U64 deltaTimeGameUS ) noexcept
    {
        _api->frameEnded( deltaTimeGameUS );
    }

    bool PXDevice::frameStarted( const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        frameStartedInternal( evt._time._game._deltaTimeUS );
        return true;
    }

    void PXDevice::frameStartedInternal( const U64 deltaTimeGameUS )
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

    bool PXDevice::intersect( const Ray& intersectionRay, const float2 range, vector<SGNRayResult>& intersectionsOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        return _api->intersect( intersectionRay, range, intersectionsOut );
    }
} //namespace Divide
