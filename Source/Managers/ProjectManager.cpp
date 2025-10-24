

#include "Headers/ProjectManager.h"
#include "Headers/FrameListenerManager.h"
#include "Headers/RenderPassManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Editor/Headers/Editor.h"

#include "GUI/Headers/GUI.h"
#include "GUI/Headers/GUIButton.h"

#include "AI/PathFinding/Headers/DivideRecast.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Scenes/Headers/ScenePool.h"
#include "Scenes/Headers/SceneShaderData.h"
#include "Graphs/Headers/SceneGraph.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Environment/Vegetation/Headers/Vegetation.h"
#include "Environment/Sky/Headers/Sky.h"
#include "Environment/Water/Headers/Water.h"

#include "Dynamics/Entities/Units/Headers/Player.h"
#include "Geometry/Importer/Headers/DVDConverter.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"
#include <filesystem>

namespace Divide
{

    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    bool operator==( const SceneEntry& lhs, const SceneEntry& rhs ) noexcept
    {
        return Util::CompareIgnoreCase(lhs._name.c_str(), rhs._name.c_str());
    }

    bool Project::CreateNewProject( const ProjectID& projectID )
    {
        const ResourcePath sourceProjectPath = Paths::g_projectsLocation / Config::DEFAULT_PROJECT_NAME;
        const ResourcePath targetProjectPath = Paths::g_projectsLocation / projectID._name;
        const FileError ret = copyDirectory( sourceProjectPath, targetProjectPath, true, true );

        return ret == FileError::NONE;

    }

    bool Project::CreateNewScene( const SceneEntry& scene, const ProjectID& projectID )
    {
        const ResourcePath scenePath = Paths::g_projectsLocation / projectID._name / Paths::g_scenesLocation;
        
        FileError ret = copyDirectory( scenePath / Config::DEFAULT_SCENE_NAME, scenePath / scene._name, true, false );
        if ( ret != FileError::NONE )
        {
            return false;
        }

        ret = copyFile( scenePath,
                        (Config::DEFAULT_SCENE_NAME + string( ".xml" )).c_str(),
                        scenePath,
                        (scene._name + ".xml").c_str(),
                        true );

        if ( ret != FileError::NONE )
        {
            ret = removeDirectory( scenePath / scene._name );
            DIVIDE_EXPECTED_CALL( ret == FileError::NONE );

            return false;
        }

        return true;
    }

    Project::Project( ProjectManager& parentMgr, const ProjectID& projectID )
        : _id( projectID )
        , _scenePool( *this )
        , _parentManager( parentMgr )
    {
        const std::filesystem::directory_iterator end;
        for ( std::filesystem::directory_iterator iter{ (Paths::g_projectsLocation / projectID._name / Paths::g_scenesLocation).fileSystemPath() }; iter != end; ++iter )
        {
            if ( std::filesystem::is_directory( *iter )  &&
                 iter->path().filename().string().compare( Config::DELETED_FOLDER_NAME ) != 0 )
            {
                auto name = iter->path().filename().string();
                if ( name.length() > 255 )
                {
                    name = name.substr( 0, 255 );
                }

                _sceneEntries.emplace_back( SceneEntry
                {
                    ._name = name.c_str()
                });
            }
        }
    }

    Project::~Project()
    {
        Console::printfn( LOCALE_STR( "SCENE_MANAGER_REMOVE_SCENES" ) );
        // ScenePool destruction should unload our active scene
    }

    void Project::idle()
    {
    
        if ( getActiveScene()->idle() )
        {
            NOP();
        }
    }

    bool Project::onFrameStart()
    {
        if ( IsSet( _sceneSwitchTarget ) )
        {
            parent().platformContext().gfx().getRenderer().postFX().setFadeOut( UColour3( 0 ), 1000.0, 0.0 );
            if ( !switchSceneInternal() )
            {
                return false;
            }
            getActiveScene()->context().taskPool( TaskPoolType::HIGH_PRIORITY ).waitForAllTasks( true );
            parent().platformContext().gfx().getRenderer().postFX().setFadeIn( 2750.0 );
        }

        return true;
    }

    bool Project::onFrameEnd()
    {
        return true;
    }

    Scene* Project::getActiveScene() const noexcept
    {
        return _scenePool.activeScene();
    }

    bool Project::switchScene( const SwitchSceneTarget& scene )
    {
        const ResourcePath scenePath = Paths::g_projectsLocation / id()._name / Paths::g_scenesLocation / scene._targetScene._name;
        if ( !pathExists( scenePath ) )
        {
            if (!scene._createIfNotExist || !CreateNewScene( scene._targetScene, id() ) )
            {
                return false;
            }
        }

        if ( !fileExists( ResourcePath{ scenePath.string() + ".xml" } ) )
        {
            return false;
        }

        _sceneSwitchTarget = scene;

        if ( !_sceneSwitchTarget._deferToStartOfFrame )
        {
            return switchSceneInternal();
        }

        return true;
    }

