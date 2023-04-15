#include "stdafx.h"

#include "Headers/PingPongScene.h"

#include "GUI/Headers/GUIButton.h"
#include "GUI/Headers/SceneGUIElements.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Managers/Headers/SceneManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Graphs/Headers/SceneGraph.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"

namespace Divide
{

    namespace
    {
        Task* g_gameTaskID = nullptr;
    }

    PingPongScene::PingPongScene( PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name )
        : Scene( context, cache, parent, name )
    {
        _sideDrift = 0;
        _directionTowardsAdversary = true;
        _upwardsDirection = false;
        _touchedOwnTableHalf = false;
        _touchedAdversaryTableHalf = false;
        _lost = false;
        _ball = nullptr;
        _paddleCam = nullptr;
        _score = 0;
        _freeFly = false;
        _wasInFreeFly = false;
    }

    void PingPongScene::processGUI( const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        constexpr D64 FpsDisplay = Time::SecondsToMilliseconds( 0.3 );

        if ( _guiTimersMS[to_base(TimerClass::APP_TIME)][0] >= FpsDisplay )
        {
            _guiTimersMS[to_base(TimerClass::APP_TIME)][0] = 0.0;
        }
        Scene::processGUI( gameDeltaTimeUS, appDeltaTimeUS );
    }

    void PingPongScene::processTasks( const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        static vec2<F32> _sunAngle =
            vec2<F32>( 0.0f, Angle::to_RADIANS( 45.0f ) );
        static bool direction = false;
        if ( !direction )
        {
            _sunAngle.y += 0.005f;
            _sunAngle.x += 0.005f;
        }
        else
        {
            _sunAngle.y -= 0.005f;
            _sunAngle.x -= 0.005f;
        }

        if ( _sunAngle.y <= Angle::to_RADIANS( 25 ) ||
            _sunAngle.y >= Angle::to_RADIANS( 70 ) )
            direction = !direction;

        _sunvector =
            vec3<F32>( -cosf( _sunAngle.x ) * sinf( _sunAngle.y ), -cosf( _sunAngle.y ),
                      -sinf( _sunAngle.x ) * sinf( _sunAngle.y ) );

        //_currentSky->getNode<Sky>().enableSun(true, _sun->get<DirectionalLightComponent>()->getDiffuseColour(), _sunvector);

        Scene::processTasks( gameDeltaTimeUS, appDeltaTimeUS );
    }

    void PingPongScene::resetGame()
    {
        _directionTowardsAdversary = true;
        _upwardsDirection = false;
        _touchedAdversaryTableHalf = false;
        _touchedOwnTableHalf = false;
        _lost = false;
        _sideDrift = 0;
        clearTasks();
        _ballSGN->get<TransformComponent>()->setPosition( vec3<F32>( 0, 2, 2 ) );
    }

    void PingPongScene::serveBall()
    {
        _GUI->modifyText( "insults", "", false );
        resetGame();

        removeTask( *g_gameTaskID );

        g_gameTaskID = CreateTask( [this]( const Task& /*parent*/ )
        {
            test( Random( 4 ), PushConstantType::INT );
        } );

        registerTask( *g_gameTaskID );
    }

