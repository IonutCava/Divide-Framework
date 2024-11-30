#include "Headers/PhysX.h"
#include "Headers/PhysXActor.h"
#include "Headers/PhysXSceneInterface.h"

#include "Graphs/Headers/SceneGraphNode.h"
#include "Scenes/Headers/Scene.h"
#include "Utility/Headers/Localization.h"


#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/ResourcePath.h"
#include "Platform/Headers/PlatformDataTypes.h"
#include "Platform/Headers/PlatformDefines.h"
#include "Platform/Threading/Headers/SharedMutex.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Core/Headers/ErrorCodes.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Core/Headers/Profiler.h"
#include "Core/Math/Headers/Ray.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Vegetation/Headers/Vegetation.h"

#include <cassert>
#include <common/PxBase.h>
#include <config.h>
#include <cooking/PxCooking.h>
#include <cooking/PxTriangleMeshDesc.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultStreams.h>
#include <extensions/PxRigidActorExt.h>
#include <foundation/Px.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxErrors.h>
#include <foundation/PxFoundation.h>
#include <foundation/PxPreprocessor.h>
#include <foundation/PxSimpleTypes.h>
#include <geometry/PxBoxGeometry.h>
#include <geometry/PxGeometry.h>
#include <geometry/PxMeshScale.h>
#include <geometry/PxTriangleMesh.h>
#include <geometry/PxTriangleMeshGeometry.h>
#include <Graphs/Headers/SceneNodeFwd.h>
#include <Physics/Headers/PhysicsAPIWrapper.h>
#include <Physics/Headers/PhysicsAsset.h>

#include <PxPhysics.h>
#include <PxRigidBody.h>
#include <PxRigidDynamic.h>
#include <PxRigidStatic.h>
#include <PxShape.h>
#include <utility>

// Connecting the SDK to Visual Debugger
#if PX_SUPPORT_GPU_PHYSX
#include <physx/gpu/PxGpu.h>
#endif

#include <extensions/PxExtensionsAPI.h>
#include <pvd/PxPvd.h>
#include <pvd/PxPvdTransport.h>

#include <foundation/PxPhysicsVersion.h>
#include <cudamanager/PxCudaContextManager.h>
#include <common/PxTolerancesScale.h>
#include <PxDeletionListener.h>

// PhysX includes //

namespace Divide
{
    namespace
    {
        constexpr bool g_recordMemoryAllocations = false;
        const char* g_collisionMeshExtension = "DVDColMesh";

        const physx::PxTolerancesScale g_toleranceScale{};
        physx::PxPvdInstrumentationFlags  g_pvdFlags{};
        physx::PxDefaultAllocator g_gDefaultAllocatorCallback;

        const char* g_pvd_target_ip = "127.0.0.1";
        physx::PxU32 g_pvd_target_port = 5425;
        physx::PxU32 g_pvd_target_timeout_ms = 10;

        struct DeletionListener final : physx::PxDeletionListener
        {
            void onRelease( const physx::PxBase* observed, [[maybe_unused]] void* userData, [[maybe_unused]] physx::PxDeletionEventFlag::Enum deletionEvent ) override
            {
                if ( observed->is<physx::PxRigidActor>() )
                {
                    [[maybe_unused]] const physx::PxRigidActor* actor = static_cast<const physx::PxRigidActor*>(observed);
                    /*
                    removeRenderActorsFromPhysicsActor(actor);
                    vector<physx::PxRigidActor*>::iterator actorIter = std::find(_physicsActors.begin(), _physicsActors.end(), actor);
                    if (actorIter != _physicsActors.end()) {
                        _physicsActors.erase(actorIter);
                    }
                    */

                }
            }
        } g_deletionListener;