    bool Project::switchSceneInternal()
    {
        SwitchSceneTarget target = _sceneSwitchTarget;
        _sceneSwitchTarget = {};
        
        const SceneEntry& scene = target._targetScene;

        STUBBED("ToDo: Threaded scene load is currently disabled -Ionut");

        const bool threaded = false;//target._loadInSeparateThread;
        bool unloadPrevious = target._unloadPreviousScene;

        DIVIDE_ASSERT( !scene._name.empty() );

        Scene* sceneToUnload = _scenePool.activeScene();
        if ( sceneToUnload != nullptr && sceneToUnload->resourceName().compare( scene._name ) == 0 )
        {
            unloadPrevious = false;
        }

        // We use our rendering task pool for scene changes because we might be creating / loading GPU assets (shaders, textures, buffers, etc)
        Start( *CreateTask([this, unloadPrevious, &scene, &sceneToUnload]( const Task& /*parentTask*/ )
            {
                // Load first, unload after to make sure we don't reload common resources
                if ( loadScene( scene ) != nullptr )
                {
                    if ( unloadPrevious && sceneToUnload )
                    {
                        Attorney::SceneProjectManager::onRemoveActive( sceneToUnload );
                        DIVIDE_EXPECTED_CALL( unloadScene( sceneToUnload ) );
                    }
                }
            }),
             parent().platformContext().taskPool( TaskPoolType::HIGH_PRIORITY ),
             threaded ? TaskPriority::DONT_CARE : TaskPriority::REALTIME,
             [this, scene, unloadPrevious, &sceneToUnload]()
             {
                 bool foundInCache = false;
                 Scene* loadedScene = _scenePool.getOrCreateScene( parent().platformContext(), *this, scene, foundInCache );
                 assert( loadedScene != nullptr && foundInCache );
                 
                 if ( loadedScene->getState() == ResourceState::RES_LOADING )
                 {
                     Attorney::SceneProjectManager::postLoadMainThread( loadedScene );
                 }
                 assert( loadedScene->getState() == ResourceState::RES_LOADED );
                 setActiveScene( loadedScene );
                 
                 if ( unloadPrevious && sceneToUnload != nullptr )
                 {
                     _scenePool.deleteScene( sceneToUnload->getGUID() );
                 }
                 
                 parent().platformContext().app().timer().resetFPSCounter();
             }
        );

        return true;
    }

    Scene* Project::loadScene( const SceneEntry& sceneEntry )
    {
        bool foundInCache = false;
        Scene* loadingScene = _scenePool.getOrCreateScene( parent().platformContext(), *this, sceneEntry, foundInCache );

        if ( !loadingScene )
        {
            Console::errorfn( LOCALE_STR( "ERROR_XML_LOAD_INVALID_SCENE" ) );
            return nullptr;
        }

        if ( loadingScene->getState() != ResourceState::RES_LOADED && !Attorney::SceneProjectManager::load( loadingScene ) )
        {
            return nullptr;
        }

        return loadingScene;
    }

    bool Project::unloadScene( Scene* scene )
    {
        assert( scene != nullptr );
        Attorney::ProjectManagerProject::waitForSaveTask( parent() );

        parent().platformContext().gui().onUnloadScene( scene );
        Attorney::SceneProjectManager::onRemoveActive( scene );
        return Attorney::SceneProjectManager::unload( scene );
    }

    void Project::setActiveScene( Scene* const scene )
    {
        assert( scene != nullptr );

        Attorney::ProjectManagerProject::waitForSaveTask( parent() );
        Attorney::SceneProjectManager::onRemoveActive( _scenePool.defaultSceneActive() ? _scenePool.defaultScene()
                                                                                        : getActiveScene() );

        _scenePool.activeScene( *scene );

        Attorney::SceneProjectManager::onSetActive( scene );
        if ( !LoadSave::loadScene( scene ) )
        {
            //corrupt save
        }

        parent().platformContext().gui().onChangeScene( scene );
        parent().platformContext().editor().onChangeScene( scene );
    }

    bool ProjectManager::OnStartup( PlatformContext& context )
    {
        return Attorney::SceneProjectManager::onStartup( context );
    }

    bool ProjectManager::OnShutdown( PlatformContext& context )
    {
        return Attorney::SceneProjectManager::onShutdown( context );
    }

    ProjectManager::ProjectManager( Kernel& parentKernel )
        : FrameListener( "ProjectManager", parentKernel.frameListenerMgr(), 2 )
        , InputAggregatorInterface()
        , KernelComponent( parentKernel )
    {
        processInput(false);
    }

    ProjectManager::~ProjectManager()
    {
        destroy();
    }


    bool ProjectManager::loadComplete() const noexcept
    {
        if ( activeProject() == nullptr )
        {
            return false;
        }

        return Attorney::SceneProjectManager::loadComplete( activeProject()->scenePool().activeScene() );
    }

    void ProjectManager::idle()
    {
        if ( _playerQueueDirty )
        {
            while ( !_playerAddQueue.empty() )
            {
                auto& [targetScene, playerSGN] = _playerAddQueue.front();
                addPlayerInternal( targetScene, playerSGN );
                _playerAddQueue.pop();
            }
            while ( !_playerRemoveQueue.empty() )
            {
                auto& [targetScene, playerSGN] = _playerRemoveQueue.front();
                removePlayerInternal( targetScene, playerSGN );
                _playerRemoveQueue.pop();
            }
            _playerQueueDirty = false;
        }
        else
        {
            Attorney::ProjectManagerProject::idle( activeProject() );
        }
    }

    ProjectIDs& ProjectManager::init()
    {
        if (!_init)
        {
            _init = true;
            DIVIDE_ASSERT( _availableProjects.empty() );
            parent().frameListenerMgr().registerFrameListener( this, 1 );

            _recast = std::make_unique<AI::Navigation::DivideRecast>();

            for ( U8 i = 0u; i < to_base( RenderStage::COUNT ); ++i )
            {
                _sceneGraphCullTimers[i] = &Time::ADD_TIMER( Util::StringFormat( "SceneGraph cull timer: {}", TypeUtil::RenderStageToString( static_cast<RenderStage>(i) ) ).c_str() );
            }

            LightPool::InitStaticData( parent().platformContext() );
        }

        efficient_clear(_availableProjects);
        const std::filesystem::directory_iterator end;
        for ( std::filesystem::directory_iterator iter{ Paths::g_projectsLocation.fileSystemPath() }; iter != end; ++iter )
        {
            if ( !std::filesystem::is_directory( *iter ) ||
                 iter->path().filename().string().compare( Config::DELETED_FOLDER_NAME ) == 0 )
            {
                continue;
            }

            const std::string projectName = iter->path().filename().string();

            _availableProjects.emplace_back( ProjectID
            {
                ._guid = _ID( projectName ),
                ._name = projectName.c_str()
            });
        }

        return _availableProjects;
    }

