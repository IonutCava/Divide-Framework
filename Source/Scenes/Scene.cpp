

#include "Headers/Scene.h"
#include "Headers/SceneEnvironmentProbePool.h"

#include "Graphs/Headers/SceneGraph.h"
#include "Core/Debugging/Headers/DebugInterface.h"
#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Kernel.h"
#include "Editor/Headers/Editor.h"

#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Utility/Headers/XMLParser.h"

#include "Environment/Sky/Headers/Sky.h"
#include "Environment/Terrain/Headers/InfinitePlane.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Terrain/Headers/TerrainDescriptor.h"
#include "Environment/Water/Headers/Water.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Shapes/Predefined/Headers/Box3D.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Geometry/Importer/Headers/DVDConverter.h"

#include "GUI/Headers/GUI.h"
#include "GUI/Headers/GUIConsole.h"
#include "GUI/Headers/SceneGUIElements.h"

#include "AI/Headers/AIManager.h"

#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/NavigationComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"
#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"

#include "Dynamics/Entities/Triggers/Headers/Trigger.h"
#include "Dynamics/Entities/Units/Headers/Player.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"

#include "ECS/Components/Headers/UnitComponent.h"
#include "Physics/Headers/PXDevice.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide
{

    namespace
    {
        constexpr U16 BYTE_BUFFER_VERSION = 1u;
        constexpr const char* const g_defaultPlayerName = "Player_%d";
    }

    I64 Scene::DEFAULT_SCENE_GUID = 0;
    Mutex Scene::s_perFrameArenaMutex{};
    MyArena<TO_MEGABYTES( 64 )> Scene::s_perFrameArena{};

    Scene::Scene( PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str<256>& name )
        : Resource( ResourceType::DEFAULT, name )
        , PlatformContextComponent( context )
        , _resourceCache( cache )
        , _parent( parent )
    {
        _loadingTasks.store( 0 );
        _flashLight.fill( nullptr );
        _currentHoverTarget.fill( -1 );
        _cameraUpdateListeners.fill( 0u );

        _state = eastl::make_unique<SceneState>( *this );
        _input = eastl::make_unique<SceneInput>( *this );
        _sceneGraph = eastl::make_unique<SceneGraph>( *this );
        _aiManager = eastl::make_unique<AI::AIManager>( *this, _context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
        _lightPool = eastl::make_unique<LightPool>( *this, _context );
        _envProbePool = eastl::make_unique<SceneEnvironmentProbePool>( *this );
        _GUI = eastl::make_unique<SceneGUIElements>( *this, _context.gui() );

        _linesPrimitive = _context.gfx().newIMP( "Generic Line Primitive" );

        PipelineDescriptor pipeDesc;
        pipeDesc._stateBlock._depthTestEnabled = false;
        pipeDesc._shaderProgramHandle = _context.gfx().imShaders()->imShaderNoTexture()->handle();
        _linesPrimitive->setPipelineDescriptor( pipeDesc );
    }

    Scene::~Scene()
    {
        _context.gfx().destroyIMP( _linesPrimitive );
    }

    bool Scene::OnStartup( PlatformContext& context )
    {
        Sky::OnStartup( context );
        DVDConverter::OnStartup( context );
        return true;
    }

    bool Scene::OnShutdown( [[maybe_unused]] PlatformContext& context )
    {
        DVDConverter::OnShutdown();
        return true;
    }

    bool Scene::frameStarted()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        LockGuard<Mutex> lk( s_perFrameArenaMutex );
        s_perFrameArena.clear();

        return true;
    }

    bool Scene::frameEnded()
    {
        return true;
    }

    bool Scene::idle()
    {  
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );
        if ( !_tasks.empty() )
        {
            // Check again to avoid race conditions
            {
                SharedLock<SharedMutex> r_lock( _tasksMutex );
                if ( _tasks.empty() )
                {
                    return true;
                }
            }

            LockGuard<SharedMutex> r_lock( _tasksMutex );
            dvd_erase_if( _tasks, []( Task* handle ) -> bool
            {
                return handle != nullptr && Finished( *handle );
            } );
        }

        return true;
    }

    void Scene::addMusic( const MusicType type, const Str<256>& name, const ResourcePath& srcFile ) const
    {
        const auto [musicFile, musicFilePath] = splitPathToNameAndLocation( srcFile );

        ResourceDescriptor music( name );
        music.assetName( musicFile );
        music.assetLocation( musicFilePath );
        music.flag( true );

        insert( state()->music( type ), _ID( name.c_str() ), CreateResource<AudioDescriptor>( resourceCache(), music ) );
    }

    bool Scene::saveNodeToXML( const SceneGraphNode* node ) const
    {
        return sceneGraph()->saveNodeToXML( node );
    }

    bool Scene::loadNodeFromXML( SceneGraphNode* node ) const
    {
        const char* assetsFile = "assets.xml";
        return sceneGraph()->loadNodeFromXML( assetsFile, node );
    }

    bool Scene::saveXML( const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride ) const
    {
        using boost::property_tree::ptree;
        const char* assetsFile = "assets.xml";
        const Str<256> saveSceneName = Str<256>( strlen( sceneNameOverride ) > 0 ? sceneNameOverride : resourceName().c_str() );

        Console::printfn( LOCALE_STR( "XML_SAVE_SCENE_START" ), saveSceneName.c_str() );

        const ResourcePath sceneLocation( Paths::g_scenesLocation + saveSceneName );
        const ResourcePath sceneDataFile( sceneLocation + ".xml" );

        if ( msgCallback )
        {
            msgCallback( "Validating directory structure ..." );
        }

        if ( !createDirectory( sceneLocation + "/collisionMeshes/" ) )
        {
            NOP();
        }
        if ( !createDirectory( sceneLocation + "/navMeshes/" ) )
        {
            NOP();
        }
        if ( !createDirectory( sceneLocation + "/nodes/" ) )
        {
            NOP();
        }

        // A scene does not necessarily need external data files. Data can be added in code for simple scenes
        {
            if ( msgCallback )
            {
                msgCallback( "Saving scene settings ..." );
            }

            ptree pt;
            pt.put( "assets", assetsFile );
            pt.put( "musicPlaylist", "musicPlaylist.xml" );

            pt.put( "vegetation.grassVisibility", state()->renderState().grassVisibility() );
            pt.put( "vegetation.treeVisibility", state()->renderState().treeVisibility() );

            pt.put( "wind.windDirX", state()->windDirX() );
            pt.put( "wind.windDirZ", state()->windDirZ() );
            pt.put( "wind.windSpeed", state()->windSpeed() );

            pt.put( "options.visibility", state()->renderState().generalVisibility() );

            const U8 activePlayerCount = _parent.getActivePlayerCount();
            for ( U8 i = 0u; i < activePlayerCount; ++i )
            {
                playerCamera( i, true )->saveToXML( pt );
            }

            pt.put( "fog.fogDensity", state()->renderState().fogDetails()._colourAndDensity.a );
            pt.put( "fog.fogScatter", state()->renderState().fogDetails()._colourSunScatter.a );
            pt.put( "fog.fogColour.<xmlattr>.r", state()->renderState().fogDetails()._colourAndDensity.r );
            pt.put( "fog.fogColour.<xmlattr>.g", state()->renderState().fogDetails()._colourAndDensity.g );
            pt.put( "fog.fogColour.<xmlattr>.b", state()->renderState().fogDetails()._colourAndDensity.b );

            pt.put( "lod.lodThresholds.<xmlattr>.x", state()->renderState().lodThresholds().x );
            pt.put( "lod.lodThresholds.<xmlattr>.y", state()->renderState().lodThresholds().y );
            pt.put( "lod.lodThresholds.<xmlattr>.z", state()->renderState().lodThresholds().z );
            pt.put( "lod.lodThresholds.<xmlattr>.w", state()->renderState().lodThresholds().w );

            pt.put( "shadowing.<xmlattr>.lightBleedBias", state()->lightBleedBias() );
            pt.put( "shadowing.<xmlattr>.minShadowVariance", state()->minShadowVariance() );

            pt.put( "dayNight.<xmlattr>.enabled", dayNightCycleEnabled() );
            pt.put( "dayNight.timeOfDay.<xmlattr>.hour", _dayNightData._time._hour );
            pt.put( "dayNight.timeOfDay.<xmlattr>.minute", _dayNightData._time._minutes );
            pt.put( "dayNight.location.<xmlattr>.latitude", _dayNightData._location._latitude );
            pt.put( "dayNight.location.<xmlattr>.longitude", _dayNightData._location._longitude );
            pt.put( "dayNight.timeOfDay.<xmlattr>.timeFactor", _dayNightData._speedFactor );

            const FileError backupReturnCode = copyFile( Paths::g_scenesLocation.c_str(), (saveSceneName + ".xml").c_str(), Paths::g_scenesLocation.c_str(), (saveSceneName + ".xml.bak").c_str(), true );
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
                XML::writeXML( sceneDataFile.c_str(), pt );
            }
        }

        if ( msgCallback )
        {
            msgCallback( "Saving scene graph data ..." );
        }
        sceneGraph()->saveToXML( assetsFile, msgCallback, sceneNameOverride );

        //save music
        {
            if ( msgCallback )
            {
                msgCallback( "Saving music data ..." );
            }

            ptree pt = {}; //ToDo: Save music data :)

            if ( copyFile( (sceneLocation + "/").c_str(), "musicPlaylist.xml", (sceneLocation + "/").c_str(), "musicPlaylist.xml.bak", true ) == FileError::NONE )
            {
                XML::writeXML( (sceneLocation + "/" + "musicPlaylist.xml.dev").c_str(), pt );
            }
        }

        Console::printfn( LOCALE_STR( "XML_SAVE_SCENE_END" ), saveSceneName.c_str() );

        if ( finishCallback )
        {
            finishCallback( true );
        }

        return true;
    }

    bool Scene::loadXML()
    {
        const Configuration& config = _context.config();

        Console::printfn( LOCALE_STR( "XML_LOAD_SCENE" ), resourceName().c_str() );
        const ResourcePath sceneLocation( Paths::g_scenesLocation + "/" + resourceName().c_str() );
        const ResourcePath sceneDataFile( sceneLocation + ".xml" );

        // A scene does not necessarily need external data files
        // Data can be added in code for simple scenes
        if ( !fileExists( sceneDataFile.c_str() ) )
        {
            sceneGraph()->loadFromXML( "assets.xml" );
            loadMusicPlaylist( sceneLocation.c_str(), "musicPlaylist.xml", this, config );
            return true;
        }

        boost::property_tree::ptree pt;
        XML::readXML( sceneDataFile.c_str(), pt );

        state()->renderState().grassVisibility( pt.get( "vegetation.grassVisibility", state()->renderState().grassVisibility() ) );
        state()->renderState().treeVisibility( pt.get( "vegetation.treeVisibility", state()->renderState().treeVisibility() ) );
        state()->renderState().generalVisibility( pt.get( "options.visibility", state()->renderState().generalVisibility() ) );

        state()->windDirX( pt.get( "wind.windDirX", state()->windDirX() ) );
        state()->windDirZ( pt.get( "wind.windDirZ", state()->windDirZ() ) );
        state()->windSpeed( pt.get( "wind.windSpeed", state()->windSpeed() ) );

        state()->lightBleedBias( pt.get( "shadowing.<xmlattr>.lightBleedBias", state()->lightBleedBias() ) );
        state()->minShadowVariance( pt.get( "shadowing.<xmlattr>.minShadowVariance", state()->minShadowVariance() ) );

        dayNightCycleEnabled( pt.get( "dayNight.<xmlattr>.enabled", false ) );
        _dayNightData._time._hour = pt.get<U8>( "dayNight.timeOfDay.<xmlattr>.hour", _dayNightData._time._hour );
        _dayNightData._time._minutes = pt.get<U8>( "dayNight.timeOfDay.<xmlattr>.minute", _dayNightData._time._minutes );
        _dayNightData._location._latitude = pt.get<F32>( "dayNight.location.<xmlattr>.latitude", _dayNightData._location._latitude );
        _dayNightData._location._longitude = pt.get<F32>( "dayNight.location.<xmlattr>.longitude", _dayNightData._location._longitude );
        _dayNightData._speedFactor = pt.get( "dayNight.timeOfDay.<xmlattr>.timeFactor", _dayNightData._speedFactor );
        _dayNightData._resetTime = true;

        FogDetails details = {};
        details._colourAndDensity.set( config.rendering.fogColour, config.rendering.fogDensity );
        details._colourSunScatter.a = config.rendering.fogScatter;

        if ( pt.get_child_optional( "fog" ) )
        {
            details._colourAndDensity = {
                pt.get<F32>( "fog.fogColour.<xmlattr>.r", details._colourAndDensity.r ),
                pt.get<F32>( "fog.fogColour.<xmlattr>.g", details._colourAndDensity.g ),
                pt.get<F32>( "fog.fogColour.<xmlattr>.b", details._colourAndDensity.b ),
                pt.get( "fog.fogDensity", details._colourAndDensity.a )
            };
            details._colourSunScatter.a = pt.get( "fog.fogScatter", details._colourSunScatter.a );
        }

        state()->renderState().fogDetails( details );

        vec4<U16> lodThresholds( config.rendering.lodThresholds );

        if ( pt.get_child_optional( "lod" ) )
        {
            lodThresholds.set( pt.get<U16>( "lod.lodThresholds.<xmlattr>.x", lodThresholds.x ),
                              pt.get<U16>( "lod.lodThresholds.<xmlattr>.y", lodThresholds.y ),
                              pt.get<U16>( "lod.lodThresholds.<xmlattr>.z", lodThresholds.z ),
                              pt.get<U16>( "lod.lodThresholds.<xmlattr>.w", lodThresholds.w ) );
        }

        state()->renderState().lodThresholds().set( lodThresholds );
        sceneGraph()->loadFromXML( pt.get( "assets", "assets.xml" ).c_str() );
        loadMusicPlaylist( sceneLocation.c_str(), pt.get( "musicPlaylist", "" ).c_str(), this, config );

        return true;
    }

    SceneNode_ptr Scene::createNode( const SceneNodeType type, const ResourceDescriptor& descriptor ) const
    {
        switch ( type )
        {
            case SceneNodeType::TYPE_WATER:            return CreateResource<WaterPlane>( resourceCache(), descriptor );
            case SceneNodeType::TYPE_TRIGGER:          return CreateResource<Trigger>( resourceCache(), descriptor );
            case SceneNodeType::TYPE_PARTICLE_EMITTER: return CreateResource<ParticleEmitter>( resourceCache(), descriptor );
            case SceneNodeType::TYPE_INFINITEPLANE:    return CreateResource<InfinitePlane>( resourceCache(), descriptor );
            default: break;
        }

        // Warning?
        return nullptr;
    }

    void Scene::loadAsset( const Task* parentTask, const XML::SceneNode& sceneNode, SceneGraphNode* parent )
    {
        assert( parent != nullptr );

        const ResourcePath sceneLocation( Paths::g_scenesLocation + "/" + resourceName().c_str() );
        ResourcePath savePath{ sceneLocation.c_str() };
        savePath.append( "/nodes/" );

        ResourcePath targetFile{ parent->name().c_str() };
        targetFile.append( "_" );
        targetFile.append( sceneNode.name.c_str() );


        const ResourcePath nodePath = savePath + Util::MakeXMLSafe( targetFile ) + ".xml";

        SceneGraphNode* crtNode = parent;
        if ( fileExists( nodePath ) )
        {

            U32 normalMask = to_base( ComponentType::TRANSFORM ) |
                to_base( ComponentType::BOUNDS ) |
                to_base( ComponentType::NETWORKING );


            boost::property_tree::ptree nodeTree = {};
            XML::readXML( nodePath.str(), nodeTree );

            const auto loadModelComplete = [this, &nodeTree]( CachedResource* res )
            {
                static_cast<SceneNode*>(res)->loadFromXML( nodeTree );
                _loadingTasks.fetch_sub( 1 );
            };

            const auto IsPrimitive = []( const U64 nameHash )
            {
                constexpr std::array<U64, 3> primitiveNames = {
                    _ID( "BOX_3D" ),
                    _ID( "QUAD_3D" ),
                    _ID( "SPHERE_3D" )
                };

                for ( const U64 it : primitiveNames )
                {
                    if ( nameHash == it )
                    {
                        return true;
                    }
                }

                return false;
            };

            const ResourcePath modelName{ nodeTree.get( "model", "" ) };

            SceneNode_ptr ret = nullptr;
            bool skipAdd = true;
            bool nodeStatic = true;

            if ( IsPrimitive( sceneNode.typeHash ) )
            {// Primitive types (only top level)
                normalMask |= to_base( ComponentType::RENDERING ) |
                    to_base( ComponentType::RIGID_BODY );

                if ( !modelName.empty() )
                {
                    _loadingTasks.fetch_add( 1 );

                    ResourceDescriptor item( sceneNode.name.c_str() );
                    item.assetName( modelName );

                    if ( Util::CompareIgnoreCase( modelName, "BOX_3D" ) )
                    {
                        ret = CreateResource<Box3D>( resourceCache(), item );
                    }
                    else if ( Util::CompareIgnoreCase( modelName, "SPHERE_3D" ) )
                    {
                        ret = CreateResource<Sphere3D>( resourceCache(), item );
                    }
                    else if ( Util::CompareIgnoreCase( modelName, "QUAD_3D" ) )
                    {
                        P32 quadMask;
                        quadMask.i = 0;
                        quadMask.b[0] = 1;
                        item.mask( quadMask );
                        ret = CreateResource<Quad3D>( resourceCache(), item );

                        Quad3D* quad = static_cast<Quad3D*>(ret.get());
                        quad->setCorner( Quad3D::CornerLocation::TOP_LEFT, vec3<F32>( 0, 1, 0 ) );
                        quad->setCorner( Quad3D::CornerLocation::TOP_RIGHT, vec3<F32>( 1, 1, 0 ) );
                        quad->setCorner( Quad3D::CornerLocation::BOTTOM_LEFT, vec3<F32>( 0, 0, 0 ) );
                        quad->setCorner( Quad3D::CornerLocation::BOTTOM_RIGHT, vec3<F32>( 1, 0, 0 ) );
                    }
                    else
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_SCENE_UNSUPPORTED_GEOM" ), sceneNode.name.c_str() );
                    }
                }
                if ( ret != nullptr )
                {
                    ResourceDescriptor materialDescriptor( (sceneNode.name + "_material").c_str() );
                    Material_ptr tempMaterial = CreateResource<Material>( resourceCache(), materialDescriptor );
                    tempMaterial->properties().shadingMode( ShadingMode::PBR_MR );
                    ret->setMaterialTpl( tempMaterial );
                    ret->addStateCallback( ResourceState::RES_LOADED, loadModelComplete );
                }
                skipAdd = false;
            }
            else
            {
                switch ( sceneNode.typeHash )
                {
                    case _ID( "ROOT" ):
                    {
                        // Nothing to do with the root. This hasn't been used for a while
                    } break;
                    case _ID( "TERRAIN" ):
                    {
                        _loadingTasks.fetch_add( 1 );
                        normalMask |= to_base( ComponentType::RENDERING );
                        addTerrain( parent, nodeTree, sceneNode.name );
                    } break;
                    case _ID( "VEGETATION_GRASS" ):
                    {
                        normalMask |= to_base( ComponentType::RENDERING );
                        NOP(); //we rebuild grass every time
                    } break;
                    case _ID( "INFINITE_PLANE" ):
                    {
                        _loadingTasks.fetch_add( 1 );
                        normalMask |= to_base( ComponentType::RENDERING );
                        if ( !addInfPlane( parent, nodeTree, sceneNode.name ) )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    case _ID( "WATER" ):
                    {
                        _loadingTasks.fetch_add( 1 );
                        normalMask |= to_base( ComponentType::RENDERING );
                        addWater( parent, nodeTree, sceneNode.name );
                    } break;
                    case _ID( "MESH" ):
                    {
                        if ( !modelName.empty() )
                        {
                            _loadingTasks.fetch_add( 1 );
                            ResourceDescriptor model( modelName.str() );
                            model.assetLocation( Paths::g_assetsLocation + Paths::g_modelsLocation );
                            model.assetName( modelName );
                            model.flag( true );
                            model.waitForReady( true );
                            Mesh_ptr meshPtr = CreateResource<Mesh>( resourceCache(), model );
                            if ( meshPtr->getObjectFlag( Object3D::ObjectFlag::OBJECT_FLAG_SKINNED ) )
                            {
                                nodeStatic = false;
                            }
                            ret = meshPtr;
                            ret->addStateCallback( ResourceState::RES_LOADED, loadModelComplete );
                        }

                        skipAdd = false;
                    } break;
                    // SubMesh (change component properties, as the meshes should already be loaded)
                    case _ID( "SUBMESH" ):
                    {
                        while ( parent->getNode().getState() != ResourceState::RES_LOADED )
                        {
                            if ( parentTask != nullptr && !idle() )
                            {
                                NOP();
                            }
                        }
                        SceneGraphNode* subMesh = parent->findChild( _ID( sceneNode.name.c_str() ), false, false );
                        if ( subMesh != nullptr )
                        {
                            subMesh->loadFromXML( nodeTree );
                        }
                    } break;
                    case _ID( "SKY" ):
                    {
                        //ToDo: Change this - Currently, just load the default sky.
                        normalMask |= to_base( ComponentType::RENDERING );
                        if ( !addSky( parent, nodeTree, sceneNode.name ) )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    // Everything else
                    default:
                    case _ID( "TRANSFORM" ):
                    {
                        skipAdd = false;
                        normalMask &= ~to_base( ComponentType::BOUNDS );
                    } break;
                }
            }

            if ( !skipAdd )
            {
                SceneGraphNodeDescriptor nodeDescriptor = {};
                nodeDescriptor._name = sceneNode.name;
                nodeDescriptor._componentMask = normalMask;
                nodeDescriptor._node = ret;
                nodeDescriptor._usageContext = nodeStatic ? NodeUsageContext::NODE_STATIC : NodeUsageContext::NODE_DYNAMIC;

                for ( auto i = 1u; i < to_base( ComponentType::COUNT ) + 1; ++i )
                {
                    const U32 componentBit = 1u << i;
                    const ComponentType type = static_cast<ComponentType>(componentBit);
                    if ( nodeTree.count( TypeUtil::ComponentTypeToString( type ) ) != 0 )
                    {
                        nodeDescriptor._componentMask |= componentBit;
                    }
                }

                crtNode = parent->addChildNode( nodeDescriptor );

                crtNode->loadFromXML( nodeTree );
            }
        }

        const U32 childCount = to_U32( sceneNode.children.size() );
        if ( childCount == 1u )
        {
            loadAsset( parentTask, sceneNode.children.front(), crtNode );
        }
        else if ( childCount > 1u )
        {
            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = childCount;
            descriptor._partitionSize = 3u;
            descriptor._priority = TaskPriority::DONT_CARE;
            descriptor._useCurrentThread = true;
            descriptor._cbk = [this, &sceneNode, &crtNode]( const Task* innerTask, const U32 start, const U32 end )
            {
                for ( U32 i = start; i < end; ++i )
                {
                    loadAsset( innerTask, sceneNode.children[i], crtNode );
                }
            };
            parallel_for( _context, descriptor );
        }
    }

    SceneGraphNode* Scene::addParticleEmitter( const Str<256>& name,
                                              std::shared_ptr<ParticleData> data,
                                              SceneGraphNode* parentNode ) const
    {
        DIVIDE_ASSERT( !name.empty(),
                      "Scene::addParticleEmitter error: invalid name specified!" );

        const ResourceDescriptor particleEmitter( name );
        std::shared_ptr<ParticleEmitter> emitter = CreateResource<ParticleEmitter>( resourceCache(), particleEmitter );

        DIVIDE_ASSERT( emitter != nullptr,
                      "Scene::addParticleEmitter error: Could not instantiate emitter!" );


        auto initData = [emitter, data]( )
        {
            if ( !emitter->initData( data ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        };

        TaskPool& pool = _context.taskPool( TaskPoolType::HIGH_PRIORITY );
        Task* initTask = CreateTask( TASK_NOP );
        Start( *initTask, pool, TaskPriority::DONT_CARE, initData);
        Wait( *initTask, pool);

        SceneGraphNodeDescriptor particleNodeDescriptor;
        particleNodeDescriptor._node = emitter;
        particleNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
        particleNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                                to_base( ComponentType::BOUNDS ) |
                                                to_base( ComponentType::RENDERING ) |
                                                to_base( ComponentType::NETWORKING ) |
                                                to_base( ComponentType::SELECTION );

        return parentNode->addChildNode( particleNodeDescriptor );
    }

    void Scene::addTerrain( SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName )
    {
        Console::printfn( LOCALE_STR( "XML_LOAD_TERRAIN" ), nodeName.c_str() );

        // Load the rest of the terrain
        TerrainDescriptor ter( (nodeName + "_descriptor").c_str() );
        if ( !ter.loadFromXML( pt, nodeName.c_str() ) )
        {
            return;
        }

        const auto registerTerrain = [this, nodeName, parentNode, pt]( CachedResource* res )
        {
            SceneGraphNodeDescriptor terrainNodeDescriptor;
            terrainNodeDescriptor._name = nodeName;
            terrainNodeDescriptor._node = std::static_pointer_cast<Terrain>(res->shared_from_this());
            terrainNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
            terrainNodeDescriptor._componentMask = to_base( ComponentType::NAVIGATION ) |
                                                   to_base( ComponentType::TRANSFORM ) |
                                                   to_base( ComponentType::RIGID_BODY ) |
                                                   to_base( ComponentType::BOUNDS ) |
                                                   to_base( ComponentType::RENDERING ) |
                                                   to_base( ComponentType::NETWORKING );
            terrainNodeDescriptor._node->loadFromXML( pt );

            SceneGraphNode* terrainTemp = parentNode->addChildNode( terrainNodeDescriptor );

            NavigationComponent* nComp = terrainTemp->get<NavigationComponent>();
            nComp->navigationContext( NavigationComponent::NavigationContext::NODE_OBSTACLE );

            terrainTemp->loadFromXML( pt );
            _loadingTasks.fetch_sub( 1 );
        };

        ResourceDescriptor descriptor( ter.getVariable( "terrainName" ) );
        descriptor.propertyDescriptor( ter );
        descriptor.flag( ter.active() );
        descriptor.waitForReady( false );
        Terrain_ptr ret = CreateResource<Terrain>( resourceCache(), descriptor );
        ret->addStateCallback( ResourceState::RES_LOADED, registerTerrain );
    }

    void Scene::toggleFlashlight( const PlayerIndex idx )
    {
        SceneGraphNode*& flashLight = _flashLight[idx];
        if ( !flashLight )
        {
            SceneGraphNodeDescriptor lightNodeDescriptor;
            lightNodeDescriptor._serialize = false;
            lightNodeDescriptor._name = Util::StringFormat( "Flashlight_%d", idx ).c_str();
            lightNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
            lightNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                                 to_base( ComponentType::BOUNDS ) |
                                                 to_base( ComponentType::NETWORKING ) |
                                                 to_base( ComponentType::SPOT_LIGHT );
            flashLight = _sceneGraph->getRoot()->addChildNode( lightNodeDescriptor );
            SpotLightComponent* spotLight = flashLight->get<SpotLightComponent>();
            spotLight->castsShadows( true );
            spotLight->setDiffuseColour( DefaultColours::WHITE.rgb );
            flashLight->get<BoundsComponent>()->collisionsEnabled(false);

            _flashLight[idx] = flashLight;

            _cameraUpdateListeners[idx] = playerCamera( idx )->addUpdateListener( [this, idx]( const Camera& cam )
            {
                if ( idx < _scenePlayers.size() && idx < _flashLight.size() && _flashLight[idx] )
                {
                    if ( cam.getGUID() == playerCamera( idx )->getGUID() )
                    {
                        TransformComponent* tComp = _flashLight[idx]->get<TransformComponent>();
                        tComp->setPosition( cam.snapshot()._eye );
                        tComp->setRotationEuler( cam.euler() );
                    }
                }
            } );
        }

        flashLight->get<SpotLightComponent>()->toggleEnabled();
    }

    SceneGraphNode* Scene::addSky( SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName )
    {
        ResourceDescriptor skyDescriptor( ("DefaultSky_" + nodeName).c_str() );


        //skyDescriptor.ID(2);

        //ToDo: Double check that this diameter is correct, otherwise fall back to default of "2"
        skyDescriptor.ID( to_U32( std::floor( Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT )->snapshot()._zPlanes.max * 2 ) ) );

        Sky_ptr skyItem = CreateResource<Sky>( resourceCache(), skyDescriptor );
        DIVIDE_ASSERT( skyItem != nullptr, "Scene::addSky error: Could not create sky resource!" );
        skyItem->loadFromXML( pt );

        SceneGraphNodeDescriptor skyNodeDescriptor;
        skyNodeDescriptor._node = skyItem;
        skyNodeDescriptor._name = nodeName;
        skyNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
        skyNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                           to_base( ComponentType::BOUNDS ) |
                                           to_base( ComponentType::RENDERING ) |
                                           to_base( ComponentType::NETWORKING );

        SceneGraphNode* skyNode = parentNode->addChildNode( skyNodeDescriptor );
        skyNode->setFlag( SceneGraphNode::Flags::VISIBILITY_LOCKED );
        skyNode->loadFromXML( pt );
        skyNode->get<BoundsComponent>()->collisionsEnabled( false );

        return skyNode;
    }

    void Scene::addWater( SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName )
    {
        const auto registerWater = [this, nodeName, &parentNode, pt]( CachedResource* res )
        {
            SceneGraphNodeDescriptor waterNodeDescriptor;
            waterNodeDescriptor._name = nodeName;
            waterNodeDescriptor._node = std::static_pointer_cast<WaterPlane>(res->shared_from_this());
            waterNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
            waterNodeDescriptor._componentMask = to_base( ComponentType::NAVIGATION ) |
                                                 to_base( ComponentType::TRANSFORM ) |
                                                 to_base( ComponentType::RIGID_BODY ) |
                                                 to_base( ComponentType::BOUNDS ) |
                                                 to_base( ComponentType::RENDERING ) |
                                                 to_base( ComponentType::NETWORKING );

            waterNodeDescriptor._node->loadFromXML( pt );

            SceneGraphNode* waterNode = parentNode->addChildNode( waterNodeDescriptor );
            waterNode->loadFromXML( pt );
            _loadingTasks.fetch_sub( 1 );
        };

        ResourceDescriptor waterDescriptor( ("Water_" + nodeName).c_str() );
        waterDescriptor.waitForReady( false );
        WaterPlane_ptr ret = CreateResource<WaterPlane>( resourceCache(), waterDescriptor );
        ret->addStateCallback( ResourceState::RES_LOADED, registerWater );
    }

    SceneGraphNode* Scene::addInfPlane( SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName )
    {
        ResourceDescriptor planeDescriptor( ("InfPlane_" + nodeName).c_str() );

        const Camera* baseCamera = Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT );

        const U32 cameraFarPlane = to_U32( baseCamera->snapshot()._zPlanes.max );
        planeDescriptor.data().set( cameraFarPlane, cameraFarPlane, 0u );

        auto planeItem = CreateResource<InfinitePlane>( resourceCache(), planeDescriptor );

        DIVIDE_ASSERT( planeItem != nullptr, "Scene::addInfPlane error: Could not create infinite plane resource!" );
        planeItem->addStateCallback( ResourceState::RES_LOADED, [this]( [[maybe_unused]] CachedResource* res ) noexcept
        {
            _loadingTasks.fetch_sub( 1 );
        } );
        planeItem->loadFromXML( pt );

        SceneGraphNodeDescriptor planeNodeDescriptor;
        planeNodeDescriptor._node = planeItem;
        planeNodeDescriptor._name = nodeName;
        planeNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
        planeNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                             to_base( ComponentType::BOUNDS ) |
                                             to_base( ComponentType::RENDERING );

        SceneGraphNode* ret = parentNode->addChildNode( planeNodeDescriptor );
        ret->loadFromXML( pt );
        return ret;
    }

    U16 Scene::registerInputActions()
    {
        _input->flushCache();

        const auto none = []( [[maybe_unused]] const InputParams param ) noexcept
        {};

        const auto deleteSelection = [this]( const InputParams param )
        {
            const PlayerIndex idx = getPlayerIndexForDevice( param._deviceIndex );
            Selections& playerSelections = _currentSelection[idx];
            for ( U8 i = 0u; i < playerSelections._selectionCount; ++i )
            {
                _sceneGraph->removeNode( playerSelections._selections[i] );
            }
            playerSelections._selectionCount = 0u;
            playerSelections._selections.fill( -1 );
        };

        const auto increaseCameraSpeed = [this]( const InputParams param )
        {
            Camera* cam = playerCamera( getPlayerIndexForDevice( param._deviceIndex ) );
            if ( cam->mode() != Camera::Mode::STATIC &&
                cam->mode() != Camera::Mode::SCRIPTED )
            {
                if ( cam->speedFactor().move < Camera::MAX_CAMERA_MOVE_SPEED )
                {
                    cam->speedFactor().move += 1.f;
                }
                if ( cam->speedFactor().turn < Camera::MAX_CAMERA_TURN_SPEED )
                {
                    cam->speedFactor().turn += 1.f;
                }
            }
        };

        const auto decreaseCameraSpeed = [this]( const InputParams param )
        {
            Camera* cam = playerCamera( getPlayerIndexForDevice( param._deviceIndex ) );
            if ( cam->mode() != Camera::Mode::STATIC &&
                 cam->mode() != Camera::Mode::SCRIPTED )
            {
                if ( cam->speedFactor().move > 1.f )
                {
                    cam->speedFactor().move -= 1.f;
                }
                if ( cam->speedFactor().turn > 1.f )
                {
                    cam->speedFactor().turn -= 1.f;
                }
            }
        };

        const auto increaseResolution = [this]( [[maybe_unused]] const InputParams param )
        {
            _context.gfx().increaseResolution();
        };
        const auto decreaseResolution = [this]( [[maybe_unused]] const InputParams param )
        {
            _context.gfx().decreaseResolution();
        };

        const auto moveForward = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::POSITIVE );
        };

        const auto moveBackwards = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NEGATIVE );
        };

        const auto stopMoveFWDBCK = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NONE);
        };

        const auto strafeLeft = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NEGATIVE );
        };

        const auto strafeRight = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::POSITIVE );
        };

        const auto stopStrafeLeftRight = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NONE );
        };

        const auto rollCCW = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._roll.push( MoveDirection::NEGATIVE );
        };

        const auto rollCW = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._roll.push( MoveDirection::POSITIVE );
        };

        const auto stopRollCCWCW = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._roll.push( MoveDirection::NONE );
        };

        const auto turnLeft = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::NEGATIVE );
        };

        const auto turnRight = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::POSITIVE );
        };

        const auto stopTurnLeftRight = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::NONE );
        };

        const auto turnUp = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::NEGATIVE );
        };

        const auto turnDown = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::POSITIVE );
        };

        const auto stopTurnUpDown = [this]( const InputParams param )
        {
            state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::NONE );
        };

        const auto togglePauseState = [this]( const InputParams /*param*/ ) noexcept
        {
            _context.kernel().timingData().freezeGameTime( !_context.kernel().timingData().freezeGameTime() );
        };

        const auto takeScreenShot = [this]( [[maybe_unused]] const InputParams param )
        {
            state()->screenshotRequestQueued(true);
        };

        const auto toggleFullScreen = [this]( [[maybe_unused]] const InputParams param )
        {
            _context.gfx().toggleFullScreen();
        };

        const auto toggleFlashLight = [this]( const InputParams param )
        {
            toggleFlashlight( getPlayerIndexForDevice( param._deviceIndex ) );
        };

        const auto lockCameraToMouse = [this]( const InputParams  param )
        {
            if ( !lockCameraToPlayerMouse( getPlayerIndexForDevice( param._deviceIndex ), true ) )
            {
                NOP();
            }
        };
        const auto releaseCameraFromMouse = [this]( const InputParams  param )
        {
            if ( !lockCameraToPlayerMouse( getPlayerIndexForDevice( param._deviceIndex ), false ) )
            {
                NOP();
            }
        };

        const auto shutdown = [this]( [[maybe_unused]] const InputParams param ) noexcept
        {
            _context.app().RequestShutdown(false);
        };

        const auto povNavigation = [this]( const InputParams param )
        {
            const U32 povMask = param._var[0];

            if ( povMask & to_base( Input::JoystickPovDirection::UP ) )
            {  // Going up
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::POSITIVE );
            }
            if ( povMask & to_base( Input::JoystickPovDirection::DOWN ) )
            {  // Going down
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NEGATIVE );
            }
            if ( povMask & to_base( Input::JoystickPovDirection::RIGHT ) )
            {  // Going right
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::POSITIVE );
            }
            if ( povMask & to_base( Input::JoystickPovDirection::LEFT ) )
            {  // Going left
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NEGATIVE );
            }
            if ( povMask == to_base( Input::JoystickPovDirection::CENTERED ) )
            {  // stopped/centered out
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NONE );
                state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NONE );
            }
        };

        const auto axisNavigation = [this]( const InputParams param )
        {
            const I32 axisABS = param._var[0];
            const I32 axis = param._var[1];
            //const bool isGamePad = param._var[2] == 1;
            const I32 deadZone = param._var[3];

            switch ( axis )
            {
                case 0:
                {
                    if ( axisABS > deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::POSITIVE );
                    }
                    else if ( axisABS < -deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::NEGATIVE );
                    }
                    else
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleUD.push( MoveDirection::NONE );
                    }
                } break;
                case 1:
                {
                    if ( axisABS > deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::POSITIVE );
                    }
                    else if ( axisABS < -deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::NEGATIVE );
                    }
                    else
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._angleLR.push( MoveDirection::NONE );
                    }
                } break;

                case 2:
                {
                    if ( axisABS < -deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::POSITIVE );
                    }
                    else if ( axisABS > deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NEGATIVE );
                    }
                    else
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveFB.push( MoveDirection::NONE );
                    }
                } break;
                case 3:
                {
                    if ( axisABS < -deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NEGATIVE );
                    }
                    else if ( axisABS > deadZone )
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::POSITIVE );
                    }
                    else
                    {
                        state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) )._moveLR.push( MoveDirection::NONE );
                    }
                } break;
                default: DIVIDE_UNEXPECTED_CALL(); break;
            }
        };

        const auto toggleDebugInterface = [this]( [[maybe_unused]] const InputParams param ) noexcept
        {
            _context.debug().enabled( !_context.debug().enabled() );
        };

        const auto toggleEditor = [this]( [[maybe_unused]] const InputParams param )
        {
            if constexpr( Config::Build::ENABLE_EDITOR )
            {
                _context.editor().toggle( !_context.editor().running() );
            }
        };

        const auto toggleConsole = [this]( [[maybe_unused]] const InputParams param )
        {
            if constexpr( Config::Build::ENABLE_EDITOR )
            {
                _context.gui().getConsole().setVisible( !_context.gui().getConsole().isVisible() );
            }
        };

        const auto dragSelectBegin = [this]( const InputParams param )
        {
            beginDragSelection( getPlayerIndexForDevice( param._deviceIndex ), vec2<I32>( param._var[2], param._var[3] ) );
        };
        const auto dragSelectEnd = [this]( const InputParams param )
        {
            endDragSelection( getPlayerIndexForDevice( param._deviceIndex ), true );
        };

        InputActionList& actions = _input->actionList();
        bool ret = true;
        U16 actionID = 0;
        ret = actions.registerInputAction( actionID++, none ) && ret;                   // 0
        ret = actions.registerInputAction( actionID++, deleteSelection ) && ret;        // 1
        ret = actions.registerInputAction( actionID++, increaseCameraSpeed ) && ret;    // 2
        ret = actions.registerInputAction( actionID++, decreaseCameraSpeed ) && ret;    // 3
        ret = actions.registerInputAction( actionID++, increaseResolution ) && ret;     // 4
        ret = actions.registerInputAction( actionID++, decreaseResolution ) && ret;     // 5
        ret = actions.registerInputAction( actionID++, moveForward ) && ret;            // 6
        ret = actions.registerInputAction( actionID++, moveBackwards ) && ret;          // 7
        ret = actions.registerInputAction( actionID++, stopMoveFWDBCK ) && ret;         // 8
        ret = actions.registerInputAction( actionID++, strafeLeft ) && ret;             // 9
        ret = actions.registerInputAction( actionID++, strafeRight ) && ret;            // 10
        ret = actions.registerInputAction( actionID++, stopStrafeLeftRight ) && ret;    // 11
        ret = actions.registerInputAction( actionID++, rollCCW ) && ret;                // 12
        ret = actions.registerInputAction( actionID++, rollCW ) && ret;                 // 13
        ret = actions.registerInputAction( actionID++, stopRollCCWCW ) && ret;          // 14
        ret = actions.registerInputAction( actionID++, turnLeft ) && ret;               // 15
        ret = actions.registerInputAction( actionID++, turnRight ) && ret;              // 16
        ret = actions.registerInputAction( actionID++, stopTurnLeftRight ) && ret;      // 17
        ret = actions.registerInputAction( actionID++, turnUp ) && ret;                 // 18
        ret = actions.registerInputAction( actionID++, turnDown ) && ret;               // 19
        ret = actions.registerInputAction( actionID++, stopTurnUpDown ) && ret;         // 20
        ret = actions.registerInputAction( actionID++, togglePauseState ) && ret;       // 21
        ret = actions.registerInputAction( actionID++, takeScreenShot ) && ret;         // 22
        ret = actions.registerInputAction( actionID++, toggleFullScreen ) && ret;       // 23
        ret = actions.registerInputAction( actionID++, toggleFlashLight ) && ret;       // 24
        ret = actions.registerInputAction( actionID++, lockCameraToMouse ) && ret;      // 25
        ret = actions.registerInputAction( actionID++, releaseCameraFromMouse ) && ret; // 26
        ret = actions.registerInputAction( actionID++, shutdown ) && ret;               // 27
        ret = actions.registerInputAction( actionID++, povNavigation ) && ret;          // 28
        ret = actions.registerInputAction( actionID++, axisNavigation ) && ret;         // 29
        ret = actions.registerInputAction( actionID++, toggleDebugInterface ) && ret;   // 30
        ret = actions.registerInputAction( actionID++, toggleEditor ) && ret;           // 31
        ret = actions.registerInputAction( actionID++, toggleConsole ) && ret;          // 32
        ret = actions.registerInputAction( actionID++, dragSelectBegin ) && ret;        // 33
        ret = actions.registerInputAction( actionID++, dragSelectEnd ) && ret;          // 34

        if ( !ret )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        return actionID;
    }

    bool Scene::lockCameraToPlayerMouse( const PlayerIndex index, const bool lockState ) const noexcept
    {
        static bool hadWindowGrab = false;
        static vec2<I32> lastMousePosition;

        state()->playerState( index ).cameraLockedToMouse( lockState );

        const DisplayWindow* window = _context.app().windowManager().getFocusedWindow();
        if ( lockState )
        {
            if ( window != nullptr )
            {
                hadWindowGrab = window->grabState();
            }
            lastMousePosition = WindowManager::GetGlobalCursorPosition();
            WindowManager::ToggleRelativeMouseMode( true );
        }
        else
        {
            WindowManager::ToggleRelativeMouseMode( false );
            state()->playerState( index ).resetMoveDirections();
            if ( window != nullptr )
            {
                window->grabState( hadWindowGrab );
            }
            WindowManager::SetGlobalCursorPosition( lastMousePosition.x, lastMousePosition.y );
        }

        return true;
    }

    bool Scene::load()
    {
        // Load the main scene from XML
        if ( !loadXML() )
        {
            return false;
        }

        const bool errorState = false;

        setState( ResourceState::RES_LOADING );

        Camera* baseCamera = Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT );
        const F32 hFoV = _context.config().runtime.horizontalFOV;
        const F32 vFoV = Angle::to_VerticalFoV( hFoV, to_D64( baseCamera->snapshot()._aspectRatio ) );
        baseCamera->setProjection( vFoV, { Camera::s_minNearZ, _context.config().runtime.cameraViewDistance } );
        baseCamera->speedFactor().move = Camera::DEFAULT_CAMERA_MOVE_SPEED;
        baseCamera->speedFactor().turn = Camera::DEFAULT_CAMERA_TURN_SPEED;
        baseCamera->updateLookAt();

        SceneGraphNode* rootNode = _sceneGraph->getRoot();
        vector<XML::SceneNode>& rootChildren = _xmlSceneGraphRootNode.children;
        const size_t childCount = rootChildren.size();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = to_U32( childCount );
        descriptor._partitionSize = 3u;

        if ( descriptor._iterCount > descriptor._partitionSize * 3u )
        {
            descriptor._priority = TaskPriority::DONT_CARE;
            descriptor._useCurrentThread = true;
            descriptor._allowPoolIdle = true;
            descriptor._waitForFinish = true;
            descriptor._cbk = [this, &rootNode, &rootChildren]( const Task* parentTask, const U32 start, const U32 end )
            {
                for ( U32 i = start; i < end; ++i )
                {
                    loadAsset( parentTask, rootChildren[i], rootNode );
                }
            };
            parallel_for( _context, descriptor );
        }
        else
        {
            for ( U32 i = 0u; i < descriptor._iterCount; ++i )
            {
                loadAsset( nullptr, rootChildren[i], rootNode );
            }
        }
        WAIT_FOR_CONDITION( _loadingTasks.load() == 0u );

        // We always add a sky
        const auto& skies = sceneGraph()->getNodesByType( SceneNodeType::TYPE_SKY );
        assert( !skies.empty() );
        Sky& currentSky = skies[0]->getNode<Sky>();
        const auto& dirLights = _lightPool->getLights( LightType::DIRECTIONAL );

        DirectionalLightComponent* sun = nullptr;
        for ( auto light : dirLights )
        {
            const auto dirLight = light->sgn()->get<DirectionalLightComponent>();
            if ( dirLight->tag() == SUN_LIGHT_TAG )
            {
                sun = dirLight;
                break;
            }
        }
        if ( sun != nullptr )
        {
            sun->castsShadows( true );
            initDayNightCycle( currentSky, *sun );
        }

        if ( errorState )
        {
            Console::errorfn( LOCALE_STR( "ERROR_SCENE_LOAD" ), "scene load function" );
            return false;
        }

        loadComplete( true );
        [[maybe_unused]] const U16 lastActionID = registerInputActions();

        XML::loadDefaultKeyBindings( (Paths::g_xmlDataLocation + "keyBindings.xml").str(), this );

        return postLoad();
    }

    bool Scene::unload()
    {
        _aiManager->stop();
        WAIT_FOR_CONDITION( !_aiManager->running() );

        U32 totalLoadingTasks = _loadingTasks.load();
        while ( totalLoadingTasks > 0 )
        {
            const U32 actualTasks = _loadingTasks.load();
            if ( totalLoadingTasks != actualTasks )
            {
                totalLoadingTasks = actualTasks;
            }
            std::this_thread::yield();
        }

        clearTasks();

        for ( const size_t idx : _selectionCallbackIndices )
        {
            _parent.removeSelectionCallback( idx );
        }
        _selectionCallbackIndices.clear();

        _context.pfx().destroyPhysicsScene( *this );

        /// Unload scenegraph
        _xmlSceneGraphRootNode = {};
        _flashLight.fill( nullptr );
        _sceneGraph->unload();

        loadComplete( false );
        assert( _scenePlayers.empty() );

        return true;
    }

    bool Scene::postLoad()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        _sceneGraph->postLoad();

        return _context.pfx().initPhysicsScene( *this );
    }

    void Scene::postLoadMainThread()
    {
        assert( Runtime::isMainThread() );
        setState( ResourceState::RES_LOADED );
    }

    string Scene::GetPlayerSGNName( const PlayerIndex idx )
    {
        return Util::StringFormat( g_defaultPlayerName, idx + 1 );
    }

    void Scene::currentPlayerPass( const PlayerIndex idx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        //ToDo: These don't necessarily need to match -Ionut
        updateCameraControls( idx );
        state()->renderState().renderPass( idx );
        state()->playerPass( idx );

        if ( state()->playerState().cameraUnderwater() )
        {
            _context.gfx().getRenderer().postFX().pushFilter( FilterType::FILTER_UNDERWATER );
        }
        else
        {
            _context.gfx().getRenderer().postFX().popFilter( FilterType::FILTER_UNDERWATER );
        }
    }

    void Scene::onSetActive()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        _aiManager->pauseUpdate( false );

        input()->onSetActive();
        _context.sfx().stopMusic();
        _context.sfx().dumpPlaylists();

        for ( U32 i = 0u; i < to_base( MusicType::COUNT ); ++i )
        {
            const SceneState::MusicPlaylist& playlist = state()->music( static_cast<MusicType>(i) );
            if ( !playlist.empty() )
            {
                for ( const auto& song : playlist )
                {
                    _context.sfx().addMusic( i, song.second );
                }
            }
        }
        if ( !_context.sfx().playMusic( 0 ) )
        {
            //DIVIDE_UNEXPECTED_CALL();
            NOP();
        }

        assert( _parent.getActivePlayerCount() == 0 );
        addPlayerInternal( false );
    }

    void Scene::onRemoveActive()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _aiManager->pauseUpdate( true );

        while ( !_scenePlayers.empty() )
        {
            Attorney::SceneManagerScene::removePlayer( _parent, *this, _scenePlayers.back()->getBoundNode(), false );
        }

        input()->onRemoveActive();
    }

    void Scene::addPlayerInternal( const bool queue )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        // Limit max player count
        if ( _parent.getActivePlayerCount() == Config::MAX_LOCAL_PLAYER_COUNT )
        {
            return;
        }

        const string playerName = GetPlayerSGNName( static_cast<PlayerIndex>(_parent.getActivePlayerCount()) );

        SceneGraphNode* playerSGN( _sceneGraph->findNode( playerName.c_str() ) );
        if ( !playerSGN )
        {
            SceneGraphNode* root = _sceneGraph->getRoot();

            SceneGraphNodeDescriptor playerNodeDescriptor;
            playerNodeDescriptor._serialize = false;
            playerNodeDescriptor._node = std::make_shared<SceneNode>( resourceCache(), to_size( generateGUID() + _parent.getActivePlayerCount() ), playerName.c_str(), ResourcePath{playerName}, ResourcePath{}, SceneNodeType::TYPE_TRANSFORM, 0u);
            playerNodeDescriptor._name = playerName.c_str();
            playerNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
            playerNodeDescriptor._componentMask = to_base( ComponentType::UNIT ) |
                to_base( ComponentType::TRANSFORM ) |
                to_base( ComponentType::BOUNDS ) |
                to_base( ComponentType::NETWORKING );

            playerSGN = root->addChildNode( playerNodeDescriptor );
        }

        Attorney::SceneManagerScene::addPlayer( _parent, *this, playerSGN, queue );
        DIVIDE_ASSERT( playerSGN->get<UnitComponent>()->getUnit() != nullptr );
    }

    void Scene::removePlayerInternal( const PlayerIndex idx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        assert( idx < _scenePlayers.size() );

        Attorney::SceneManagerScene::removePlayer( _parent, *this, _scenePlayers[getSceneIndexForPlayer( idx )]->getBoundNode(), true );
    }

    void Scene::onPlayerAdd( const Player_ptr& player )
    {
        _scenePlayers.push_back( player.get() );
        state()->onPlayerAdd( player->index() );
        input()->onPlayerAdd( player->index() );
    }

    void Scene::onPlayerRemove( const Player_ptr& player )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const PlayerIndex idx = player->index();

        input()->onPlayerRemove( idx );
        state()->onPlayerRemove( idx );
        _cameraUpdateListeners[idx] = 0u;
        if ( _flashLight[idx] != nullptr )
        {
            _sceneGraph->getRoot()->removeChildNode( _flashLight[idx] );
            _flashLight[idx] = nullptr;
        }
        _sceneGraph->getRoot()->removeChildNode( player->getBoundNode() );

        _scenePlayers.erase( std::cbegin( _scenePlayers ) + getSceneIndexForPlayer( idx ) );
    }

    U8 Scene::getSceneIndexForPlayer( const PlayerIndex idx ) const
    {
        for ( U8 i = 0; i < to_U8( _scenePlayers.size() ); ++i )
        {
            if ( _scenePlayers[i]->index() == idx )
            {
                return i;
            }
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0;
    }

    Player* Scene::getPlayerForIndex( const PlayerIndex idx ) const
    {
        return _scenePlayers[getSceneIndexForPlayer( idx )];
    }

    U8 Scene::getPlayerIndexForDevice( const U8 deviceIndex ) const
    {
        return input()->getPlayerIndexForDevice( deviceIndex );
    }

    bool Scene::mouseMoved( const Input::MouseMoveEvent& arg )
    {
        if ( !arg._wheelEvent )
        {
            const PlayerIndex idx = getPlayerIndexForDevice( arg._deviceIndex );
            DragSelectData& data = _dragSelectData[idx];
            if ( data._isDragging )
            {
                data._endDragPos = vec2<U16>(arg.state().X.abs, arg.state().Y.abs);
                updateSelectionData( idx, data );
            }
            else
            {
                const bool sceneFocused = Config::Build::ENABLE_EDITOR ? !_context.editor().hasFocus() : true;
                const bool sceneHovered = Config::Build::ENABLE_EDITOR ? !_context.editor().isHovered() : true;

                if ( sceneFocused && sceneHovered && !state()->playerState( idx ).cameraLockedToMouse() )
                {
                    findHoverTarget( idx, vec2<U16>( arg.state().X.abs, arg.state().Y.abs ) );
                }
                else if ( !sceneHovered )
                {
                    clearHoverTarget( idx );
                }
            }
        }
        return false;
    }

    bool Scene::updateCameraControls( const PlayerIndex idx ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        Camera* cam = playerCamera( idx );
        if ( cam->mode() == Camera::Mode::STATIC ||
             cam->mode() == Camera::Mode::SCRIPTED )
        {
            return false;
        }

        SceneStatePerPlayer& playerState = state()->playerState( idx );
        const bool updated = cam->moveFromPlayerState(playerState);
        playerState.cameraUpdated( updated );

        if ( updated )
        {
            playerState.cameraUnderwater( checkCameraUnderwater( *cam ) );
        }

        return updated;
    }

    void Scene::updateSceneState( const U64 deltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        sceneRuntimeUS( sceneRuntimeUS() + deltaTimeUS );

        updateSceneStateInternal( deltaTimeUS );
        _state->waterBodies().resize(0);
        _sceneGraph->sceneUpdate( deltaTimeUS, *_state );
        _aiManager->update( deltaTimeUS );
    }

    void Scene::updateSceneStateInternal( [[maybe_unused]] const U64 deltaTimeUS )
    {
    }

    void Scene::onChangeFocus( const bool hasFocus )
    {
        if ( !hasFocus )
        {
            //Add a focus flag and ignore redundant calls

            for ( const Player* player : _scenePlayers )
            {
                state()->playerState( player->index() ).resetMoveDirections();
                endDragSelection( player->index(), false );
            }
            _parent.wantsMouse( false );
            //_context.kernel().timingData().freezeGameTime(true);
        }
        else
        {
            NOP();
        }
    }

    void Scene::registerTask( Task& taskItem, const bool start, const TaskPriority priority )
    {
        {
            LockGuard<SharedMutex> w_lock( _tasksMutex );
            _tasks.push_back( &taskItem );
        }
        if ( start )
        {
            Start( taskItem, _context.taskPool( TaskPoolType::HIGH_PRIORITY ), priority );
        }
    }

    void Scene::clearTasks()
    {
        Console::printfn( LOCALE_STR( "STOP_SCENE_TASKS" ) );
        // Performance shouldn't be an issue here
        LockGuard<SharedMutex> w_lock( _tasksMutex );
        for ( const Task* task : _tasks )
        {
            Wait( *task, _context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
        }

        _tasks.clear();
    }

    void Scene::removeTask( const Task& task )
    {
        LockGuard<SharedMutex> w_lock( _tasksMutex );
        for ( vector<Task*>::iterator it = begin( _tasks ); it != end( _tasks ); ++it )
        {
            if ( (*it)->_id == task._id )
            {
                Wait( **it, _context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
                _tasks.erase( it );
                return;
            }
        }
    }

    void Scene::processInput( [[maybe_unused]] PlayerIndex idx, [[maybe_unused]] const U64 gameDeltaTimeUS, [[maybe_unused]] const U64 appDeltaTimeUS )
    {
    }

    void Scene::addGuiTimer( const TimerClass intervalClass, const U64 intervalUS, DELEGATE<void, U64/*elapsed time*/> cbk )
    {
        if ( !cbk )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        _guiTimers.emplace_back( TimerStruct
        {
            ._callbackIntervalUS = intervalUS,
            ._timerClass = intervalClass,
            ._cbk = cbk
        });
    }

    void Scene::addTaskTimer( const TimerClass intervalClass, const U64 intervalUS, DELEGATE<void, U64/*elapsed time*/> cbk )
    {
        if ( !cbk )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        _taskTimers.emplace_back( TimerStruct
        {
            ._callbackIntervalUS = intervalUS,
            ._timerClass = intervalClass,
            ._cbk = cbk
        });
    }

    void Scene::processInternalTimers( const U64 appDeltaUS, const U64 gameDeltaUS, vector<TimerStruct>& timers )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        eastl::for_each( begin( timers ),
                         end( timers ),
                         [appDeltaUS, gameDeltaUS]( TimerStruct& timer)
        {
            const U64 delta = timer._timerClass == TimerClass::APP_TIME ? appDeltaUS : gameDeltaUS;

            timer._internalTimer += delta;
            timer._internalTimerTotal += delta;

            if ( timer._internalTimer >= timer._callbackIntervalUS )
            {
                timer._cbk(timer._internalTimerTotal );
                timer._internalTimer = 0u;
            }

        });
    }

    void Scene::processGUI( const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        processInternalTimers( appDeltaTimeUS, gameDeltaTimeUS, _guiTimers);
    }

    void Scene::processTasks( const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        static bool increaseWeatherScale = true;

        processInternalTimers( appDeltaTimeUS, gameDeltaTimeUS, _taskTimers );

        if ( _dayNightData._skyInstance != nullptr )
        {
            static struct tm timeOfDay = {};

            const vec3<F32> sunPosition = _dayNightData._skyInstance->getSunPosition( _dayNightData._sunLight->range() );
            const vec3<F32> sunDirection = Normalized( VECTOR3_ZERO - sunPosition );

            if ( _dayNightData._resetTime )
            {
                const time_t t = time( nullptr );
                timeOfDay = *localtime( &t );
                timeOfDay.tm_hour = _dayNightData._time._hour;
                timeOfDay.tm_min = _dayNightData._time._minutes;
                _dayNightData._resetTime = false;
                _dayNightData._timeAccumulatorSec = Time::Seconds( 1.1f );
                _dayNightData._timeAccumulatorHour = 0.f;
                if ( _envProbePool != nullptr )
                {
                    SceneEnvironmentProbePool::OnTimeOfDayChange( *_envProbePool );
                }

                _dayNightData._sunLight->sgn()->get<TransformComponent>()->setDirection( sunDirection );
            }

            const F32 speedFactor = dayNightCycleEnabled() ? _dayNightData._speedFactor : 0.f;
            const F32 deltaSeconds = Time::MillisecondsToSeconds<F32>( gameDeltaTimeUS );
            const F32 addTime = speedFactor * deltaSeconds;
            if ( addTime > 0.f )
            {
                _dayNightData._timeAccumulatorSec += addTime;
                _dayNightData._timeAccumulatorHour += addTime;
                Atmosphere atmosphere = _dayNightData._skyInstance->atmosphere();
                if ( atmosphere._cloudCoverage > 0.9f && increaseWeatherScale )
                {
                    increaseWeatherScale = false;
                }
                else if ( atmosphere._cloudCoverage < 0.1f && !increaseWeatherScale )
                {
                    increaseWeatherScale = true;
                }
                atmosphere._cloudCoverage += (deltaSeconds * (increaseWeatherScale ? 0.001f : -0.001f));

                _dayNightData._skyInstance->setAtmosphere( atmosphere );
            }
            if ( std::abs( _dayNightData._timeAccumulatorSec ) > Time::Seconds( 1.f ) )
            {
                timeOfDay.tm_sec += to_I32( _dayNightData._timeAccumulatorSec );
                const time_t now = mktime( &timeOfDay ); // normalize it
                _dayNightData._timeAccumulatorSec = 0.f;

                if ( _dayNightData._timeAccumulatorHour > 1.f )
                {
                    if ( _envProbePool != nullptr )
                    {
                        SceneEnvironmentProbePool::OnTimeOfDayChange( *_envProbePool );
                    }
                    _dayNightData._timeAccumulatorHour = 0.f;
                }
                const FColour3 moonColour = Normalized( _dayNightData._skyInstance->moonColour().rgb );
                const SunInfo details = _dayNightData._skyInstance->setDateTimeAndLocation( localtime( &now ), _dayNightData._location );
                Light* light = _dayNightData._sunLight;

                const FColour3 sunsetOrange = FColour3( 99.2f, 36.9f, 32.5f ) / 100.f;

                // Sunset / sunrise
                const F32 altitudeDegrees = Angle::RadiansToDegrees( details.altitude );
                const bool isNight = altitudeDegrees < -25.f;
                // Dawn
                if ( IS_IN_RANGE_INCLUSIVE( altitudeDegrees, 0.0f, 25.0f ) )
                {
                    light->setDiffuseColour( Lerp( sunsetOrange, DefaultColours::WHITE.rgb, altitudeDegrees / 25.f ) );
                }
                // Dusk
                else if ( IS_IN_RANGE_INCLUSIVE( altitudeDegrees, -25.0f, 0.0f ) )
                {
                    light->setDiffuseColour( Lerp( sunsetOrange, moonColour, -altitudeDegrees / 25.f ) );
                }
                // Night
                else if ( isNight )
                {
                    light->setDiffuseColour( moonColour );
                }
                // Day
                else
                {
                    light->setDiffuseColour( DefaultColours::WHITE.rgb );
                }
                _dayNightData._time._hour = to_U8( timeOfDay.tm_hour );
                _dayNightData._time._minutes = to_U8( timeOfDay.tm_min );

                light->sgn()->get<TransformComponent>()->setDirection( sunDirection * (isNight ? -1 : 1) );
            }
        }
    }

    void Scene::drawCustomUI( const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( _linesPrimitive->hasBatch() )
        {
            GFX::EnqueueCommand( bufferInOut, GFX::SetViewportCommand{ targetViewport } );
            _linesPrimitive->getCommandBuffer( bufferInOut, memCmdInOut );
        }
    }

    void Scene::debugDraw( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        // Show NavMeshes
        _aiManager->debugDraw( bufferInOut, memCmdInOut, false );
        _lightPool->drawLightImpostors( bufferInOut );
    }

    bool Scene::checkCameraUnderwater( const PlayerIndex idx ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const Camera* crtCamera = Attorney::SceneManagerCameraAccessor::playerCamera( _parent, idx );
        return checkCameraUnderwater( *crtCamera );
    }

    bool Scene::checkCameraUnderwater( const Camera& camera ) const noexcept
    {
        const vec3<F32>& eyePos = camera.snapshot()._eye;
        {
            const auto& waterBodies = state()->waterBodies();
            for ( const WaterBodyData& water : waterBodies )
            {
                const vec3<F32>& extents = water._extents;
                const vec3<F32>& position = water._positionW;
                const F32 halfWidth = (extents.x + position.x) * 0.5f;
                const F32 halfLength = (extents.z + position.z) * 0.5f;
                if ( eyePos.x >= -halfWidth && eyePos.x <= halfWidth &&
                    eyePos.z >= -halfLength && eyePos.z <= halfLength )
                {
                    const float depth = -extents.y + position.y;
                    return eyePos.y < position.y&& eyePos.y > depth;
                }
            }
        }

        return false;
    }

    void Scene::findHoverTarget( PlayerIndex idx, const vec2<I32> aimPos )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        constexpr std::array<SceneNodeType, 6> s_ignoredNodes = {
            SceneNodeType::TYPE_TRANSFORM,
            SceneNodeType::TYPE_WATER,
            SceneNodeType::TYPE_SKY,
            SceneNodeType::TYPE_PARTICLE_EMITTER,
            SceneNodeType::TYPE_INFINITEPLANE,
            SceneNodeType::TYPE_VEGETATION
        };

        const Camera* crtCamera = playerCamera( idx );
        const vec2<U16> renderingResolution = _context.gfx().renderingResolution();
        const vec3<F32> direction = crtCamera->unProject( to_F32( aimPos.x ),
                                                         renderingResolution.height - to_F32( aimPos.y ),
                                                         {
                                                              0,
                                                              0,
                                                              renderingResolution.width,
                                                              renderingResolution.height
                                                          } );

        // see if we select another one
        _sceneSelectionCandidates.resize( 0 );

        SGNIntersectionParams intersectionParams = {};
        intersectionParams._ray = { crtCamera->snapshot()._eye, direction };
        intersectionParams._range = crtCamera->snapshot()._zPlanes;
        intersectionParams._includeTransformNodes = true;
        intersectionParams._ignoredTypes = s_ignoredNodes.data();
        intersectionParams._ignoredTypesCount = s_ignoredNodes.size();

        // Cast the picking ray and find items between the nearPlane and far Plane
        sceneGraph()->intersect( intersectionParams, _sceneSelectionCandidates );

        if ( !_sceneSelectionCandidates.empty() )
        {
            // If we don't force selections, remove all of the nodes that lack a SelectionComponent
            eastl::sort( begin( _sceneSelectionCandidates ),
                        end( _sceneSelectionCandidates ),
                        []( const SGNRayResult& A, const SGNRayResult& B ) noexcept -> bool
            {
                return A.dist < B.dist;
            } );

            SceneGraphNode* target = nullptr;
            for ( const SGNRayResult& result : _sceneSelectionCandidates )
            {
                if ( result.inside || result.dist < 0.0f )
                {
                    continue;
                }

                SceneGraphNode* crtNode = _sceneGraph->findNode( result.sgnGUID );
                if ( DebugBreak( crtNode == nullptr ) )
                {
                    continue;
                }

                // In the editor, we can select ... anything ...
                if ( Config::Build::ENABLE_EDITOR && _context.editor().inEditMode() )
                {
                    target = crtNode;
                    break;
                }

            testNode:
                // Outside of the editor, we only select nodes that have a selection component
                if ( crtNode->get<SelectionComponent>() != nullptr && crtNode->get<SelectionComponent>()->enabled() )
                {
                    target = crtNode;
                    break;
                }

                if ( crtNode->getNode().type() == SceneNodeType::TYPE_SUBMESH )
                {
                    // In normal gameplay, we need to top node for the selection (i.e. a mesh for submeshe intersections)
                    // Because we use either the physics system or a recursive scenegraph intersection loop, we may end up with
                    // child-node data as a result
                    crtNode = crtNode->parent();
                    if ( crtNode != nullptr )
                    {
                        goto testNode;
                    }
                }
            }

            clearHoverTarget( idx );

            if ( target != nullptr )
            {
                _currentHoverTarget[idx] = target->getGUID();
                if ( !target->hasFlag( SceneGraphNode::Flags::SELECTED ) )
                {
                    target->setFlag( SceneGraphNode::Flags::HOVERED, true );
                }
            }
        }
        else
        {
            clearHoverTarget( idx );
        }
    }

    void Scene::clearHoverTarget( const PlayerIndex idx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        if ( _currentHoverTarget[idx] != -1 )
        {
            SceneGraphNode* oldTarget = _sceneGraph->findNode( _currentHoverTarget[idx] );
            if ( oldTarget != nullptr )
            {
                oldTarget->clearFlag( SceneGraphNode::Flags::HOVERED );
            }
        }

        _currentHoverTarget[idx] = -1;
    }

    void Scene::onNodeDestroy( SceneGraphNode* node )
    {
        const I64 guid = node->getGUID();
        for ( I64& target : _currentHoverTarget )
        {
            if ( target == guid )
            {
                target = -1;
            }
        }

        for ( Selections& playerSelections : _currentSelection )
        {
            for ( I8 i = playerSelections._selectionCount; i > 0; --i )
            {
                const I64 crtGUID = playerSelections._selections[i - 1];
                if ( crtGUID == guid )
                {
                    playerSelections._selections[i - 1] = -1;
                    std::swap( playerSelections._selections[i - 1], playerSelections._selections[playerSelections._selectionCount--] );
                }
            }
        }

        _parent.onNodeDestroy( node );
    }

    bool Scene::resetSelection( const PlayerIndex idx, const bool resetIfLocked )
    {
        Selections& tempSelections = _tempSelection[idx];
        Selections& playerSelections = _currentSelection[idx];
        const U8 selectionCount = playerSelections._selectionCount;

        tempSelections = {};

        for ( U8 i = 0; i < selectionCount; ++i )
        {
            SceneGraphNode* node = sceneGraph()->findNode( playerSelections._selections[i] );
            if ( node != nullptr && (!node->hasFlag( SceneGraphNode::Flags::SELECTION_LOCKED ) || resetIfLocked) )
            {
                node->clearFlag( SceneGraphNode::Flags::HOVERED, true );
                node->clearFlag( SceneGraphNode::Flags::SELECTED, true );
            }
            else if ( node != nullptr )
            {
                tempSelections._selections[tempSelections._selectionCount++] = node->getGUID();
            }
        }

        playerSelections = tempSelections;
        return tempSelections._selectionCount == 0u;
    }

    void Scene::setSelected( const PlayerIndex idx, const vector_fast<SceneGraphNode*>& SGNs, const bool recursive )
    {
        Selections& playerSelections = _currentSelection[idx];

        for ( SceneGraphNode* sgn : SGNs )
        {
            if ( !sgn->hasFlag( SceneGraphNode::Flags::SELECTED ) )
            {
                playerSelections._selections[playerSelections._selectionCount++] = sgn->getGUID();
                sgn->setFlag( SceneGraphNode::Flags::SELECTED, recursive );
            }
        }
    }

    const Selections& Scene::getCurrentSelection( const PlayerIndex index ) const
    {
        return _currentSelection[index];
    }

    bool Scene::findSelection( const PlayerIndex idx, const bool clearOld )
    {
        // Clear old selection
        if ( clearOld )
        {
            if ( !_parent.resetSelection( idx, false ) )
            {
                return false;
            }
        }

        const I64 hoverGUID = _currentHoverTarget[idx];
        // No hover target
        if ( hoverGUID == -1 )
        {
            return false;
        }

        Selections& playerSelections = _currentSelection[idx];
        for ( U8 i = 0u; i < playerSelections._selectionCount; ++i )
        {
            if ( playerSelections._selections[i] == hoverGUID )
            {
                //Already selected
                return true;
            }
        }

        SceneGraphNode* selectedNode = _sceneGraph->findNode( hoverGUID );
        if ( selectedNode != nullptr )
        {
            _parent.setSelected( idx, { selectedNode }, false );
            return true;
        }
        if ( !_parent.resetSelection( idx, false ) )
        {
            NOP();
        }
        return false;
    }

    void Scene::beginDragSelection( const PlayerIndex idx, const vec2<I32> mousePos )
    {
        if constexpr( Config::Build::ENABLE_EDITOR )
        {
            if ( _context.editor().running() && _context.editor().isHovered() )
            {
                return;
            }
        }

        DragSelectData& data = _dragSelectData[idx];
        data._startDragPos =  mousePos;
        data._endDragPos = data._startDragPos;
        data._isDragging = true;
    }

    void Scene::updateSelectionData( PlayerIndex idx, DragSelectData& data )
    {
        static std::array<Line, 4> s_lines = {
            Line{VECTOR3_ZERO, VECTOR3_UNIT, DefaultColours::GREEN_U8, DefaultColours::GREEN_U8, 2.0f, 1.0f},
            Line{VECTOR3_ZERO, VECTOR3_UNIT, DefaultColours::GREEN_U8, DefaultColours::GREEN_U8, 2.0f, 1.0f},
            Line{VECTOR3_ZERO, VECTOR3_UNIT, DefaultColours::GREEN_U8, DefaultColours::GREEN_U8, 2.0f, 1.0f},
            Line{VECTOR3_ZERO, VECTOR3_UNIT, DefaultColours::GREEN_U8, DefaultColours::GREEN_U8, 2.0f, 1.0f}
        };

        if constexpr( Config::Build::ENABLE_EDITOR )
        {
            const Editor& editor = _context.editor();
            if ( editor.hasFocus() )
            {
                endDragSelection( idx, false );
                return;
            }
        }

        _parent.wantsMouse( true );

        const vec2<U16> resolution = _context.gfx().renderingResolution();

        const vec2<I32> startPos = { data._startDragPos.x, resolution.height - data._startDragPos.y };

        const vec2<I32> endPos  = { data._endDragPos.x, resolution.height - data._endDragPos.y };

        const I32 startX = std::min( startPos.x, endPos.x );
        const I32 startY = std::min( startPos.y, endPos.y );

        const Rect<I32> selectionRect {
            startX,
            startY,
            std::abs( endPos.x - startPos.x ),
            std::abs( endPos.y - startPos.y )
        };

        //X0, Y0 -> X1, Y0
        s_lines[0].positionStart( { selectionRect.x, selectionRect.y, 0 } );
        s_lines[0].positionEnd( { selectionRect.x + selectionRect.z, selectionRect.y, 0 } );

        //X1 , Y0 -> X1, Y1
        s_lines[1].positionStart( { selectionRect.x + selectionRect.z, selectionRect.y, 0 } );
        s_lines[1].positionEnd( { selectionRect.x + selectionRect.z, selectionRect.y + selectionRect.w, 0 } );

        //X1, Y1 -> X0, Y1
        s_lines[2].positionStart( s_lines[1].positionEnd() );
        s_lines[2].positionEnd( { selectionRect.x, selectionRect.y + selectionRect.w, 0 } );

        //X0, Y1 -> X0, Y0
        s_lines[3].positionStart( s_lines[2].positionEnd() );
        s_lines[3].positionEnd( s_lines[0].positionStart() );

        _linesPrimitive->fromLines( s_lines.data(), s_lines.size() );

        if ( GFXDevice::FrameCount() % 2 == 0 )
        {
            clearHoverTarget( idx );
            if ( _parent.resetSelection( idx, false ) )
            {
                const Camera* crtCamera = playerCamera( idx );

                thread_local vector_fast<SceneGraphNode*> nodes;
                Attorney::SceneManagerScene::getNodesInScreenRect( _parent, selectionRect, *crtCamera, nodes );

                _parent.setSelected( idx, nodes, false );
            }
        }
    }

    void Scene::endDragSelection( const PlayerIndex idx, const bool clearSelection )
    {
        constexpr F32 DRAG_SELECTION_THRESHOLD_PX_SQ = 9.f;

        DragSelectData& data = _dragSelectData[idx];

        _linesPrimitive->clearBatch();
        _parent.wantsMouse( false );
        data._isDragging = false;
        if ( data._startDragPos.distanceSquared( data._endDragPos ) < DRAG_SELECTION_THRESHOLD_PX_SQ )
        {
            if ( !findSelection( idx, clearSelection ) )
            {
                NOP();
            }
        }
    }

    void Scene::initDayNightCycle( Sky& skyInstance, DirectionalLightComponent& sunLight ) noexcept
    {
        _dayNightData._skyInstance = &skyInstance;
        _dayNightData._sunLight = &sunLight;
        if ( !_dayNightData._resetTime )
        {
            // Usually loaded from XML/save data
            _dayNightData._time = skyInstance.GetTimeOfDay();
            _dayNightData._resetTime = true;
        }
        _dayNightData._timeAccumulatorSec = Time::Seconds( 1.1f );
        _dayNightData._timeAccumulatorHour = 0.f;
        sunLight.lockDirection( true );

        const vec3<F32> sunPosition = _dayNightData._skyInstance->getSunPosition( sunLight.range() );
        sunLight.sgn()->get<TransformComponent>()->setDirection( Normalized( VECTOR3_ZERO - sunPosition ) );
    }

    void Scene::setDayNightCycleTimeFactor( const F32 factor ) noexcept
    {
        _dayNightData._speedFactor = factor;
    }

    F32 Scene::getDayNightCycleTimeFactor() const noexcept
    {
        return _dayNightData._speedFactor;
    }

    void Scene::setTimeOfDay( const SimpleTime& time ) noexcept
    {
        _dayNightData._time = time;
        _dayNightData._resetTime = true;
    }

    const SimpleTime& Scene::getTimeOfDay() const noexcept
    {
        return _dayNightData._time;
    }

    void Scene::setGeographicLocation( const SimpleLocation& location ) noexcept
    {
        _dayNightData._location = location;
        _dayNightData._resetTime = true;
    }

    const SimpleLocation& Scene::getGeographicLocation() const noexcept
    {
        return _dayNightData._location;
    }

    [[nodiscard]] vec3<F32> Scene::getSunPosition() const
    {
        if ( _dayNightData._sunLight != nullptr )
        {
            return _dayNightData._sunLight->sgn()->get<TransformComponent>()->getWorldPosition();
        }
        return vec3<F32>(500, 500, 500);
    }

    [[nodiscard]] vec3<F32> Scene::getSunDirection() const
    {
        if ( _dayNightData._sunLight != nullptr )
        {
            return _dayNightData._sunLight->sgn()->get<TransformComponent>()->getLocalDirection();
        }

        return vec3<F32>(WORLD_Y_NEG_AXIS);
    }

    SunInfo Scene::getCurrentSunDetails() const noexcept
    {
        if ( _dayNightData._skyInstance != nullptr )
        {
            return _dayNightData._skyInstance->getCurrentDetails();
        }

        return {};
    }

    Atmosphere Scene::getCurrentAtmosphere() const noexcept
    {
        if ( _dayNightData._skyInstance != nullptr )
        {
            return _dayNightData._skyInstance->atmosphere();
        }

        return {};
    }

    void Scene::setCurrentAtmosphere( const Atmosphere& atmosphere ) const noexcept
    {
        if ( _dayNightData._skyInstance != nullptr )
        {
            return _dayNightData._skyInstance->setAtmosphere( atmosphere );
        }
    }

    bool Scene::save( ByteBuffer& outputBuffer ) const
    {
        outputBuffer << BYTE_BUFFER_VERSION;
        const U8 playerCount = to_U8( _scenePlayers.size() );
        outputBuffer << playerCount;
        for ( U8 i = 0; i < playerCount; ++i )
        {
            const Camera* cam = _scenePlayers[i]->camera();
            outputBuffer << _scenePlayers[i]->index() << cam->snapshot()._eye << cam->snapshot()._orientation;
        }

        return _sceneGraph->saveCache( outputBuffer );
    }

    bool Scene::load( ByteBuffer& inputBuffer )
    {

        if ( !inputBuffer.bufferEmpty() )
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            inputBuffer >> tempVer;
            if ( tempVer == BYTE_BUFFER_VERSION )
            {
                const U8 currentPlayerCount = to_U8( _scenePlayers.size() );

                vec3<F32> camPos;
                Quaternion<F32> camOrientation;

                U8 currentPlayerIndex = 0u;
                U8 previousPlayerCount = 0u;
                inputBuffer >> previousPlayerCount;
                for ( U8 i = 0; i < previousPlayerCount; ++i )
                {
                    inputBuffer >> currentPlayerIndex >> camPos >> camOrientation;
                    if ( currentPlayerIndex < currentPlayerCount )
                    {
                        Camera* cam = _scenePlayers[currentPlayerIndex]->camera();
                        cam->setEye( camPos );
                        cam->setRotation( camOrientation );
                        state()->playerState( currentPlayerIndex ).cameraUnderwater( checkCameraUnderwater( *cam ) );
                    }
                }
            }
            else
            {
                return false;
            }
        }

        return _sceneGraph->loadCache( inputBuffer );
    }

    Camera* Scene::playerCamera( const bool skipOverride ) const
    {
        return Attorney::SceneManagerCameraAccessor::playerCamera( _parent, skipOverride );
    }

    Camera* Scene::playerCamera( const U8 index, const bool skipOverride ) const
    {
        return Attorney::SceneManagerCameraAccessor::playerCamera( _parent, index, skipOverride );
    }

    void Attorney::SceneEnvironmentProbeComponent::registerProbe( const Scene& scene, EnvironmentProbeComponent* probe )
    {
        DIVIDE_ASSERT( scene._envProbePool != nullptr );

        scene._envProbePool->registerProbe( probe );
    }

    void Attorney::SceneEnvironmentProbeComponent::unregisterProbe( const Scene& scene, const EnvironmentProbeComponent* const probe )
    {
        DIVIDE_ASSERT( scene._envProbePool != nullptr );

        scene._envProbePool->unregisterProbe( probe );
    }

} //namespace Divide