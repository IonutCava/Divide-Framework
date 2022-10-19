#include "stdafx.h"

#include "Headers/PhysXSceneInterface.h"
#include "Headers/PhysX.h"

#include "Scenes/Headers/Scene.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Utility/Headers/Localization.h"
#include "Physics/Headers/PXDevice.h"

namespace Divide
{
    namespace
    {
        constexpr U32 g_parallelPartitionSize = 32;
    }

    enum class PhysXSceneInterfaceState : U8
    {
        STATE_LOADING_ACTORS
    };
#ifdef USE_MBP

    static void setupMBP( physx::PxScene& scene )
    {
        using namespace physx;

        const float range = 1000.0f;
        const PxU32 subdiv = 4;
        // const PxU32 subdiv = 1;
        // const PxU32 subdiv = 2;
        // const PxU32 subdiv = 8;

        const PxVec3 min( -range );
        const PxVec3 max( range );
        const PxBounds3 globalBounds( min, max );

        PxBounds3 bounds[256];
        const PxU32 nbRegions = PxBroadPhaseExt::createRegionsFromWorldBounds( bounds, globalBounds, subdiv );

        for ( PxU32 i = 0; i < nbRegions; i++ )
        {
            PxBroadPhaseRegion region;
            region.bounds = bounds[i];
            region.userData = (void*)i;
            scene.addBroadPhaseRegion( region );
        }
    }
#endif //USE_MBP

    PhysXSceneInterface::PhysXSceneInterface( Scene& parentScene )
        : PhysicsSceneInterface( parentScene )
    {
    }

    PhysXSceneInterface::~PhysXSceneInterface()
    {
        release();
    }

    bool PhysXSceneInterface::init()
    {
        const PhysX& physX = static_cast<PhysX&>(_parentScene.context().pfx().getImpl());
        physx::PxPhysics* gPhysicsSDK = physX.getSDK();
        // Create the scene
        if ( !gPhysicsSDK )
        {
            Console::errorfn( Locale::Get( _ID( "ERROR_PHYSX_SDK" ) ) );
            return false;
        }

        physx::PxSceneDesc sceneDesc( gPhysicsSDK->getTolerancesScale() );
        sceneDesc.gravity = physx::PxVec3( DEFAULT_GRAVITY.x, DEFAULT_GRAVITY.y, DEFAULT_GRAVITY.z );
        if ( !sceneDesc.cpuDispatcher )
        {
            _cpuDispatcher = physx::PxDefaultCpuDispatcherCreate( std::max( 2u, HardwareThreadCount() - 2u ) );
            if ( !_cpuDispatcher )
            {
                Console::errorfn( Locale::Get( _ID( "ERROR_PHYSX_INTERFACE_CPU_DISPATCH" ) ) );
            }
            sceneDesc.cpuDispatcher = _cpuDispatcher;
        }

        if ( !sceneDesc.filterShader )
        {
            sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
        }

#if PX_SUPPORT_GPU_PHYSX
        if ( !sceneDesc.cudaContextManager )
            sceneDesc.cudaContextManager = physX.cudaContextManager();
#endif //PX_SUPPORT_GPU_PHYSX

        //sceneDesc.frictionType = physx::PxFrictionType::eTWO_DIRECTIONAL;
        //sceneDesc.frictionType = physx::PxFrictionType::eONE_DIRECTIONAL;
        //sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
        //sceneDesc.flags |= physx::PxSceneFlag::eENABLE_AVERAGE_POINT;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_STABILIZATION;
        //sceneDesc.flags |= physx::PxSceneFlag::eADAPTIVE_FORCE;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
        sceneDesc.sceneQueryUpdateMode = physx::PxSceneQueryUpdateMode::eBUILD_ENABLED_COMMIT_DISABLED;

        //sceneDesc.flags |= physx::PxSceneFlag::eDISABLE_CONTACT_CACHE;
        //sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eGPU;
        //sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eSAP;
        sceneDesc.gpuMaxNumPartitions = 8;
        //sceneDesc.solverType = physx::PxSolverType::eTGS;
#ifdef USE_MBP
        sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eMBP;
#endif //USE_MBP

        _gScene = gPhysicsSDK->createScene( sceneDesc );
        if ( !_gScene )
        {
            Console::errorfn( Locale::Get( _ID( "ERROR_PHYSX_INTERFACE_CREATE_SCENE" ) ) );
            return false;
        }

        physx::PxSceneWriteLock scopedLock( *_gScene );
        [[maybe_unused]] const physx::PxSceneFlags flag = _gScene->getFlags();
        //_gScene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0);
        //_gScene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);

        if constexpr( !Config::Build::IS_SHIPPING_BUILD )
        {
            physx::PxPvdSceneClient* pvdClient = _gScene->getScenePvdClient();
            if ( pvdClient != nullptr )
            {
                pvdClient->setScenePvdFlag( physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true );
                pvdClient->setScenePvdFlag( physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true );
                pvdClient->setScenePvdFlag( physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true );
            }
        }
#ifdef USE_MBP
        setupMBP( *_gScene );
#endif //USE_MBP

        return true;
    }

