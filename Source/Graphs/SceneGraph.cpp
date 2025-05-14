

#include "Headers/SceneGraph.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/FrameListenerManager.h"
#include "Managers/Headers/ProjectManager.h"
#include "Utility/Headers/Localization.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Physics/Headers/PXDevice.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "Platform/File/Headers/FileManagement.h"

#include "ECS/Systems/Headers/ECSManager.h"
#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

#include <ECS/EntityManager.h>

namespace Divide
{

    namespace
    {
        constexpr U16 BYTE_BUFFER_VERSION = 1u;
        constexpr U32 g_cacheMarkerByteValue[2]{ 0xDEADBEEF, 0xBADDCAFE };
        constexpr U32 g_nodesPerPartition = 32u;
    };

    BoundingSphere SceneGraph::GetBounds( const SceneGraphNode* sgn )
    {
        BoundingSphere ret{ VECTOR3_ZERO , EPSILON_F32 };
        if ( sgn != nullptr )
        {
            const BoundsComponent* bComp = sgn->get<BoundsComponent>();
            if ( bComp != nullptr )
            {
                return bComp->getBoundingSphere();
            }

            const TransformComponent* tComp = sgn->get<TransformComponent>();
            if ( tComp != nullptr )
            {
                ret.setCenter( tComp->getWorldPosition() );
                ret.setRadius( std::max( tComp->getLocalScale().maxComponent() * 2.f, 1.f ) );
            }
        }

        return ret;
    }

    SceneGraph::SceneGraph( Scene& parentScene )
        : FrameListener( "SceneGraph", parentScene.context().kernel().frameListenerMgr(), 1 )
        , SceneComponent( parentScene )
    {
        _ecsManager = std::make_unique<ECSManager>( parentScene.context(), GetECSEngine() );
    }

    SceneGraph::~SceneGraph()
    {
        DIVIDE_ASSERT(_root == nullptr);
    }

    SceneGraphNode* SceneGraph::createSceneGraphNode( PlatformContext& context, SceneGraph* sceneGraph, const SceneGraphNodeDescriptor& descriptor )
    {   
        LockGuard<Mutex> u_lock( _nodeCreateMutex );
        const ECS::EntityId nodeID = GetEntityManager()->CreateEntity<SceneGraphNode>( context, sceneGraph, descriptor );
        return static_cast<SceneGraphNode*>(GetEntityManager()->GetEntity( nodeID ));
    }

    void SceneGraph::load()
    {
        DIVIDE_ASSERT( _root == nullptr );

        ResourceDescriptor<TransformNode> nodeDescriptor{"ROOT"};
        
        SceneGraphNodeDescriptor rootDescriptor = {};
        rootDescriptor._name = "ROOT";
        rootDescriptor._nodeHandle = FromHandle(CreateResource(  nodeDescriptor ));
        rootDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS );
        rootDescriptor._usageContext = NodeUsageContext::NODE_STATIC;

