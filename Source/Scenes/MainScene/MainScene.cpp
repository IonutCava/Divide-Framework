

#include "Headers/MainScene.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Environment/Water/Headers/Water.h"
#include "Geometry/Material/Headers/Material.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Dynamics/Entities/Units/Headers/Player.h"
#include "GUI/Headers/SceneGUIElements.h"

#include "Graphs/Headers/SceneGraph.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/NavigationComponent.h"

namespace Divide
{

    namespace
    {
        Task* g_boxMoveTaskID = nullptr;
    }

    MainScene::MainScene( PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str<256>& name )
        : Scene( context, cache, parent, name )
    {
    }


    void MainScene::processInput( PlayerIndex idx, const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        if ( state()->playerState( idx ).cameraUpdated() )
        {
            Camera& cam = *getPlayerForIndex( idx )->camera();
            const vec3<F32>& eyePos = cam.snapshot()._eye;
            const vec3<F32>& euler = cam.euler();
            if ( !_freeflyCamera )
            {
                F32 terrainHeight = 0.0f;

                vector<SceneGraphNode*> terrains = _sceneGraph->getNodesByType( SceneNodeType::TYPE_TERRAIN );
                // Mutable copy
                vec3<F32> eyePosition = eyePos;
                for ( SceneGraphNode* terrainNode : terrains )
                {
                    const Terrain& ter = terrainNode->getNode<Terrain>();

                    CLAMP<F32>( eyePosition.x,
                               ter.getDimensions().width * 0.5f * -1.0f,
                               ter.getDimensions().width * 0.5f );
                    CLAMP<F32>( eyePosition.z,
                               ter.getDimensions().height * 0.5f * -1.0f,
                               ter.getDimensions().height * 0.5f );

                    mat4<F32> mat = MAT4_IDENTITY;
                    terrainNode->get<TransformComponent>()->getWorldMatrix( mat );
                    vec3<F32> position = mat * ter.getVertFromGlobal( eyePosition.x, eyePosition.z, true )._position;
                    terrainHeight = position.y;
                    if ( !IS_ZERO( terrainHeight ) )
                    {
                        eyePosition.y = terrainHeight + 1.85f;
                        cam.setEye( eyePosition );
                        break;
                    }
                }
                _GUI->modifyText( "camPosition",
                                 Util::StringFormat( "[ X: %5.2f | Y: %5.2f | Z: %5.2f ] [Pitch: %5.2f | Yaw: %5.2f] [TerHght: %5.2f ]",
                                                     eyePos.x, eyePos.y, eyePos.z, euler.pitch, euler.yaw, terrainHeight ), false );
            }
            else
            {
                _GUI->modifyText( "camPosition",
                                 Util::StringFormat( "[ X: %5.2f | Y: %5.2f | Z: %5.2f ] [Pitch: %5.2f | Yaw: %5.2f]",
                                                     eyePos.x, eyePos.y, eyePos.z, euler.pitch, euler.yaw ), false );
            }
        }

        Scene::processInput( idx, gameDeltaTimeUS, appDeltaTimeUS );
    }

    bool MainScene::load()
    {
        // Load scene resources
        const bool loadState = Scene::load();
        Camera* baseCamera = Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT );
        baseCamera->speedFactor().move = 10.0f;

        ResourceDescriptor infiniteWater( "waterEntity" );
        infiniteWater.data( vec3<U32>( baseCamera->snapshot()._zPlanes.max ) );
        const WaterPlane_ptr water = CreateResource<WaterPlane>( resourceCache(), infiniteWater );

        SceneGraphNodeDescriptor waterNodeDescriptor;
        waterNodeDescriptor._node = water;
        waterNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
        waterNodeDescriptor._componentMask = to_base( ComponentType::NAVIGATION ) |
            to_base( ComponentType::TRANSFORM ) |
            to_base( ComponentType::BOUNDS ) |
            to_base( ComponentType::RENDERING ) |
            to_base( ComponentType::NETWORKING );
        SceneGraphNode* waterGraphNode = _sceneGraph->getRoot()->addChildNode( waterNodeDescriptor );

        waterGraphNode->get<NavigationComponent>()->navigationContext( NavigationComponent::NavigationContext::NODE_IGNORE );


        if ( loadState )
        {
            addGuiTimer( TimerClass::APP_TIME,
                         Time::SecondsToMicroseconds( 0.5 ),
                         [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                         {
                             _GUI->modifyText( "underwater",
                                               Util::StringFormat( "Underwater [ %s ] | WaterLevel [%.2f] ]", state()->playerState( 0 ).cameraUnderwater() ? "true" : "false", state()->waterBodies()[0]._positionW.y ),
                                               false );
                             _GUI->modifyText( "RenderBinCount",
                                                Util::StringFormat( "Number of items in Render Bin: %d.", _context.kernel().renderPassManager()->getLastTotalBinSize( RenderStage::DISPLAY ) ),
                                                 false );
                         });

            addGuiTimer( TimerClass::APP_TIME,
                         Time::SecondsToMicroseconds( 1.0 ),
                         [this]( const U64 elapsedTimeUS )
                         {
                             _GUI->modifyText( "timeDisplay",
                                              Util::StringFormat( "Elapsed time: %5.0f", Time::MicrosecondsToSeconds( elapsedTimeUS )),
                                              false );
                         });

            removeTask( *g_boxMoveTaskID );
            g_boxMoveTaskID = CreateTask( [this]( const Task& /*parent*/ )
            {
                test();
            } );

            registerTask( *g_boxMoveTaskID );

            ResourceDescriptor beepSound( "beep sound" );
            beepSound.assetName( ResourcePath{ "beep.wav" } );
            beepSound.assetLocation( Paths::g_assetsLocation + Paths::g_soundsLocation );
            _beep = CreateResource<AudioDescriptor>( resourceCache(), beepSound );

            return true;
        }

        return false;
    }