        struct DvdErrorCallback final : physx::PxErrorCallback
        {
            void reportError( const physx::PxErrorCode::Enum code, const char* message, const char* file, const int line ) override
            {
                switch ( code )
                {
                    case physx::PxErrorCode::eNO_ERROR:           Console::printfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "None", message, file, line ); return;
                    case physx::PxErrorCode::eDEBUG_INFO:         Console::d_printfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Debug Msg", message, file, line ); return;
                    case physx::PxErrorCode::eDEBUG_WARNING:      Console::d_warnf( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Debug Warn", message, file, line ); return;
                    case physx::PxErrorCode::eINVALID_PARAMETER:  Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Invalid Parameter", message, file, line ); return;
                    case physx::PxErrorCode::eINVALID_OPERATION:  Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Invalid Operation", message, file, line ); return;
                    case physx::PxErrorCode::eOUT_OF_MEMORY:      Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Mem", message, file, line ); return;
                    case physx::PxErrorCode::eINTERNAL_ERROR:     Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Internal", message, file, line ); return;
                    case physx::PxErrorCode::eABORT:              Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Abort", message, file, line ); return;
                    case physx::PxErrorCode::ePERF_WARNING:       Console::warnfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "Perf", message, file, line ); return;
                    case physx::PxErrorCode::eMASK_ALL:           Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "ALL", message, file, line ); return;
                    default: break;
                }
                Console::errorfn( LOCALE_STR( "ERROR_PHYSX_GENERIC" ), "UNKNOWN", message, file, line );
            }
        } g_physxErrorCallback;

    };

    hashMap<U64, physx::PxTriangleMesh*> PhysX::s_gMeshCache;
    SharedMutex PhysX::s_meshCacheLock;

    PhysX::PhysX( PlatformContext& context )
        : PhysicsAPIWrapper(context)
    {
    }

    ErrorCode PhysX::initPhysicsAPI( const U8 targetFrameRate, const F32 simSpeed )
    {

        // Make sure we always try to close as much of our API stuff as possible on failure
        bool init = false;
        SCOPE_EXIT{
            if ( !init && !closePhysicsAPI() )
            {
                Console::errorfn( LOCALE_STR( "ERROR_START_PHYSX_API" ) );
            }
        };

        Console::printfn( LOCALE_STR( "START_PHYSX_API" ) ) ;

        _simulationSpeed = simSpeed;
        // create foundation object with default error and allocator callbacks.
        _foundation = PxCreateFoundation( PX_PHYSICS_VERSION, g_gDefaultAllocatorCallback, g_physxErrorCallback );
        if ( _foundation == nullptr )
        {
            return ErrorCode::PHYSX_INIT_ERROR;
        }

#if PX_SUPPORT_GPU_PHYSX
        physx::PxCudaContextManagerDesc cudaContextManagerDesc;
        _cudaContextManager = PxCreateCudaContextManager( *_foundation, cudaContextManagerDesc, PxGetProfilerCallback() );
        if ( _cudaContextManager != nullptr )
        {
            if ( !_cudaContextManager->contextIsValid() )
            {
                _cudaContextManager->release();
                _cudaContextManager = nullptr;
            }
        }
#endif //PX_SUPPORT_GPU_PHYSX
        if constexpr( Config::Build::IS_DEBUG_BUILD || Config::Build::IS_PROFILE_BUILD )
        {
            createPvdConnection( g_pvd_target_ip,
                                 g_pvd_target_port,
                                 g_pvd_target_timeout_ms,
                                 Config::Build::IS_DEBUG_BUILD );
        }

        _gPhysicsSDK = PxCreatePhysics( PX_PHYSICS_VERSION, *_foundation, g_toleranceScale, g_recordMemoryAllocations, _pvd );

        if ( _gPhysicsSDK == nullptr )
        {
            Console::errorfn( LOCALE_STR( "ERROR_START_PHYSX_API" ) );
            return ErrorCode::PHYSX_INIT_ERROR;
        }

        if ( !PxInitExtensions( *_gPhysicsSDK, _pvd ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_EXTENSION_PHYSX_API" ) );
            return ErrorCode::PHYSX_EXTENSION_ERROR;
        }

        _gPhysicsSDK->registerDeletionListener( g_deletionListener, physx::PxDeletionEventFlag::eUSER_RELEASE );

        //ToDo: Add proper material controls to RigidBodyComponent -Ionut
        _defaultMaterial = _gPhysicsSDK->createMaterial( 0.5f, 0.5f, 0.1f );
        if ( _defaultMaterial == nullptr )
        {
            Console::errorfn( LOCALE_STR( "ERROR_START_PHYSX_API" ) );
            return ErrorCode::PHYSX_INIT_ERROR;
        }

        init = true;
        updateTimeStep( targetFrameRate, _simulationSpeed );
        Console::printfn( LOCALE_STR( "START_PHYSX_API_OK" ) );

        return ErrorCode::NO_ERR;
    }

    #define SAFE_RELEASE(X) if (X != nullptr) { X->release(); X = nullptr;} static_assert(true, "")

    bool PhysX::closePhysicsAPI()
    {
        if ( !_gPhysicsSDK )
        {
            return false;
        }

        Console::printfn( LOCALE_STR( "STOP_PHYSX_API" ) );

        DIVIDE_ASSERT( _targetScene == nullptr, "PhysX error: target scene not destroyed before calling closePhysicsAPI." );

        SAFE_RELEASE( _gPhysicsSDK );
        PxCloseExtensions();
        SAFE_RELEASE( _cudaContextManager );
        SAFE_RELEASE( _pvd );
        SAFE_RELEASE( _transport );
        SAFE_RELEASE( _foundation );

        return true;
    }

    void PhysX::togglePvdConnection() const
    {
        if ( _pvd == nullptr )
        {
            return;
        }

        if ( _pvd->isConnected() )
        {
            _pvd->disconnect();
        }
        else
        {
            if ( _pvd->connect( *_transport, g_pvdFlags ) )
            {
                Console::d_printfn( LOCALE_STR( "CONNECT_PVD_OK" ) );
            }
        }
    }

    void PhysX::createPvdConnection( const char* ip, const physx::PxU32 port, const physx::PxU32 timeout, const bool useFullConnection )
    {
        //Create a pvd connection that writes data straight to the filesystem.  This is
        //the fastest connection on windows for various reasons.  First, the transport is quite fast as
        //pvd writes data in blocks and filesystems work well with that abstraction.
        //Second, you don't have the PVD application parsing data and using CPU and memory bandwidth
        //while your application is running.
        //physx::PxPvdTransport* transport = physx::PxDefaultPvdFileTransportCreate( "c:\\mywork\\sample.pxd2" );

        //The normal way to connect to pvd.  PVD needs to be running at the time this function is called.
        //We don't worry about the return value because we are already registered as a listener for connections
        //and thus our onPvdConnected call will take care of setting up our basic connection state.
        _transport = physx::PxDefaultPvdSocketTransportCreate( ip, port, timeout );
        if ( _transport == nullptr )
        {
            return;
        }

        //The connection flags state overall what data is to be sent to PVD.  Currently
        //the Debug connection flag requires support from the implementation (don't send
        //the data when debug isn't set) but the other two flags, profile and memory
        //are taken care of by the PVD SDK.

        //Use these flags for a clean profile trace with minimal overhead
        g_pvdFlags = useFullConnection ? physx::PxPvdInstrumentationFlag::eALL : physx::PxPvdInstrumentationFlag::ePROFILE;
        _pvd = physx::PxCreatePvd( *_foundation );
        if ( _pvd->connect( *_transport, g_pvdFlags ) )
        {
            Console::d_printfn( LOCALE_STR( "CONNECT_PVD_OK" ) );
        }
    }


    /// Process results
    void PhysX::frameStartedInternal( const U64 deltaTimeGameUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        if ( _targetScene != nullptr )
        {
            _targetScene->frameStarted( deltaTimeGameUS );
        }
    }

    /// Update actors
    void PhysX::frameEndedInternal( const U64 deltaTimeGameUS )
    {
        if ( _targetScene != nullptr ) [[likely]]
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
            _targetScene->frameEnded( deltaTimeGameUS );
        }
    }

    void PhysX::idle()
    {
        if ( _targetScene != nullptr ) [[likely]]
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
            _targetScene->idle();
        }
    }

    bool PhysX::initPhysicsScene( Scene& scene )
    {
        if ( _targetScene != nullptr ) [[likely]]
        {
            const I64 currentScene = _targetScene->parentScene().getGUID();
            const I64 callingScene = scene.getGUID();
            if ( currentScene == callingScene )
            {
                if ( !_targetScene->isInit() )
                {
                    if ( !_targetScene->init() )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }
                }
                // nothing to do.
                return true;
            }

            if ( !destroyPhysicsScene( _targetScene->parentScene() ) )
            {
                DIVIDE_UNEXPECTED_CALL_MSG( "Failed to destroy active physics scene!" );
            }
        }

        DIVIDE_ASSERT( _targetScene == nullptr );
        _targetScene = std::make_unique<PhysXSceneInterface>( scene );
        return _targetScene->init();
    }

    bool PhysX::destroyPhysicsScene( const Scene& scene )
    {
        if ( _targetScene != nullptr ) [[likely]]
        {
            // Because we can load scenes in the background, our current active scene might not
            // be the one the calling scene wants to destroy.
            if ( _targetScene->parentScene().getGUID() != scene.getGUID() )
            {
                return false;
            }
            /// Destroy physics (:D)
            _targetScene.reset();
        }

        return true;
    }

    physx::PxRigidActor* PhysX::createActorForGroup( const PhysicsGroup group, const physx::PxTransform& pose )
    {
        physx::PxRigidActor* ret = nullptr;
        switch ( group )
        {
            case PhysicsGroup::GROUP_STATIC:
                ret = _gPhysicsSDK->createRigidStatic( pose );
                break;
            case PhysicsGroup::GROUP_DYNAMIC:
                ret = _gPhysicsSDK->createRigidDynamic( pose );
                break;
            case PhysicsGroup::GROUP_KINEMATIC:
            {
                ret = _gPhysicsSDK->createRigidDynamic( pose );
                assert( ret != nullptr );
                auto dynamicActor = static_cast<physx::PxRigidBody*>(ret);
                dynamicActor->setRigidBodyFlag( physx::PxRigidBodyFlag::Enum::eKINEMATIC, true );
            }break;
            default: DIVIDE_UNEXPECTED_CALL(); break; //Not implemented yet
        }

        return ret;
    }

    PhysicsAsset* PhysX::createRigidActor( SceneGraphNode* node, RigidBodyComponent& parentComp )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        PhysXActor* newActor = new PhysXActor( parentComp );

        const TransformComponent* tComp = node->get<TransformComponent>();
        assert( tComp != nullptr );

        const float3& position = tComp->getWorldPosition();
        const float4 orientation = tComp->getWorldOrientation().asVec4();
        const physx::PxTransform posePxTransform( Util::toVec3( position ), physx::PxQuat( orientation.x, orientation.y, orientation.z, orientation.w ).getConjugate() );

        newActor->_actor = createActorForGroup( parentComp.physicsCollisionGroup(), posePxTransform );
        if ( newActor->_actor == nullptr )
        {
            delete newActor;
            return nullptr;
        }

        newActor->_actor->userData = node;
        SceneNode& sNode = node->getNode();
        const auto meshName = sNode.assetName().empty() ? sNode.resourceName() : sNode.assetName();
        const U64 nameHash = _ID( meshName.c_str() );

        ResourcePath cachePath = Paths::g_collisionMeshCacheLocation / meshName;
        cachePath.append(".");
        cachePath.append(g_collisionMeshExtension);
        const string cachePathStr = cachePath.string();

        if ( Is3DObject( sNode.type() ) )
        {
            newActor->_type = physx::PxGeometryType::eTRIANGLEMESH;

            physx::PxTriangleMesh* nodeGeometry = nullptr;
            {
                SharedLock<SharedMutex> r_lock( s_meshCacheLock );
                const auto it = s_gMeshCache.find( nameHash );
                if ( it != s_gMeshCache.end() )
                {
                    nodeGeometry = it->second;
                    Console::printfn( LOCALE_STR( "COLLISION_MESH_LOADED_FROM_RAM" ), meshName );
                }
            }

            if ( nodeGeometry == nullptr )
            {
                LockGuard<SharedMutex> w_lock( s_meshCacheLock );
                // Check again to avoid race conditions
                const auto it = s_gMeshCache.find( nameHash );
                if ( it != s_gMeshCache.end() )
                {
                    nodeGeometry = it->second;
                    Console::printfn( LOCALE_STR( "COLLISION_MESH_LOADED_FROM_RAM" ), meshName );
                }
                else
                {
                    Object3D& obj = node->getNode<Object3D>();
                    const U8 lodCount = obj.getGeometryPartitionCount();
                    const U16 partitionID = obj.getGeometryPartitionID( lodCount - 1 );
                    const vector<uint3>& triangles = obj.getTriangles( partitionID );

                    if ( triangles.empty() )
                    {
                        delete newActor;
                        return nullptr;
                    }

                    const bool collisionMeshFileExists = fileExists( cachePath );
                    if ( !collisionMeshFileExists && !pathExists( Paths::g_collisionMeshCacheLocation ) )
                    {
                        if ( createDirectory( Paths::g_collisionMeshCacheLocation ) != FileError::NONE)
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    }
                    if ( !collisionMeshFileExists )
                    {
                        physx::PxTriangleMeshDesc meshDesc;
                        meshDesc.points.stride = sizeof( VertexBuffer::Vertex );

                        meshDesc.triangles.count = static_cast<physx::PxU32>(triangles.size());
                        meshDesc.triangles.stride = sizeof( triangles.front() );
                        meshDesc.triangles.data = triangles.data();

                        physx::PxDefaultFileOutputStream outputStream( cachePathStr.c_str() );
                        if ( obj.type() == SceneNodeType::TYPE_TERRAIN )
                        {
                            const auto& verts = node->getNode<Terrain>().getVerts();
                            meshDesc.points.count = static_cast<physx::PxU32>(verts.size());
                            meshDesc.points.data = verts[0]._position._v;
                        }
                        else
                        {
                            DIVIDE_ASSERT( obj.geometryBuffer() != nullptr );
                            meshDesc.points.count = static_cast<physx::PxU32>(obj.geometryBuffer()->getVertexCount());
                            meshDesc.points.data = obj.geometryBuffer()->getVertices()[0]._position._v;
                        }

                        const auto getErrorMessage = []( const physx::PxTriangleMeshCookingResult::Enum value )
                        {
                            switch ( value )
                            {
                                case physx::PxTriangleMeshCookingResult::Enum::eSUCCESS:
                                    return "Success";
                                case physx::PxTriangleMeshCookingResult::Enum::eLARGE_TRIANGLE:
                                    return "A triangle is too large for well-conditioned results. Tessellate the mesh for better behavior, see the user guide section on cooking for more details.";
                                case physx::PxTriangleMeshCookingResult::Enum::eFAILURE:
                                    return "Something unrecoverable happened. Check the error stream to find out what.";
                                default: break;
                            }

                            return "UNKNWOWN";
                        };

                        physx::PxCookingParams params( g_toleranceScale );
                        params.meshWeldTolerance = 0.001f;
                        params.meshPreprocessParams = physx::PxMeshPreprocessingFlags( physx::PxMeshPreprocessingFlag::eWELD_VERTICES );
#if PX_SUPPORT_GPU_PHYSX
                        params.buildGPUData = true; //Enable GRB data being produced in cooking.
#else
                        params.buildGPUData = false;
#endif
                        params.midphaseDesc = physx::PxMeshMidPhase::Enum::eBVH34;

                        physx::PxTriangleMeshCookingResult::Enum result;
                        if ( !PxCookTriangleMesh( params, meshDesc, outputStream, &result ) )
                        {
                            STUBBED( "ToDo: If we fail to build/load a collision mesh, fallback to an AABB aproximation -Ionut" );
                            Console::errorfn( LOCALE_STR( "ERROR_COOK_TRIANGLE_MESH" ), getErrorMessage( result ) );
                        }
                    }
                    else
                    {
                        Console::printfn( LOCALE_STR( "COLLISION_MESH_LOADED_FROM_FILE" ), meshName );
                    }

                    physx::PxDefaultFileInputData inData( cachePathStr.c_str() );
                    nodeGeometry = _gPhysicsSDK->createTriangleMesh( inData );
                    if ( nodeGeometry )
                    {
                        hashAlg::insert( s_gMeshCache, nameHash, nodeGeometry );
                    }
                    else
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_CREATE_TRIANGLE_MESH" ) );
                    }
                }
            }
            if ( nodeGeometry != nullptr )
            {
                const float3& scale = tComp->getWorldScale();
                const physx::PxTriangleMeshGeometry geometry = {
                    nodeGeometry,
                    physx::PxMeshScale( physx::PxVec3( scale.x, scale.y, scale.z ),
                                       physx::PxQuat( physx::PxIdentity ) )
                };

                STUBBED( "PhysX implementation only uses one shape per actor for now! -Ionut" );
                physx::PxRigidActorExt::createExclusiveShape( *newActor->_actor, geometry, *_defaultMaterial );

            }
        }
        else if ( sNode.type() == SceneNodeType::TYPE_INFINITEPLANE ||
                  sNode.type() == SceneNodeType::TYPE_WATER ||
                  sNode.type() == SceneNodeType::TYPE_PARTICLE_EMITTER )
        {
            // Use AABB
            const BoundsComponent* bComp = node->get<BoundsComponent>();
            assert( bComp != nullptr );
            const float3 hExtent = bComp->getBoundingBox().getHalfExtent();

            newActor->_type = physx::PxGeometryType::eBOX;

            const physx::PxBoxGeometry geometry = { hExtent.x, hExtent.y, hExtent.z };
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape( *newActor->_actor, geometry, *_defaultMaterial );
            if ( sNode.type() == SceneNodeType::TYPE_WATER )
            {
                // Water geom has the range  [0, depth] and half extents work from [-half depth, half depth]
                // so offset the local pose by half
                auto crtPose = shape->getLocalPose();
                crtPose.p.y -= hExtent.y;
                shape->setLocalPose( crtPose );
            }
        }
        else
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        // If we got here, the new actor was just created (didn't exist previously in the scene), so add it
        _targetScene->addRigidActor( newActor );

        return newActor;
    }

    bool PhysX::convertActor( PhysicsAsset* actor, const PhysicsGroup newGroup )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );

        PhysXActor* targetActor = dynamic_cast<PhysXActor*>(actor);

        if ( targetActor != nullptr )
        {
            physx::PxRigidActor* newActor = createActorForGroup( newGroup, targetActor->_actor->getGlobalPose() );
            DIVIDE_ASSERT( newActor != nullptr );

            const physx::PxU32 nShapes = targetActor->_actor->getNbShapes();
            vector<physx::PxShape*> shapes( nShapes );
            targetActor->_actor->getShapes( shapes.data(), nShapes );
            for ( physx::PxU32 i = 0; i < nShapes; ++i )
            {
                newActor->attachShape( *shapes[i] );
            }

            _targetScene->updateRigidActor( targetActor->_actor, newActor );
            targetActor->_actor = newActor;
            return true;
        }

        return false;
    }

    bool PhysX::intersect( const Ray& intersectionRay, const float2 range, vector<SGNRayResult>& intersectionsOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Physics );
        return _targetScene->intersect( intersectionRay, range, intersectionsOut );
    }

};