    void ProjectManager::destroy()
    {
        if ( _init )
        {
            LightPool::DestroyStaticData();
            Console::printfn( LOCALE_STR( "STOP_SCENE_MANAGER" ) );
            // Console::printfn(Locale::Get("SCENE_MANAGER_DELETE"));
            _recast.reset();
            _init = false;
        }
    }

    ErrorCode ProjectManager::loadProject( const ProjectID& targetProject, const bool deferToStartOfFrame )
    {
        _projectSwitchTarget._targetProject = targetProject;

        if (!deferToStartOfFrame)
        {
            return loadProjectInternal();
        }

        return ErrorCode::NO_ERR;
    }


    ErrorCode ProjectManager::loadProjectInternal()
    {
        SwitchProjectTarget target = _projectSwitchTarget;
        _projectSwitchTarget = {};

        if ( _activeProject != nullptr )
        {
            Console::warnfn( LOCALE_STR( "WARN_PROJECT_CHANGE" ), _activeProject->id()._name, target._targetProject._name );
        }

        _activeProject = std::make_unique<Project>( *this, target._targetProject );

        if ( _activeProject == nullptr || _activeProject->getSceneEntries().empty() )
        {
            Console::errorfn( LOCALE_STR( "ERROR_PROJECT_LOAD" ), target._targetProject._name );
            return ErrorCode::MISSING_PROJECT_DATA;
        }

        SwitchSceneTarget sceneTarget
        {
            ._targetScene = _activeProject->getSceneEntries().front(),
            ._unloadPreviousScene = true,
            ._loadInSeparateThread = false,
            ._deferToStartOfFrame = false,
            ._createIfNotExist = false
        };

        if ( !_activeProject->switchScene( sceneTarget ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_SCENE_LOAD" ), _activeProject->getSceneEntries().front()._name.c_str() );
            return ErrorCode::MISSING_SCENE_DATA;
        }

        return ErrorCode::NO_ERR;
    }

    void ProjectManager::waitForSaveTask()
    {
        if ( _saveTask == nullptr )
        {
            return;
        }

        Wait( *_saveTask, parent().platformContext().taskPool( TaskPoolType::LOW_PRIORITY ) );
    }

    void ProjectManager::initPostLoadState() noexcept
    {
        processInput(true);

        if constexpr( Config::Build::IS_EDITOR_BUILD )
        {
            static_assert(Config::Build::ENABLE_EDITOR);
            DisplayWindow& window = platformContext().mainWindow();
            if ( window.type() == WindowType::WINDOW )
            {
                window.maximized( true );
            }

            platformContext().editor().toggle( true );
        }
    }

    void ProjectManager::onResolutionChange( const SizeChangeParams& params )
    {
        if ( _init )
        {
            const F32 aspectRatio = to_F32( params.width ) / params.height;
            const Angle::DEGREES_F vFoV = Angle::to_VerticalFoV( Angle::DEGREES_F(platformContext().config().runtime.horizontalFOV), to_D64( aspectRatio ) );
            const float2 zPlanes( Camera::s_minNearZ, platformContext().config().runtime.cameraViewDistance );

            auto& players = Attorney::SceneProjectManager::getPlayers( activeProject()->getActiveScene() );
            for ( const auto& crtPlayer : players )
            {
                if ( crtPlayer != nullptr )
                {
                    crtPlayer->camera()->setProjection( aspectRatio, vFoV, zPlanes );
                }
            }
        }
    }

    void ProjectManager::addPlayer( Scene* parentScene, SceneGraphNode* playerNode, const bool queue )
    {
        if ( queue )
        {
            _playerAddQueue.push( std::make_pair( parentScene, playerNode ) );
            _playerQueueDirty = true;
        }
        else
        {
            addPlayerInternal( parentScene, playerNode );
        }
    }

    void ProjectManager::addPlayerInternal( Scene* parentScene, SceneGraphNode* playerNode )
    {
        const I64 sgnGUID = playerNode->getGUID();

        auto& players = Attorney::SceneProjectManager::getPlayers( parentScene );

        for ( const auto& crtPlayer : players )
        {
            if ( crtPlayer && crtPlayer->getBoundNode()->getGUID() == sgnGUID )
            {
                return;
            }
        }

        U32 i = 0u;
        for ( ; i < Config::MAX_LOCAL_PLAYER_COUNT; ++i )
        {
            if ( players[i] == nullptr )
            {
                break;
            }
        }

        if ( i < Config::MAX_LOCAL_PLAYER_COUNT )
        {
            players[i] = std::make_shared<Player>( to_U8( i ) );
            players[i]->camera()->fromCamera( *Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT ) );
            players[i]->camera()->setGlobalAxis(true, false, false);

            {
                boost::property_tree::ptree pt;

                const ResourcePath sceneDataFile = Scene::GetSceneRootFolder( parentScene->parent() ) / (parentScene->resourceName() + ".xml");
                XML::readXML( sceneDataFile, pt );
                players[i]->camera()->loadFromXML( pt );
            }

            playerNode->get<UnitComponent>()->setUnit( players[i] );
            Attorney::SceneProjectManager::onPlayerAdd( parentScene, players[i] );
        }
    }

    void ProjectManager::removePlayer( Scene* parentScene, SceneGraphNode* playerNode, const bool queue )
    {
        if ( queue )
        {
            _playerRemoveQueue.push( std::make_pair( parentScene, playerNode ) );
            _playerQueueDirty = true;
        }
        else
        {
            removePlayerInternal( parentScene, playerNode );
        }
    }