    void PingPongScene::test( std::any a, PushConstantType type )
    {
        bool updated = false;
        string message;
        TransformComponent* ballTransform = _ballSGN->get<TransformComponent>();
        vec3<F32> ballPosition = ballTransform->getWorldPosition();

        SceneGraphNode* table( _sceneGraph->findNode( "table" ) );
        SceneGraphNode* net( _sceneGraph->findNode( "net" ) );
        SceneGraphNode* opponent( _sceneGraph->findNode( "opponent" ) );
        SceneGraphNode* paddle( _sceneGraph->findNode( "paddle" ) );

        vec3<F32> paddlePosition = paddle->get<TransformComponent>()->getWorldPosition();
        vec3<F32> opponentPosition = opponent->get<TransformComponent>()->getWorldPosition();
        vec3<F32> tablePosition = table->get<TransformComponent>()->getWorldPosition();

        // Is the ball coming towards us or towards the opponent?
        _directionTowardsAdversary ? ballPosition.z -= 0.11f : ballPosition.z +=
            0.11f;
        // Up or down?
        _upwardsDirection ? ballPosition.y += 0.084f : ballPosition.y -= 0.084f;

        // Is the ball moving to the right or to the left?
        ballPosition.x += _sideDrift * 0.15f;
        if ( !COMPARE( opponentPosition.x, ballPosition.x ) )
            opponent->get<TransformComponent>()->translateX(
                ballPosition.x - opponentPosition.x );

        ballTransform->translate( ballPosition - ballTransform->getWorldPosition() );

        // Did we hit the table? Bounce then ...
        if ( table->get<BoundsComponent>()->getBoundingBox().collision( _ballSGN->get<BoundsComponent>()->getBoundingBox() ) )
        {
            if ( ballPosition.z > tablePosition.z )
            {
                _touchedOwnTableHalf = true;
                _touchedAdversaryTableHalf = false;
            }
            else
            {
                _touchedOwnTableHalf = false;
                _touchedAdversaryTableHalf = true;
            }
            _upwardsDirection = true;
        }
        // Kinetic  energy depletion
        if ( ballPosition.y > 2.1f ) _upwardsDirection = false;

        // Did we hit the paddle?
        if ( _ballSGN->get<BoundsComponent>()->getBoundingBox().collision( paddle->get<BoundsComponent>()->getBoundingBox() ) )
        {
            _sideDrift = ballPosition.x - paddlePosition.x;
            // If we hit the ball with the upper margin of the paddle, add a slight
            // impuls to the ball
            if ( ballPosition.y >= paddlePosition.y ) ballPosition.z -= 0.12f;

            _directionTowardsAdversary = true;
        }

        if ( ballPosition.y + 0.75f < table->get<BoundsComponent>()->getBoundingBox().getMax().y )
        {
            // If we hit the ball and it landed on the opponent's table half
            // Or if the opponent hit the ball and it landed on our table half
            if ( _touchedAdversaryTableHalf && _directionTowardsAdversary ||
                !_directionTowardsAdversary && !_touchedOwnTableHalf )
                _lost = false;
            else
                _lost = true;

            updated = true;
        }
        // Did we win or lose?
        if ( ballPosition.z >= paddlePosition.z )
        {
            _lost = true;
            updated = true;
        }
        if ( ballPosition.z <= opponentPosition.z )
        {
            _lost = false;
            updated = true;
        }

        if ( _ballSGN->get<BoundsComponent>()->getBoundingBox().collision( net->get<BoundsComponent>()->getBoundingBox() ) )
        {
            if ( _directionTowardsAdversary )
            {
                // Did we hit the net?
                _lost = true;
            }
            else
            {
                // Did the opponent hit the net?
                _lost = false;
            }
            updated = true;
        }

        // Did we hit the opponent? Then change ball direction ... BUT ...
        // Add a small chance that we win
        if ( Random( 30 ) != 2 )
            if ( _ballSGN->get<BoundsComponent>()
                                ->getBoundingBox().collision( opponent->get<BoundsComponent>()
                                                              ->getBoundingBox() ) )
            {
                _sideDrift =
                    ballPosition.x -
                    opponent->get<TransformComponent>()->getWorldPosition().x;
                _directionTowardsAdversary = false;
            }
        // Add a spin effect to the ball
        ballTransform->rotate( vec3<F32>( ballPosition.z, 1, 1 ) );

        if ( updated )
        {
            if ( _lost )
            {
                message = "You lost!";
                _score--;

                if ( type == PushConstantType::INT )
                {
                    I32 quote = std::any_cast<I32>(a);
                    if ( _score % 3 == 0 )
                        _GUI->modifyText( "insults", _quotes[quote], false );
                }
            }
            else
            {
                message = "You won!";
                _score++;
            }

            _GUI->modifyText( "Score", Util::StringFormat( "Score: %d", _score ), false );
            _GUI->modifyText( "Message", message, false );
            resetGame();
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    }

    void PingPongScene::processInput( const PlayerIndex idx, const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
    {
        if ( _freeFly )
        {
            _wasInFreeFly = true;
            Scene::processInput( idx, gameDeltaTimeUS, appDeltaTimeUS );
            return;
        }
        if ( _wasInFreeFly )
        {
            // Position the camera
            _paddleCam->setPitch( -90 );
            _paddleCam->setYaw( 0 );
            _paddleCam->setRoll( 0 );
            _wasInFreeFly = false;
        }
        // Move FB = Forward/Back = up/down
        // Move LR = Left/Right
        static F32 paddleMovementDivisor = 10;
        // Camera controls
        if ( state()->playerState( idx )._angleLR.top() != MoveDirection::NONE)
        {
            _paddleCam->rotateYaw( Angle::DEGREES<F32>( state()->playerState( idx )._angleLR.top() ) );
        }
        if ( state()->playerState( idx )._angleUD.top() != MoveDirection::NONE )
        {
            _paddleCam->rotatePitch( Angle::DEGREES<F32>( state()->playerState( idx )._angleUD.top() ) );
        }

        SceneGraphNode* paddle( _sceneGraph->findNode( "paddle" ) );

        const vec3<F32> pos = paddle->get<TransformComponent>()->getWorldPosition();

        // Paddle movement is limited to the [-3,3] range except for Y-descent
        if ( state()->playerState( idx )._moveFB.top() != MoveDirection::NONE )
        {
            if ( state()->playerState( idx )._moveFB.top() == MoveDirection::POSITIVE && pos.y >= 3 ||
                 state()->playerState( idx )._moveFB.top() == MoveDirection::NEGATIVE && pos.y <= 0.5f )
            {
                Scene::processInput( idx, gameDeltaTimeUS, appDeltaTimeUS );
                return;
            }
            paddle->get<TransformComponent>()->translateY( to_I32( state()->playerState( idx )._moveFB.top() ) / paddleMovementDivisor );
        }

        if ( state()->playerState( idx )._moveLR.top() != MoveDirection::NONE )
        {
            // Left/right movement is flipped for proper control
            if ( state()->playerState( idx )._moveLR.top() == MoveDirection::NEGATIVE && pos.x >= 3 ||
                 state()->playerState( idx )._moveLR.top() == MoveDirection::POSITIVE && pos.x <= -3 )
            {
                Scene::processInput( idx, gameDeltaTimeUS, appDeltaTimeUS );
                return;
            }
            paddle->get<TransformComponent>()->translateX( to_I32( state()->playerState( idx )._moveLR.top() ) / paddleMovementDivisor );
        }

        Scene::processInput( idx, gameDeltaTimeUS, appDeltaTimeUS );
    }

    bool PingPongScene::load()
    {
        _freeFly = false;
        _wasInFreeFly = false;

        // Load scene resources
        const bool loadState = Scene::load();

        ResourceDescriptor ballDescriptor( "Ping Pong Ball" );
        ballDescriptor.ID( 16u );
        ballDescriptor.enumValue( Util::FLOAT_TO_UINT( 0.1f ) );
        _ball = CreateResource<Sphere3D>( resourceCache(), ballDescriptor );
        _ball->getMaterialTpl()->properties().shadingMode( ShadingMode::PBR_MR );
        _ball->getMaterialTpl()->properties().baseColour( FColour4( 0.4f, 0.4f, 0.4f, 1.0f ) );
        _ball->getMaterialTpl()->properties().roughness( 0.6f );
        _ball->getMaterialTpl()->properties().metallic( 0.8f );

        SceneGraphNodeDescriptor ballNodeDescriptor;
        ballNodeDescriptor._node = _ball;
        ballNodeDescriptor._name = "PingPongBallSGN";
        ballNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
        ballNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
            to_base( ComponentType::BOUNDS ) |
            to_base( ComponentType::RENDERING ) |
            to_base( ComponentType::RIGID_BODY ) |
            to_base( ComponentType::NAVIGATION ) |
            to_base( ComponentType::NETWORKING );
        _ballSGN = _sceneGraph->getRoot()->addChildNode( ballNodeDescriptor );
        _ballSGN->get<TransformComponent>()->translate( vec3<F32>( 0, 2, 2 ) );
        _ballSGN->get<RigidBodyComponent>()->physicsCollisionGroup( PhysicsGroup::GROUP_KINEMATIC );
        // Add some taunts
        _quotes.push_back( "Ha ha ... even Odin's laughin'!" );
        _quotes.push_back( "If you're a ping-pong player, I'm Jimmy Page" );
        _quotes.push_back(
            "Ooolee, ole ole ole, see the ball? ... It's past your end" );
        _quotes.push_back(
            "You're lucky the room's empty. I'd be so ashamed otherwise if I were "
            "you" );
        _quotes.push_back( "It's not the hard. Even a monkey can do it." );

        _guiTimersMS[to_base(TimerClass::APP_TIME)].push_back(0.0);  // Fps
        _taskTimers[to_base( TimerClass::GAME_TIME )].push_back(0.0);  // Light

        _paddleCam = Camera::CreateCamera( "paddleCam", Camera::Mode::FREE_FLY );
        _paddleCam->fromCamera( *playerCamera() );
        // Position the camera
        // renderState().getCamera().setPitch(-90);
        _paddleCam->movementLocked( true );
        _paddleCam->setEye( vec3<F32>( 0, 2.5f, 6.5f ) );


        return loadState;
    }

    U16 PingPongScene::registerInputActions()
    {
        U16 actionID = Scene::registerInputActions();

        {
            //ToDo: Move these to per-scene XML file
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( const InputParams /*param*/ )
            {
                serveBall();
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
            _input->addJoystickMapping( Input::Joystick::JOYSTICK_1, Input::JoystickElementType::BUTTON_PRESS, 0, actionEntry );
            actionID++;
        }
        {
            PressReleaseActions::Entry actionEntry = {};
            actionEntry.releaseIDs().insert( actionID );
            if ( !_input->actionList().registerInputAction( actionID, [this]( const InputParams param )
            {
                _freeFly = !_freeFly;
                if ( !_freeFly )
                {
                    state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) ).overrideCamera( _paddleCam );
                }
                else
                {
                    state()->playerState( getPlayerIndexForDevice( param._deviceIndex ) ).overrideCamera( nullptr );
                }
            } ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            };
            _input->addKeyMapping( Input::KeyCode::KC_L, actionEntry );
        }
        return actionID++;
    }

    void PingPongScene::postLoadMainThread()
    {
        const vec2<U16> screenResolution = _context.gfx().renderTargetPool().getRenderTarget( RenderTargetNames::SCREEN )->getResolution();
        const Rect<U16> targetRenderViewport = { 0u, 0u, screenResolution.width, screenResolution.height };

        // Buttons and text labels
        GUIButton* btn = _GUI->addButton( "Serve",
                                         "Serve",
                                         pixelPosition( to_I32( targetRenderViewport.sizeX - 120 ),
                                                        to_I32( targetRenderViewport.sizeY / 1.1f ) ),
                                         pixelScale( 100, 25 ) );

        btn->setEventCallback( GUIButton::Event::MouseClick, [this]( I64 /*btnGUID*/ )
        {
            serveBall();
        } );

        _GUI->addText( "Score",
                      pixelPosition( to_I32( targetRenderViewport.sizeX - 120 ),
                                     to_I32( targetRenderViewport.sizeY / 1.3f ) ),
            Font::DIVIDE_DEFAULT,
                      UColour4( 255, 0, 0, 255 ),
            Util::StringFormat( "Score: %d", 0 ) );

        _GUI->addText( "Message",
                      pixelPosition( to_I32( targetRenderViewport.sizeX - 120 ),
                                     to_I32( targetRenderViewport.sizeY / 1.5f ) ),
            Font::DIVIDE_DEFAULT,
                      UColour4( 255, 0, 0, 255 ),
            "" );
        _GUI->addText( "insults",
                      pixelPosition( targetRenderViewport.sizeX / 4,
                                     targetRenderViewport.sizeY / 3 ),
            Font::DIVIDE_DEFAULT,
                      UColour4( 0, 255, 0, 255 ),
            "" );

        Scene::postLoadMainThread();
    }

}