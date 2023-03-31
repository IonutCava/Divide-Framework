#include "stdafx.h"

#include "Headers/DefaultScene.h"

#include "GUI/Headers/GUIButton.h"

#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Dynamics/Entities/Units/Headers/Player.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "GUI/Headers/SceneGUIElements.h"
#include "Platform/Headers/DisplayWindow.h"
#include "Managers/Headers/RenderPassManager.h"


namespace Divide {
DefaultScene::DefaultScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name)
    : Scene(context, cache, parent, name)
{
    Scene::DEFAULT_SCENE_GUID = getGUID();
}

bool DefaultScene::load() {
    const bool loadState = Scene::load();
    state()->saveLoadDisabled(true);
    _guiTimersMS[to_base( TimerClass::APP_TIME )].push_back( 0.0 );  // Fps
    _guiTimersMS[to_base( TimerClass::APP_TIME )].push_back( 0.0 );  // Time

    return loadState;
}

void DefaultScene::processGUI( const U64 gameDeltaTimeUS, const U64 appDeltaTimeUS )
{
    constexpr D64 FpsDisplay = Time::SecondsToMilliseconds( 0.5 );
    constexpr D64 TimeDisplay = Time::SecondsToMilliseconds( 1.0 );

    if ( _guiTimersMS[to_base( TimerClass::APP_TIME )][0] >= FpsDisplay )
    {
        _GUI->modifyText( "RenderBinCount",
                          Util::StringFormat( "Number of items in Render Bin: %d.",
                                              _context.kernel().renderPassManager()->getLastTotalBinSize( RenderStage::DISPLAY ) ), false );
        _guiTimersMS[to_base( TimerClass::APP_TIME )][0] = 0.0;
    }

    if ( _guiTimersMS[to_base( TimerClass::APP_TIME )][1] >= TimeDisplay )
    {
        _GUI->modifyText( "timeDisplay",
                          Util::StringFormat( "Elapsed time: %5.0f", Time::Game::ElapsedSeconds() ), false );
        _guiTimersMS[to_base( TimerClass::APP_TIME )][1] = 0.0;
    }

    Scene::processGUI(gameDeltaTimeUS, appDeltaTimeUS);
}

void DefaultScene::postLoadMainThread()
{
    _GUI->addText( "timeDisplay", pixelPosition( 60, 80 ), Font::DIVIDE_DEFAULT,
                   UColour4( 64, 64, 355, 255 ),
                   Util::StringFormat( "Elapsed time: %5.0f", Time::Game::ElapsedSeconds() ) );
    _GUI->addText( "RenderBinCount", pixelPosition( 60, 135 ), Font::BATANG,
                   UColour4( 164, 32, 32, 255 ),
                   Util::StringFormat( "Number of items in Render Bin: %d", 0 ) );
    Scene::postLoadMainThread();
}

}