    void ProjectManager::removePlayerInternal( Scene* parentScene, SceneGraphNode* playerNode )
    {
        if ( playerNode == nullptr )
        {
            return;
        }

        const I64 targetGUID = playerNode->getGUID();

        auto& players = Attorney::SceneProjectManager::getPlayers( parentScene );
        for ( U32 i = 0; i < Config::MAX_LOCAL_PLAYER_COUNT; ++i )
        {
            if ( players[i] != nullptr && players[i]->getBoundNode()->getGUID() == targetGUID )
            {
                Attorney::SceneProjectManager::onPlayerRemove( parentScene, players[i] );
                players[i] = nullptr;
                break;
            }
        }
    }

    void ProjectManager::getNodesInScreenRect( const Rect<I32>& screenRect, const Camera& camera, vector<SceneGraphNode*>& nodesOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        constexpr SceneNodeType s_ignoredNodes[6]
        {
            SceneNodeType::TYPE_TRANSFORM,
            SceneNodeType::TYPE_WATER,
            SceneNodeType::TYPE_SKY,
            SceneNodeType::TYPE_PARTICLE_EMITTER,
            SceneNodeType::TYPE_INFINITEPLANE,
            SceneNodeType::TYPE_VEGETATION
        };

        static vector<SGNRayResult> rayResults = {};
        static VisibleNodeList<VisibleNode, 1024> inRectList;
        static VisibleNodeList<VisibleNode, 1024> LoSList;

        nodesOut.clear();
        inRectList.reset();
        LoSList.reset();
        rayResults.clear();

        const auto& sceneGraph = activeProject()->getActiveScene()->sceneGraph();
        const float3& eye = camera.snapshot()._eye;
        const float2  zPlanes = camera.snapshot()._zPlanes;

        SGNIntersectionParams intersectionParams = {};
        intersectionParams._includeTransformNodes = false;
        intersectionParams._ignoredTypes = &s_ignoredNodes[0];
        intersectionParams._ignoredTypesCount = std::size(s_ignoredNodes);

        const GFXDevice& gfx = parent().platformContext().gfx();

        const auto CheckPointLoS = [&]( const float3& point, const I64 nodeGUID, const I64 parentNodeGUID ) -> bool
        {
            intersectionParams._ray = { point, point.direction( eye ) };
            intersectionParams._range = { 0.f, zPlanes.y };

            const F32 distanceToPoint = eye.distance( point );

            sceneGraph->intersect( intersectionParams, rayResults );

            for ( const SGNRayResult& result : rayResults )
            {
                if ( result.sgnGUID == nodeGUID ||
                    result.sgnGUID == parentNodeGUID )
                {
                    continue;
                }

                if ( result.inside || result.dist < distanceToPoint )
                {
                    return false;
                }
            }
            return true;
        };

        const auto HasLoSToCamera = [&]( SceneGraphNode* node, const float3& point )
        {
            I64 parentNodeGUID = -1;
            const I64 nodeGUID = node->getGUID();
            if ( Is3DObject( node->getNode().type() ) )
            {
                parentNodeGUID = node->parent()->getGUID();
            }
            return CheckPointLoS( point, nodeGUID, parentNodeGUID );
        };

        const auto IsNodeInRect = [&screenRect, &camera, &gfx]( SceneGraphNode* node )
        {
            assert( node != nullptr );
            const SceneNode& sNode = node->getNode();
            if ( Is3DObject( sNode.type() ))
            {
                auto* sComp = node->get<SelectionComponent>();
                if ( sComp == nullptr &&
                    (sNode.type() == SceneNodeType::TYPE_SUBMESH) )
                {
                    if ( node->parent() != nullptr )
                    {
                        // Already selected. Skip.
                        if ( node->parent()->hasFlag( SceneGraphNode::Flags::SELECTED ) )
                        {
                            return false;
                        }
                        sComp = node->parent()->get<SelectionComponent>();
                    }
                }
                if ( sComp != nullptr && sComp->enabled() )
                {
                    const BoundsComponent* bComp = node->get<BoundsComponent>();
                    if ( bComp != nullptr )
                    {
                        const float3& center = bComp->getBoundingSphere()._sphere.center;
                        const vec2<U16> resolution = gfx.renderingResolution();
                        const Rect<I32> targetViewport( 0, 0, to_I32( resolution.width ), to_I32( resolution.height ) );
                        return screenRect.contains( camera.project( center, targetViewport ) );
                    }
                }
            }

            return false;
        };

        //Step 1: Grab ALL nodes in rect
        for ( size_t i = 0u; i < _recentlyRenderedNodes.size(); ++i )
        {
            const VisibleNode& node = _recentlyRenderedNodes.node( i );
            if ( IsNodeInRect( node._node ) )
            {
                inRectList.append( node );
                if ( inRectList.size() == 1024 )
                {
                    break;
                }
            }
        }

        //Step 2: Check Straight LoS to camera
        for ( size_t i = 0u; i < inRectList.size(); ++i )
        {
            const VisibleNode& node = inRectList.node( i );
            if ( HasLoSToCamera( node._node, node._node->get<BoundsComponent>()->getBoundingSphere()._sphere.center ) )
            {
                LoSList.append( node );
            }
            else
            {
                // This is gonna hurt.The raycast failed, but the node might still be visible
                const OBB& obb = node._node->get<BoundsComponent>()->getOBB();
                for ( U8 p = 0; p < 8; ++p )
                {
                    if ( HasLoSToCamera( node._node, obb.cornerPoint( p ) ) )
                    {
                        LoSList.append( node );
                        break;
                    }
                }
            }
        }

        //Step 3: Create list of visible nodes
        for ( size_t i = 0; i < LoSList.size(); ++i )
        {
            SceneGraphNode* parsedNode = LoSList.node( i )._node;
            if ( parsedNode != nullptr )
            {
                while ( true )
                {
                    const SceneNode& node = parsedNode->getNode();
                    if ( node.type() == SceneNodeType::TYPE_SUBMESH )
                    {
                        parsedNode = parsedNode->parent();
                    }
                    else
                    {
                        break;
                    }
                }

                if ( eastl::find( cbegin( nodesOut ), cend( nodesOut ), parsedNode ) == cend( nodesOut ) )
                {
                    nodesOut.push_back( parsedNode );
                }
            }
        }
    }