       _root = createSceneGraphNode( parentScene().context(), this, rootDescriptor );
        onNodeAdd( _root );
    }

    void SceneGraph::unload()
    {
        Console::d_printfn( LOCALE_STR( "DELETE_SCENEGRAPH" ) );

        destroySceneGraphNode( _root );
        DIVIDE_ASSERT( _root == nullptr );

    }

    void SceneGraph::addToDeleteQueue( SceneGraphNode* node, const size_t childIdx )
    {
        LockGuard<SharedMutex> w_lock( _pendingDeletionLock );
        vector<size_t>& list = _pendingDeletion[node];
        if ( eastl::find( cbegin( list ), cend( list ), childIdx ) == cend( list ) )
        {
            list.push_back( childIdx );
        }
    }

    void SceneGraph::onNodeUpdated( const SceneGraphNode& node )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        //ToDo: Maybe add particles too? -Ionut
        if ( Is3DObject( node.getNode().type() ) )
        {
            SceneEnvironmentProbePool* probes = Attorney::SceneGraph::getEnvProbes( &parentScene() );
            probes->onNodeUpdated( node );
        }
        else if ( node.getNode().type() == SceneNodeType::TYPE_SKY )
        {
            SceneEnvironmentProbePool::SkyLightNeedsRefresh( true );
        }
    }

    void SceneGraph::onNodeSpatialChange( const SceneGraphNode& node )
    {
        BoundsComponent* bComp = node.get<BoundsComponent>();
        if ( bComp != nullptr )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

            LightPool* pool = Attorney::SceneGraph::getLightPool( &parentScene() );
            pool->onVolumeMoved( bComp->getBoundingSphere(), node.usageContext() == NodeUsageContext::NODE_STATIC );

            if ( bComp->collisionsEnabled() )
            {
                Attorney::SceneGraphNodeSceneGraph::updateCollisions( node, *getRoot(), _intersectionsCache, _intersectionsLock );
            }
        }

        Attorney::SceneGraph::onNodeSpatialChange(&parentScene(), node);
    }

    void SceneGraph::onNodeMoved( const SceneGraphNode& node )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );
        onNodeUpdated( node );
    }

    void SceneGraph::onNodeDestroy( SceneGraphNode* oldNode )
    {
        const I64 guid = oldNode->getGUID();

        if ( guid == _root->getGUID() )
        {
            return;
        }

        {
            LockGuard<SharedMutex> w_lock( _nodesByTypeLock );
            erase_if( _nodesByType[to_base( oldNode->getNode().type() )],
                      [guid]( SceneGraphNode* node )-> bool
                      {
                          return node && node->getGUID() == guid;
                      } );
        }
        {
            LockGuard<Mutex> w_lock( _nodeEventLock );
            erase_if( _nodeEventQueue,
                      [guid]( SceneGraphNode* node )-> bool
                      {
                          return node && node->getGUID() == guid;
                      } );
        }
        {
            LockGuard<Mutex> w_lock( _nodeParentChangeLock );
            erase_if( _nodeParentChangeQueue,
                      [guid]( SceneGraphNode* node )-> bool
                      {
                          return node && node->getGUID() == guid;
                      } );
        }

        Attorney::SceneGraph::onNodeDestroy( &parentScene(), oldNode );

        _nodeListChanged = true;
    }

    void SceneGraph::onNodeAdd( SceneGraphNode* newNode )
    {
        {
            LockGuard<SharedMutex> w_lock( _nodesByTypeLock );
            _nodesByType[to_base( newNode->getNode().type() )].push_back( newNode );
        }
        _nodeListChanged = true;
    }

    bool SceneGraph::removeNodesByType( const SceneNodeType nodeType )
    {
        return _root != nullptr && getRoot()->removeNodesByType( nodeType );
    }

    bool SceneGraph::removeNode( const I64 guid )
    {
        return removeNode( findNode( guid ) );
    }

    bool SceneGraph::removeNode( SceneGraphNode* node )
    {
        if ( node )
        {
            SceneGraphNode* parent = node->parent();
            if ( parent )
            {
                if ( !parent->removeChildNode( node, true ) )
                {
                    return false;
                }
            }

            return true;
        }

        return false;
    }

    bool SceneGraph::frameStarted( [[maybe_unused]] const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        // Gather all nodes at the start of the frame only if we added/removed any of them
        if ( _nodeListChanged )
        {
            // Very rarely called
            efficient_clear( _nodeList );
            Attorney::SceneGraphNodeSceneGraph::getAllNodes( _root, _nodeList );
            _nodeListChanged = false;
        }

        {
            PROFILE_SCOPE( "ECS::OnFrameStart", Profiler::Category::Scene );
            GetECSEngine().OnFrameStart();
        }
        return true;
    }

    bool SceneGraph::frameEnded( [[maybe_unused]] const FrameEvent& evt )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        {
            PROFILE_SCOPE( "ECS::OnFrameEnd", Profiler::Category::Scene );
            GetECSEngine().OnFrameEnd();
        }
        {
            PROFILE_SCOPE( "Process parent change queue", Profiler::Category::Scene );
            LockGuard<Mutex> w_lock( _nodeParentChangeLock );
            for ( SceneGraphNode* node : _nodeParentChangeQueue )
            {
                Attorney::SceneGraphNodeSceneGraph::changeParent( node );
            }
            efficient_clear( _nodeParentChangeQueue );
        }
        {
            LockGuard<SharedMutex> lock( _pendingDeletionLock );
            if ( !_pendingDeletion.empty() )
            {
                for ( auto entry : _pendingDeletion )
                {
                    if ( entry.first != nullptr )
                    {
                        Attorney::SceneGraphNodeSceneGraph::processDeleteQueue( entry.first, entry.second );
                    }
                }
                _pendingDeletion.clear();
            }
        }
        return true;
    }

    void SceneGraph::sceneUpdate( const U64 deltaTimeUS, SceneState& sceneState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const F32 msTime = Time::MicrosecondsToMilliseconds<F32>( deltaTimeUS );

        TaskPool& threadPool = parentScene().context().taskPool(TaskPoolType::HIGH_PRIORITY);

        {
            PROFILE_SCOPE( "ECS::PreUpdate", Profiler::Category::Scene );
            GetECSEngine().PreUpdate( msTime );
        }
        {
            PROFILE_SCOPE( "ECS::Update", Profiler::Category::Scene );
            GetECSEngine().Update( msTime );
        }
        {
            PROFILE_SCOPE( "ECS::PostUpdate", Profiler::Category::Scene );
            GetECSEngine().PostUpdate( msTime );
        }
        {
            PROFILE_SCOPE( "Process node scene update", Profiler::Category::Scene );
            Parallel_For
            (
                threadPool,
                ParallelForDescriptor
                {
                    ._iterCount = to_U32(_nodeList.size()),
                    ._partitionSize = g_nodesPerPartition
                },
                [&]( const Task* /*parentTask*/, const U32 start, const U32 end )
                {
                    for ( U32 i = start; i < end; ++i )
                    {
                        _nodeList[i]->sceneUpdate( deltaTimeUS, sceneState );
                    }
                }
            );
        
        }
        {
            PROFILE_SCOPE( "Process event queue", Profiler::Category::Scene );
            LockGuard<Mutex> w_lock( _nodeEventLock );
            Parallel_For
            ( 
                threadPool,
                ParallelForDescriptor
                {
                    ._iterCount = to_U32(_nodeEventQueue.size()),
                    ._partitionSize = g_nodesPerPartition
                }, 
                [this]( const Task* /*parentTask*/, const U32 start, const U32 end )
                {
                    for ( U32 i = start; i < end; ++i )
                    {
                        Attorney::SceneGraphNodeSceneGraph::processEvents( _nodeEventQueue[i] );
                    }
                }
            );

            efficient_clear( _nodeEventQueue );
        }

        {
            PROFILE_SCOPE( "Process intersections", Profiler::Category::Scene );
            LockGuard<Mutex> w_lock( _intersectionsLock );
            for ( const IntersectionRecord& ir : _intersectionsCache )
            {
                HandleIntersection( ir );
            }
            _intersectionsCache.resize( 0 );
        }
    }

    void SceneGraph::HandleIntersection( const IntersectionRecord& intersection )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const BoundsComponent* obj1 = intersection._intersectedObject1;
        const BoundsComponent* obj2 = intersection._intersectedObject2;
        if ( obj1 != nullptr && obj2 != nullptr )
        {
            // Check for child / parent relation
            if ( obj1->parentSGN()->isRelated( obj2->parentSGN() ) )
            {
                return;
            }

            RigidBodyComponent* comp1 = obj1->parentSGN()->get<RigidBodyComponent>();
            RigidBodyComponent* comp2 = obj2->parentSGN()->get<RigidBodyComponent>();

            if ( comp1 && comp2 )
            {
                comp1->onCollision( *comp2 );
                comp2->onCollision( *comp1 );
            }
        }
    }
    void SceneGraph::onNetworkSend( const U32 frameCount )
    {
        Attorney::SceneGraphNodeSceneGraph::onNetworkSend( _root, frameCount );
    }

    bool SceneGraph::intersect( const SGNIntersectionParams& params, vector<SGNRayResult>& intersectionsOut ) const
    {
        efficient_clear( intersectionsOut );

        // Try to leverage our physics system as it will always be way more faster and accurate
        if ( !parentScene().context().pfx().intersect( params._ray, params._range, intersectionsOut ) )
        {
            // Fallback to Sphere/AABB/OBB intersections
            const IntersectionRay intersectRay = GetIntersectionRay(params._ray);

            if ( !_root->intersect(intersectRay, params._range, intersectionsOut ) )
            {
                return false;
            }
        }


        DIVIDE_ASSERT( !intersectionsOut.empty() );

        const auto isIgnored = [&params]( const SceneNodeType type )
        {
            for ( size_t i = 0; i < params._ignoredTypesCount; ++i )
            {
                if ( type == params._ignoredTypes[i] )
                {
                    return true;
                }
            }
            return false;
        };

        for ( SGNRayResult& result : intersectionsOut )
        {
            SceneGraphNode* node = findNode( result.sgnGUID );
            const SceneNodeType snType = node->getNode().type();

            if ( isIgnored( snType ) || (!params._includeTransformNodes && IsTransformNode( snType )) )
            {
                result.sgnGUID = -1;
            }
        }

        erase_if( intersectionsOut,
                  []( const SGNRayResult& res )
                  {
                      return res.dist < 0.f || res.sgnGUID == -1;
                  } );

        return !intersectionsOut.empty();
    }

    void SceneGraph::postLoad()
    {
        NOP();
    }

    void SceneGraph::destroySceneGraphNode( SceneGraphNode*& node, const bool inPlace )
    {
        if ( node )
        {
            if ( inPlace )
            {
                GetEntityManager()->DestroyAndRemoveEntity( node->GetEntityID() );
            }
            else
            {
                GetEntityManager()->DestroyEntity( node->GetEntityID() );
            }
            node = nullptr;
        }
    }

    size_t SceneGraph::getTotalNodeCount() const noexcept
    {
        size_t ret = 0;

        SharedLock<SharedMutex> r_lock( _nodesByTypeLock );
        for ( const auto& nodes : _nodesByType )
        {
            ret += nodes.size();
        }

        return ret;
    }

    const vector<SceneGraphNode*>& SceneGraph::getNodesByType( const SceneNodeType type ) const
    {
        SharedLock<SharedMutex> r_lock( _nodesByTypeLock );
        return _nodesByType[to_base( type )];
    }

    ECS::EntityManager* SceneGraph::GetEntityManager()
    {
        return GetECSEngine().GetEntityManager();
    }

    ECS::EntityManager* SceneGraph::GetEntityManager() const
    {
        return GetECSEngine().GetEntityManager();
    }

    ECS::ComponentManager* SceneGraph::GetComponentManager()
    {
        return GetECSEngine().GetComponentManager();
    }

    ECS::ComponentManager* SceneGraph::GetComponentManager() const
    {
        return GetECSEngine().GetComponentManager();
    }

    SceneGraphNode* SceneGraph::findNode( const Str<128>& name, const bool sceneNodeName ) const
    {
        return findNode( _ID( name.c_str() ), sceneNodeName );
    }

    SceneGraphNode* SceneGraph::findNode( const U64 nameHash, const bool sceneNodeName ) const
    {
        const U64 cmpHash = sceneNodeName ? _ID( _root->getNode().resourceName().c_str() ) : _ID( _root->name().c_str() );

        if ( cmpHash == nameHash )
        {
            return _root;
        }

        return _root->findChild( nameHash, sceneNodeName, true );
    }

    SceneGraphNode* SceneGraph::findNode( const I64 guid ) const
    {
        if ( _root->getGUID() == guid )
        {
            return _root;
        }

        return _root->findChild( guid, false, true );
    }

    bool SceneGraph::saveCache( ByteBuffer& outputBuffer ) const
    {
        const std::function<bool( SceneGraphNode*, ByteBuffer& )> saveNodes = [&]( SceneGraphNode* sgn, ByteBuffer& outputBuffer )
        {
            // Because loading is async, nodes will not be necessarily in the same order. We need a way to find
            // the node using some sort of ID. Name based ID is bad, but is the only system available at the time of writing -Ionut
            outputBuffer << _ID( sgn->name().c_str() );
            if ( !Attorney::SceneGraphNodeSceneGraph::saveCache( sgn, outputBuffer ) )
            {
                NOP();
            }

            // Data may be bad, so add markers to be able to just jump over the entire node data instead of attempting partial loads
            outputBuffer.addMarker( g_cacheMarkerByteValue );

            {
                const SceneGraphNode::ChildContainer& children = sgn->getChildren();
                SharedLock<SharedMutex> r_lock( children._lock );
                const U32 childCount = children._count;
                for ( U32 i = 0u; i < childCount; ++i )
                {
                    if ( !saveNodes( children._data[i], outputBuffer ) )
                    {
                        NOP();
                    }
                }
            }

            return true;
        };

        outputBuffer << BYTE_BUFFER_VERSION;

        if ( saveNodes( _root, outputBuffer ) )
        {
            outputBuffer << _ID( _root->name().c_str() );
            return true;
        }

        return false;
    }

    bool SceneGraph::loadCache( ByteBuffer& inputBuffer )
    {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        inputBuffer >> tempVer;
        if ( tempVer == BYTE_BUFFER_VERSION )
        {
            const U64 rootID = _ID( _root->name().c_str() );

            U64 nodeID = 0u;

            bool skipRoot = true;
            bool missingData = false;
            do
            {
                if ( !inputBuffer.bufferEmpty() )
                {
                    inputBuffer >> nodeID;
                    if ( nodeID == rootID && !skipRoot )
                    {
                        break;
                    }

                    SceneGraphNode* node = findNode( nodeID, false );

                    if ( node == nullptr || !Attorney::SceneGraphNodeSceneGraph::loadCache( node, inputBuffer ) )
                    {
                        missingData = true;
                    }

                    inputBuffer.readSkipToMarker( g_cacheMarkerByteValue );
                }
                else
                {
                    missingData = true;
                    break;
                }
                if ( nodeID == rootID && skipRoot )
                {
                    skipRoot = false;
                    nodeID = 0u;
                }
            }
            while ( nodeID != rootID );

            return !missingData;
        }

        return false;
    }

    namespace
    {
        constexpr size_t g_sceneGraphVersion = 1;

        boost::property_tree::ptree dumpSGNtoAssets( SceneGraphNode* node )
        {
            boost::property_tree::ptree entry;
            entry.put( "<xmlattr>.name", node->name().c_str() );
            entry.put( "<xmlattr>.type", Names::sceneNodeType[to_base( node->getNode().type() )] );

            const SceneGraphNode::ChildContainer& children = node->getChildren();
            SharedLock<SharedMutex> r_lock( children._lock );
            const U32 childCount = children._count;
            for ( U32 i = 0u; i < childCount; ++i )
            {
                if ( children._data[i]->serialize() )
                {
                    entry.add_child( "node", dumpSGNtoAssets( children._data[i] ) );
                }
            }

            return entry;
        }
    };

    void SceneGraph::saveToXML( const ResourcePath& assetsFile, DELEGATE<void, std::string_view> msgCallback ) const
    {
        const ResourcePath sceneLocation = Scene::GetSceneFullPath(parentScene());

        {
            boost::property_tree::ptree pt;
            pt.put( "version", g_sceneGraphVersion );
            pt.add_child( "entities.node", dumpSGNtoAssets( _root ) );

            const FileError backupReturnCode = copyFile( sceneLocation, assetsFile.string(), sceneLocation, Util::StringFormat("{}.bak", assetsFile.string()), true );
            if ( backupReturnCode != FileError::NONE &&
                 backupReturnCode != FileError::FILE_NOT_FOUND &&
                 backupReturnCode != FileError::FILE_EMPTY )
            {
                if constexpr( !Config::Build::IS_SHIPPING_BUILD )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
            else
            {
                XML::writeXML(sceneLocation / assetsFile, pt );
            }
        }

        const SceneGraphNode::ChildContainer& children = _root->getChildren();
        SharedLock<SharedMutex> r_lock( children._lock );
        const U32 childCount = children._count;
        for ( U32 i = 0u; i < childCount; ++i )
        {
            children._data[i]->saveToXML( sceneLocation, msgCallback );
        }
    }

    namespace
    {
        boost::property_tree::ptree g_emptyPtree;
    }

    void SceneGraph::loadFromXML( const ResourcePath& assetsFile )
    {
        using boost::property_tree::ptree;

        const ResourcePath sceneLocation = Scene::GetSceneFullPath( parentScene() );

        const ResourcePath file = sceneLocation / assetsFile;

        if ( !fileExists( file ) )
        {
            return;
        }

        Console::printfn( LOCALE_STR( "XML_LOAD_GEOMETRY" ), file );

        ptree pt = {};
        XML::readXML( file, pt );
        if ( pt.get( "version", g_sceneGraphVersion ) != g_sceneGraphVersion )
        {
            // ToDo: Scene graph version mismatch. Handle condition - Ionut
            NOP();
        }

        const auto readNode = []( const ptree& rootNode, XML::SceneNode& graphOut, auto& readNodeRef ) -> void
        {
            for ( const auto& [name, value] : rootNode.get_child( "<xmlattr>", g_emptyPtree ) )
            {
                if ( name.compare("name") == 0 )
                {
                    graphOut.name = value.data().c_str();
                }
                else if ( name.compare("type") == 0 )
                {
                    graphOut.typeHash = _ID( value.data().c_str() );
                }
                else
                {
                    //ToDo: Error handling -Ionut
                    NOP();
                }
            }

            for ( const auto& [name, ptree] : rootNode.get_child( "" ) )
            {
                if ( name == "node" )
                {
                    graphOut.children.emplace_back();
                    readNodeRef( ptree, graphOut.children.back(), readNodeRef );
                }
            }
        };



        XML::SceneNode rootNode = {};
        const auto& [name, node_pt] = pt.get_child( "entities", g_emptyPtree ).front();
        // This is way faster than pre-declaring a std::function and capturing that or by using 2 separate
        // lambdas and capturing one.
        readNode( node_pt, rootNode, readNode );
        // This may not be needed;
        assert( rootNode.typeHash == _ID( "TRANSFORM" ) );
        Attorney::SceneGraph::addSceneGraphToLoad( &parentScene(), MOV( rootNode ) );
    }

    bool SceneGraph::saveNodeToXML( const SceneGraphNode* node ) const
    {
        node->saveToXML( Scene::GetSceneFullPath( parentScene() ) );
        return true;
    }

    bool SceneGraph::loadNodeFromXML( [[maybe_unused]] const ResourcePath& assetsFile, SceneGraphNode* node ) const
    {
        node->loadFromXML( Scene::GetSceneFullPath( parentScene() ) );
        return true;
    }

};