    bool PhysXSceneInterface::isInit() const noexcept
    {
        return _gScene != nullptr;
    }

#define SAFE_RELEASE(X) if (X != nullptr) { X->release(); X = nullptr;}
    void PhysXSceneInterface::release()
    {
        Console::d_printfn( Locale::Get( _ID( "STOP_PHYSX_SCENE_INTERFACE" ) ) );

        idle();

        SAFE_RELEASE( _cpuDispatcher );
        SAFE_RELEASE( _gScene );
    }

    void PhysXSceneInterface::idle()
    {
        if ( _gScene == nullptr )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        bool expected = true;
        if ( _rigidActorsQueued.compare_exchange_strong( expected, false ) )
        {
            PhysXActor* crtActor = nullptr;
            while ( _sceneRigidQueue.try_dequeue( crtActor ) )
            {
                _sceneRigidActors.push_back( crtActor );
                _gScene->addActor( *(crtActor->_actor) );
            }
        }
    }

    void PhysXSceneInterface::update( const U64 deltaTimeUS )
    {
        if ( _gScene == nullptr )
        {
            return;
        }
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        // retrieve array of actors that moved
        physx::PxU32 nbActiveActors = 0;
        physx::PxActor** activeActors = _gScene->getActiveActors( nbActiveActors );

        if ( nbActiveActors > 0u )
        {
            // update each render object with the new transform
            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = to_U32( nbActiveActors );
            descriptor._partitionSize = g_parallelPartitionSize;
            descriptor._cbk = [this, activeActors]( const Task*, const U32 start, const U32 end )
            {
                for ( U32 i = start; i < end; ++i )
                {
                    UpdateActor( activeActors[i] );
                }
            };

            parallel_for( parentScene().context(), descriptor );
        }
    }

    void PhysXSceneInterface::UpdateActor( physx::PxActor* actor )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        const physx::PxRigidActor* rigidActor = static_cast<physx::PxRigidActor*>(actor);
        TransformComponent* tComp = static_cast<SceneGraphNode*>(rigidActor->userData)->get<TransformComponent>();

        const physx::PxTransform pT = rigidActor->getGlobalPose();
        const physx::PxQuat pQ = pT.q.getConjugate();
        tComp->setRotation( Quaternion<F32>( pQ.x, pQ.y, pQ.z, pQ.w ) );
        tComp->setPosition( pT.p.x, pT.p.y, pT.p.z );
    }

    void PhysXSceneInterface::process( const U64 deltaTimeUS )
    {
        if ( _gScene != nullptr )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

            _gScene->simulate( Time::MicrosecondsToMilliseconds<physx::PxReal>( deltaTimeUS ) );

            bool block = false;
            while ( !_gScene->fetchResults( block ) )
            {
                idle();
                block = true;
            }
        }
    }

    void PhysXSceneInterface::addRigidActor( PhysXActor* const actor )
    {
        assert( actor != nullptr );
        // We DO NOT take ownership of actors. Ownership remains with RigidBodyComponent
        _sceneRigidQueue.enqueue( actor );
        _rigidActorsQueued.store( true );
    }

    void PhysXSceneInterface::updateRigidActor( physx::PxRigidActor* oldActor, physx::PxRigidActor* newActor ) const
    {
        if ( oldActor != nullptr )
        {
            _gScene->removeActor( *oldActor );
        }
        if ( newActor != nullptr )
        {
            _gScene->addActor( *newActor );
        }
    }

    bool PhysXSceneInterface::intersect( const Ray& intersectionRay, const vec2<F32> range, vector<SGNRayResult>& intersectionsOut ) const
    {
        physx::PxRaycastBuffer hit;

        const physx::PxHitFlags outputFlags = physx::PxHitFlag::eMESH_ANY | physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL;
        const physx::PxVec3 unitDir = Util::toVec3( intersectionRay._direction );
        const physx::PxVec3 origin = Util::toVec3( intersectionRay._origin );

        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        if ( _gScene->raycast( origin + unitDir * range.min, unitDir, range.max, hit, outputFlags ) )
        {
            const physx::PxU32 numHits = hit.getNbAnyHits();

            for ( physx::PxU32 i = 0; i < numHits; ++i )
            {
                hit.block = hit.getAnyHit( i );
                PX_ASSERT( hit.block.shape );
                PX_ASSERT( hit.block.actor );
                PX_ASSERT( hit.block.distance <= probeLength + extra );

                const SceneGraphNode* node = static_cast<SceneGraphNode*>(hit.block.actor->userData);
                intersectionsOut.push_back( {
                    node->getGUID(),
                    hit.block.distance,
                    false,
                    node->name().c_str()
                                            } );
            }
            return numHits > 0;
        }

        return false;
    }
};