    bool ProjectManager::frameStarted( [[maybe_unused]] const FrameEvent& evt )
    {
        if ( _init )
        {
            if ( IsSet(_projectSwitchTarget) && loadProjectInternal() != ErrorCode::NO_ERR )
            {
                return false;
            }

            if ( !Attorney::ProjectManagerProject::onFrameStart( *activeProject() ) )
            {
                return false;
            }

            return Attorney::SceneProjectManager::frameStarted( activeProject()->getActiveScene() );
        }

        return true;
    }

    bool ProjectManager::frameEnded( [[maybe_unused]] const FrameEvent& evt )
    {
        if ( _init )
        {
            if (!Attorney::ProjectManagerProject::onFrameEnd( *activeProject() ))
            {
                return false;
            }

            return Attorney::SceneProjectManager::frameEnded( activeProject()->getActiveScene() );
        }

        return true;
    }

    void ProjectManager::updateSceneState( const U64 deltaGameTimeUS, const U64 deltaAppTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Scene* activeScene = activeProject()->getActiveScene();
        assert( activeScene->getState() == ResourceState::RES_LOADED );
        // Update internal timers
        _elapsedGameTime += deltaGameTimeUS;
        _elapsedGameTimeMS = Time::MicrosecondsToMilliseconds<U32>( _elapsedGameTime );
        _elapsedAppTime += deltaAppTimeUS;
        _elapsedAppTimeMS = Time::MicrosecondsToMilliseconds<U32>( _elapsedAppTime );

        const Scene::DayNightData& dayNightData = activeScene->dayNightData();

        const FColour3 sunColour = dayNightData._sunLight != nullptr
            ? dayNightData._sunLight->getDiffuseColour()
            : DefaultColours::WHITE.rgb;

        const GFXDevice& gfx = parent().platformContext().gfx();
        SceneShaderData* sceneData = gfx.sceneData().get();

        const Angle::DEGREES_F sunAltitudeMax =                  ( activeScene->getCurrentSunDetails().altitudeMax );
        const Angle::DEGREES_F sunAltitude    = Angle::to_DEGREES( activeScene->getCurrentSunDetails().altitude );
        const Angle::DEGREES_F sunAzimuth     = Angle::to_DEGREES( activeScene->getCurrentSunDetails().azimuth );

        sceneData->sunDetails( activeScene->getSunDirection(), sunColour, (sunAltitude / sunAltitudeMax), sunAzimuth);
        sceneData->appData( _elapsedGameTimeMS, _elapsedAppTimeMS, gfx.materialDebugFlag() );

        //_sceneData->skyColour(horizonColour, zenithColour);

        FogDetails fog = activeScene->state()->renderState().fogDetails();
        fog._colourSunScatter.rgb = sunColour;

        if ( !platformContext().config().rendering.enableFog )
        {
            fog._colourAndDensity.a = 0.f;
        }
        sceneData->fogDetails( fog );

        const auto& activeSceneState = activeScene->state();
        sceneData->windDetails( activeSceneState->windDirX(),
                               0.0f,
                               activeSceneState->windDirZ(),
                               activeSceneState->windSpeed() );

        Attorney::GFXDeviceProjectManager::shadowingSettings( _parent.platformContext().gfx(), activeSceneState->lightBleedBias(), activeSceneState->minShadowVariance() );

        activeScene->updateSceneState( deltaGameTimeUS );

        U8 index = 0u;

        const auto& waterBodies = activeSceneState->waterBodies();
        for ( const auto& body : waterBodies )
        {
            sceneData->waterDetails( index++, body );
        }
        _saveTimer += deltaGameTimeUS;

        if ( _saveTimer >= Time::SecondsToMicroseconds<U64>( Config::Build::IS_DEBUG_BUILD ? 5 : 10 ) )
        {
            if ( !saveActiveScene( true, true ) )
            {
                NOP();
            }
            _saveTimer = 0ULL;
        }
        if ( dayNightData._skyInstance != nullptr )
        {
            _parent.platformContext().gfx().getRenderer().postFX().isDayTime( dayNightData._skyInstance->isDay() );
        }
    }

    void ProjectManager::drawCustomUI( const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        //Set a 2D camera for rendering
        GFX::EnqueueCommand<GFX::SetCameraCommand>( bufferInOut)->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
        GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport = targetViewport;

        Attorney::SceneProjectManager::drawCustomUI( activeProject()->getActiveScene(), targetViewport, bufferInOut, memCmdInOut );
    }

    void ProjectManager::postRender( GFX::CommandBuffer& bufferInOut, [[maybe_unused]] GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Scene* activeScene = activeProject()->getActiveScene();
        if ( activeScene->state()->screenshotRequestQueued() )
        {
            platformContext().gfx().screenshot( Util::StringFormat("Frame_{}", GFXDevice::FrameCount()), bufferInOut);
            activeScene->state()->screenshotRequestQueued(false);
        }
    }