    U16 MainScene::registerInputActions()
    {
        U16 actionID = Scene::registerInputActions();

        {
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( InputParams /*param*/ )
            {
                _context.sfx().playSound( _beep );
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            _input->addKeyMapping( Input::KeyCode::KC_X, actionEntry );
            actionID++;
        }
        {
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( InputParams /*param*/ )
            {
                _musicPlaying = !_musicPlaying;
                if ( _musicPlaying )
                {
                    const SceneState::MusicPlaylist::const_iterator it = state()->music( MusicType::TYPE_BACKGROUND ).find( _ID( "themeSong" ) );
                    if ( it != std::cend( state()->music( MusicType::TYPE_BACKGROUND ) ) )
                    {
                        _context.sfx().playMusic( it->second );
                    }
                }
                else
                {
                    _context.sfx().stopMusic();
                }
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            _input->addKeyMapping( Input::KeyCode::KC_M, actionEntry );
            actionID++;
        }
        {
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( const InputParams param )
            {
                _freeflyCamera = !_freeflyCamera;
                Camera* cam = _scenePlayers[getPlayerIndexForDevice( param._deviceIndex )]->camera();
                cam->speedFactor().move =  _freeflyCamera ? 20.0f : 10.0f;
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            _input->addKeyMapping( Input::KeyCode::KC_L, actionEntry );
            actionID++;
        }
        {
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( InputParams /*param*/ )
            {
                vector<SceneGraphNode*> terrains =  _sceneGraph->getNodesByType( SceneNodeType::TYPE_TERRAIN );

                for ( SceneGraphNode* terrainNode : terrains )
                {
                    terrainNode->getNode<Terrain>().toggleBoundingBoxes();
                }
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            _input->addKeyMapping( Input::KeyCode::KC_T, actionEntry );
        }
        return actionID++;
    }

    bool MainScene::unload()
    {
        _context.sfx().stopMusic();
        _context.sfx().stopAllSounds();
        g_boxMoveTaskID = nullptr;

        return Scene::unload();
    }

    void MainScene::test()
    {
        static bool switchAB = false;
        g_boxMoveTaskID = CreateTask( [this]( const Task& /*parent*/ )
        {
            vec3<F32> pos;
            SceneGraphNode* boxNode( _sceneGraph->findNode( "box" ) );

            if ( boxNode )
            {
                pos = boxNode->get<TransformComponent>()->getWorldPosition();
            }

            if ( !switchAB )
            {
                if ( pos.x < 300 && IS_ZERO( pos.z ) ) pos.x++;
                if ( COMPARE( pos.x, 300.f ) )
                {
                    if ( pos.y < 800 && IS_ZERO( pos.z ) ) pos.y++;
                    if ( COMPARE( pos.y, 800.f ) )
                    {
                        if ( pos.z > -500 ) pos.z--;
                        if ( COMPARE( pos.z, -500.f ) ) switchAB = true;
                    }
                }
            }
            else
            {
                if ( pos.x > -300 && COMPARE( pos.z, -500 ) ) pos.x--;
                if ( COMPARE( pos.x, -300.f ) )
                {
                    if ( pos.y > 100 && COMPARE( pos.z, -500 ) ) pos.y--;
                    if ( COMPARE( pos.y, 100 ) )
                    {
                        if ( pos.z < 0 ) pos.z++;
                        if ( IS_ZERO( pos.z ) ) switchAB = false;
                    }
                }
            }
            if ( boxNode )
            {
                boxNode->get<TransformComponent>()->setPosition( pos );
            }
        });

        registerTask( *g_boxMoveTaskID );
    }

    void MainScene::postLoadMainThread()
    {
        _GUI->addText( "timeDisplay",
                       pixelPosition( 60, 80 ),
                       Font::DIVIDE_DEFAULT,
                       UColour4( 164, 64, 64, 255 ),
                       "Elapsed time: 0.0f" );

        _GUI->addText( "underwater",
                       pixelPosition( 60, 115 ),
                       Font::DIVIDE_DEFAULT,
                       UColour4( 64, 200, 64, 255 ),
                       "Underwater [ false ] | WaterLevel [ 0.0f ] ]");

        _GUI->addText( "RenderBinCount",
                       pixelPosition( 60, 135 ),
                       Font::BATANG,
                       UColour4( 164, 64, 64, 255 ),
                       "Number of items in Render Bin: 0");

        const vec3<F32>& eyePos = Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT )->snapshot()._eye;
        const vec3<F32>& euler = Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT )->euler();
        _GUI->addText( "camPosition",
                       pixelPosition( 60, 100 ),
                       Font::DIVIDE_DEFAULT,
                       UColour4( 64, 200, 64, 255 ),
                       Util::StringFormat( "Position [ X: %5.0f | Y: %5.0f | Z: %5.0f ] [Pitch: %5.2f | Yaw: %5.2f]",
                                            eyePos.x, eyePos.y, eyePos.z, euler.pitch, euler.yaw ) );

        Scene::postLoadMainThread();
    }

}