    void ProjectManager::debugDraw( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Scene* activeScene = activeProject()->getActiveScene();

        Attorney::SceneProjectManager::debugDraw( activeScene, bufferInOut, memCmdInOut );
        // Draw bounding boxes, skeletons, axis gizmo, etc.
        platformContext().gfx().debugDraw( activeScene->state()->renderState(), bufferInOut, memCmdInOut );
    }

    Camera* ProjectManager::playerCamera( const PlayerIndex idx, const bool skipOverride ) const noexcept
    {
        if ( activePlayerCount() <= idx )
        {
            return nullptr;
        }

        Scene* activeScene = activeProject()->getActiveScene();
        if ( !skipOverride )
        {
            Camera* overrideCamera = activeScene->state()->playerState( idx ).overrideCamera();
            if ( overrideCamera != nullptr )
            {
                return overrideCamera;
            }
        }

        return Attorney::SceneProjectManager::getPlayers(activeScene)[idx]->camera();
    }

    Camera* ProjectManager::playerCamera( const bool skipOverride ) const noexcept
    {
        return playerCamera( _currentPlayerPass, skipOverride );
    }

    void ProjectManager::currentPlayerPass( const PlayerIndex idx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        _currentPlayerPass = idx;
        Attorney::SceneProjectManager::currentPlayerPass( activeProject()->getActiveScene(), _currentPlayerPass );
        playerCamera()->updateLookAt();
    }

    void ProjectManager::editorPreviewNode( const I64 editorPreviewNode ) noexcept
    {
        activeProject()->getActiveScene()->state()->renderState().singleNodeRenderGUID( editorPreviewNode );
    }

    BoundingSphere ProjectManager::moveCameraToNode( Camera* camera, const SceneGraphNode* targetNode ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        BoundingSphere bSphere;
        float3 targetPos = WORLD_Z_NEG_AXIS;
        float3 eyePos = VECTOR3_ZERO;

        if ( camera == nullptr )
        {
            camera = playerCamera();
        }

        /// Root node just means a teleport to (0,0,0)
        if ( targetNode->parent() != nullptr )
        {
            bSphere = SceneGraph::GetBounds(targetNode);
            targetPos = bSphere._sphere.center;
            eyePos = targetPos - (bSphere._sphere.radius * 1.5f * camera->viewMatrix().getForwardDirection());
        }
        else
        {
            bSphere.setCenter(VECTOR3_ZERO);
            bSphere.setRadius(1.f);
        }

        camera->lookAt( eyePos, targetPos );
        return bSphere;
    }

    bool ProjectManager::saveNode( const SceneGraphNode* targetNode ) const
    {
        return LoadSave::saveNodeToXML( activeProject()->getActiveScene(), targetNode );
    }

    bool ProjectManager::loadNode( SceneGraphNode* targetNode ) const
    {
        return LoadSave::loadNodeFromXML( activeProject()->getActiveScene(), targetNode );
    }

    
    void ProjectManager::getSortedReflectiveNodes( const Camera* camera, const RenderStage stage, const bool inView, VisibleNodeList<>& nodesOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        static vector<SceneGraphNode*> allNodes = {};
        activeProject()->getActiveScene()->sceneGraph()->getNodesByType( { SceneNodeType::TYPE_WATER, SceneNodeType::TYPE_SUBMESH, SceneNodeType::TYPE_SPHERE_3D, SceneNodeType::TYPE_BOX_3D, SceneNodeType::TYPE_QUAD_3D }, allNodes );

        erase_if( allNodes,
                 []( SceneGraphNode* node ) noexcept ->  bool
        {
            Handle<Material> mat = node->get<RenderingComponent>()->getMaterialInstance();
            return node->getNode().type() != SceneNodeType::TYPE_WATER && (mat == INVALID_HANDLE<Material> || !Get(mat)->isReflective());
        } );

        if ( inView )
        {
            NodeCullParams cullParams = {};
            cullParams._lodThresholds = activeProject()->getActiveScene()->state()->renderState().lodThresholds();
            cullParams._stage = stage;
            cullParams._cameraEyePos = camera->snapshot()._eye;
            cullParams._frustum = &camera->getFrustum();
            cullParams._cullMaxDistance = camera->snapshot()._zPlanes.max;

            RenderPassCuller::FrustumCull( parent().platformContext(), cullParams, to_base( CullOptions::DEFAULT_CULL_OPTIONS ), allNodes, nodesOut );
        }
        else
        {
            RenderPassCuller::ToVisibleNodes( camera, allNodes, nodesOut );
        }
    }

    void ProjectManager::getSortedRefractiveNodes( const Camera* camera, const RenderStage stage, const bool inView, VisibleNodeList<>& nodesOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        static vector<SceneGraphNode*> allNodes = {};
        activeProject()->getActiveScene()->sceneGraph()->getNodesByType( { SceneNodeType::TYPE_WATER, SceneNodeType::TYPE_SUBMESH, SceneNodeType::TYPE_SPHERE_3D, SceneNodeType::TYPE_BOX_3D, SceneNodeType::TYPE_QUAD_3D }, allNodes );

        erase_if( allNodes,
                 []( SceneGraphNode* node ) noexcept ->  bool
        {
            Handle<Material> mat = node->get<RenderingComponent>()->getMaterialInstance();
            return node->getNode().type() != SceneNodeType::TYPE_WATER && (mat == INVALID_HANDLE<Material> || !Get(mat)->isRefractive());
        } );
        if ( inView )
        {
            NodeCullParams cullParams = {};
            cullParams._lodThresholds = activeProject()->getActiveScene()->state()->renderState().lodThresholds();
            cullParams._stage = stage;
            cullParams._cameraEyePos = camera->snapshot()._eye;
            cullParams._frustum = &camera->getFrustum();
            cullParams._cullMaxDistance = camera->snapshot()._zPlanes.max;

            RenderPassCuller::FrustumCull( parent().platformContext(), cullParams, to_base( CullOptions::DEFAULT_CULL_OPTIONS ), allNodes, nodesOut );
        }
        else
        {
            RenderPassCuller::ToVisibleNodes( camera, allNodes, nodesOut );
        }
    }

    const VisibleNodeList<>& ProjectManager::getRenderedNodeList() const noexcept
    {
        return _recentlyRenderedNodes;
    }

    void ProjectManager::initDefaultCullValues( const RenderStage stage, NodeCullParams& cullParamsInOut ) noexcept
    {
        Scene* activeScene = activeProject()->getActiveScene();

        cullParamsInOut._stage = stage;
        cullParamsInOut._lodThresholds = activeScene->state()->renderState().lodThresholds( stage );
        if ( stage != RenderStage::SHADOW )
        {
            cullParamsInOut._cullMaxDistance = activeScene->state()->renderState().generalVisibility();
        }
        else
        {
            cullParamsInOut._cullMaxDistance = F32_MAX;
        }
    }

    void ProjectManager::cullSceneGraph( const NodeCullParams& params, const U16 cullFlags, VisibleNodeList<>& nodesOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Time::ScopedTimer timer( *_sceneGraphCullTimers[to_U32( params._stage )] );

        Scene* activeScene = activeProject()->getActiveScene();
        RenderPassCuller::FrustumCull( params, cullFlags, *activeScene->sceneGraph(), *activeScene->state(), _parent.platformContext(), nodesOut );

        if ( params._stage == RenderStage::DISPLAY )
        {
            _recentlyRenderedNodes = nodesOut;
        }
    }

    void ProjectManager::findNode( const float3& cameraEye, const I64 nodeGUID, VisibleNodeList<>& nodesOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( nodeGUID != -1 )
        {
            SceneGraphNode* sgn = activeProject()->getActiveScene()->sceneGraph()->findNode( nodeGUID );
            if ( sgn != nullptr )
            {
                const auto appendNode = [&nodesOut, &cameraEye]( SceneGraphNode* sgn )
                {
                    const BoundsComponent* bComp = sgn->get<BoundsComponent>();
                    VisibleNode temp{};
                    temp._node = sgn;
                    temp._distanceToCameraSq = bComp == nullptr ? 0.f : bComp->getBoundingSphere().getDistanceFromPoint( cameraEye );
                    nodesOut.append( temp );
                };
                appendNode( sgn );

                const BoundsComponent* bComp = sgn->get<BoundsComponent>();
                VisibleNode temp{};
                temp._node = sgn;
                temp._distanceToCameraSq = bComp == nullptr ? 0.f : bComp->getBoundingSphere().getDistanceFromPoint( cameraEye );
                nodesOut.append( temp );

                const auto& children = sgn->getChildren();
                SharedLock<SharedMutex> r_lock( children._lock );
                const U32 childCount = children._count;
                for ( U32 i = 0u; i < childCount; ++i )
                {
                    appendNode( children._data[i] );
                }
            }
        }
    }

    void ProjectManager::prepareLightData( const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( stage != RenderStage::SHADOW )
        {
            LightPool* pool = activeProject()->getActiveScene()->lightPool().get();
            pool->sortLightData( stage, cameraSnapshot );
            pool->uploadLightData( stage, cameraSnapshot, memCmdInOut );
        }
    }

    void ProjectManager::onChangeFocus( const bool hasFocus )
    {
        if ( !_init )
        {
            return;
        }

        activeProject()->getActiveScene()->onChangeFocus( hasFocus );
    }

    bool ProjectManager::resetSelection( const PlayerIndex idx, const bool resetIfLocked )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( Attorney::SceneProjectManager::resetSelection( activeProject()->getActiveScene(), idx, resetIfLocked ) )
        {
            for ( auto& cbk : _selectionChangeCallbacks )
            {
                cbk.second( idx, {} );
            }
            return true;
        }

        return false;
    }

    void ProjectManager::setSelected( const PlayerIndex idx, const vector<SceneGraphNode*>& SGNs, const bool recursive )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Attorney::SceneProjectManager::setSelected( activeProject()->getActiveScene(), idx, SGNs, recursive );
        for ( auto& cbk : _selectionChangeCallbacks )
        {
            cbk.second( idx, SGNs );
        }
    }

    void ProjectManager::onNodeDestroy( Scene* parentScene, [[maybe_unused]] SceneGraphNode* node )
    {
        auto& players = Attorney::SceneProjectManager::getPlayers( parentScene );
        for ( U32 i = 0; i < Config::MAX_LOCAL_PLAYER_COUNT; ++i )
        {
            if ( players[i] != nullptr && !resetSelection( players[i]->index(), true ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    void ProjectManager::mouseMovedExternally( const Input::MouseMoveEvent& arg )
    {
        Attorney::SceneProjectManager::clearHoverTarget( activeProject()->getActiveScene(), arg );
    }

    SceneEnvironmentProbePool* ProjectManager::getEnvProbes() const noexcept
    {
        return Attorney::SceneProjectManager::getEnvProbes( activeProject()->getActiveScene() );
    }

    U8 ProjectManager::activePlayerCount() const noexcept
    {
        return activeProject()->getActiveScene()->playerCount();
    }

    std::pair<Handle<Texture>, SamplerDescriptor> ProjectManager::getSkyTexture() const
    {
        const auto& skies = activeProject()->getActiveScene()->sceneGraph()->getNodesByType( SceneNodeType::TYPE_SKY );
        if ( !skies.empty() )
        {
            const Sky& sky = skies.front()->getNode<Sky>();
            return std::make_pair( sky.activeSkyBox(), sky.skyboxSampler() );
        }

        return { INVALID_HANDLE<Texture>, {} };
    }

    ///--------------------------Input Management-------------------------------------///

    bool ProjectManager::onKeyInternal( Input::KeyEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onKey(argInOut);
    }

    bool ProjectManager::onMouseMovedInternal( Input::MouseMoveEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onMouseMoved(argInOut);
    }

    bool ProjectManager::onMouseButtonInternal( Input::MouseButtonEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onMouseButton(argInOut);
    }

    bool ProjectManager::onJoystickButtonInternal( Input::JoystickEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onJoystickButton(argInOut);
    }

    bool ProjectManager::onJoystickAxisMovedInternal( Input::JoystickEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onJoystickAxisMoved(argInOut);
    }

    bool ProjectManager::onJoystickPovMovedInternal( Input::JoystickEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onJoystickPovMoved(argInOut);
    }

    bool ProjectManager::onJoystickBallMovedInternal( Input::JoystickEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onJoystickBallMoved(argInOut);
    }

    bool ProjectManager::onJoystickRemapInternal( Input::JoystickEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onJoystickRemap(argInOut);
    }

    bool ProjectManager::onTextInputInternal( Input::TextInputEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onTextInput(argInOut);
    }

    bool ProjectManager::onTextEditInternal( Input::TextEditEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onTextEdit(argInOut);
    }

    bool ProjectManager::onDeviceAddOrRemoveInternal( Input::InputEvent& argInOut)
    {
        return activeProject()->getActiveScene()->input()->onDeviceAddOrRemove(argInOut);
    }

    PlatformContext& ProjectManager::platformContext() noexcept
    {
        return parent().platformContext();
    }

    const PlatformContext& ProjectManager::platformContext() const noexcept
    {
        return parent().platformContext();
    }

    namespace
    {
        constexpr const char* g_saveFile = "current_save.sav";
        constexpr const char* g_bakSaveFile = "save.bak";
    }

    bool LoadSave::loadScene( Scene* activeScene )
    {
        if ( activeScene->state()->saveLoadDisabled() )
        {
            return true;
        }

        const Str<256>& sceneName = activeScene->resourceName();

        const ResourcePath path = Paths::g_saveLocation / sceneName;

        bool isLoadFromBackup = false;
        // If file is missing, restore from bak
        if ( !fileExists( path / g_saveFile ) )
        {
            isLoadFromBackup = true;

            // Save file might be deleted if it was corrupted
            if ( copyFile( path, g_bakSaveFile, path, g_saveFile, false ) != FileError::NONE )
            {
                NOP();
            }
        }

        ByteBuffer save;
        if ( save.loadFromFile( path, g_saveFile ) )
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            save >> tempVer;
            if ( tempVer == BYTE_BUFFER_VERSION )
            {
                if ( !Attorney::SceneLoadSave::load( activeScene, save ) )
                {
                    //Remove the save and try the backup
                    if ( deleteFile( path, g_saveFile ) != FileError::NONE )
                    {
                        NOP();
                    }
                    if ( !isLoadFromBackup )
                    {
                        return loadScene( activeScene );
                    }
                }
            }
        }
        return false;
    }


    bool LoadSave::saveNodeToXML( Scene* activeScene, const SceneGraphNode* node )
    {
        return Attorney::SceneLoadSave::saveNodeToXML( activeScene, node );
    }

    bool LoadSave::loadNodeFromXML( Scene* activeScene, SceneGraphNode* node )
    {
        return Attorney::SceneLoadSave::loadNodeFromXML( activeScene, node );
    }

    bool LoadSave::saveScene( Scene* activeScene, const bool toCache, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback )
    {
        if ( !toCache )
        {
            return Attorney::SceneLoadSave::saveXML( activeScene, msgCallback, finishCallback );
        }

        bool ret = false;
        if ( activeScene->state()->saveLoadDisabled() )
        {
            ret = true;
        }
        else
        {
            const Str<256>& sceneName = activeScene->resourceName();
            const ResourcePath path = Paths::g_saveLocation / sceneName;

            if ( copyFile( path, g_saveFile, path, g_bakSaveFile, true ) != FileError::NONE )
            {
                return false;
            }

            ByteBuffer save;
            save << BYTE_BUFFER_VERSION;
            if ( Attorney::SceneLoadSave::save( activeScene, save ) )
            {
                ret = save.dumpToFile( path, g_saveFile );
                assert( ret );
            }
        }
        if ( finishCallback )
        {
            finishCallback( ret );
        }
        return ret;
    }

    bool ProjectManager::saveActiveScene( bool toCache, const bool deferred, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Scene* activeScene = activeProject()->getActiveScene();

        // Ignore any auto-save (or manual saves) on the default scene
        if ( activeScene->getGUID() == Scene::DEFAULT_SCENE_GUID )
        {
            return true;
        }

        TaskPool& pool = parent().platformContext().taskPool( TaskPoolType::LOW_PRIORITY );
        if ( _saveTask != nullptr )
        {
            if ( !Finished( *_saveTask ) )
            {
                if ( toCache )
                {
                    return false;
                }
                DIVIDE_UNEXPECTED_CALL();
            }
            Wait( *_saveTask, pool );
        }

        _saveTask = CreateTask( nullptr,
                               [activeScene, msgCallback, finishCallback, toCache]( const Task& /*parentTask*/ )
        {
            LoadSave::saveScene( activeScene, toCache, msgCallback, finishCallback );
        });
        Start( *_saveTask, pool, deferred ? TaskPriority::DONT_CARE_NO_IDLE : TaskPriority::REALTIME );

        return true;
    }

    bool ProjectManager::networkUpdate( [[maybe_unused]] const U64 frameCount )
    {
        return true;
    }

} //namespace Divide
