

#include "Headers/DefaultScene.h"

#include "GUI/Headers/GUIButton.h"

#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/ProjectManager.h"
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


namespace Divide 
{


DefaultScene::DefaultScene(PlatformContext& context, ResourceCache& cache, Project& parent, const SceneEntry& entry)
    : Scene(context, cache, parent, entry)
{
    Scene::DEFAULT_SCENE_GUID = getGUID();
}

bool DefaultScene::load() {
    const bool loadState = Scene::load();
    state()->saveLoadDisabled(true);

    addGuiTimer(TimerClass::APP_TIME,
                 Time::SecondsToMicroseconds( 0.25),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                     _GUI->modifyText( "RenderBinCount",
                                       Util::StringFormat( "Number of items in Render Bin: {}.", _context.kernel().renderPassManager()->getLastTotalBinSize( RenderStage::DISPLAY ) ),
                                       false );
                 });

    addGuiTimer( TimerClass::APP_TIME,
                 Time::SecondsToMicroseconds( 1.0 ),
                 [this]( const U64 elapsedTimeUS )
                 {
                     _GUI->modifyText( "timeDisplay",
                                      Util::StringFormat( "Elapsed time: {:5.0f}", Time::MicrosecondsToSeconds(elapsedTimeUS)),
                                      false);
                 });

    return loadState;
}

void DefaultScene::postLoadMainThread()
{
    _GUI->addText( "timeDisplay",
                   pixelPosition( 60, 80 ),
                   Font::DIVIDE_DEFAULT,
                   UColour4( 64, 64, 355, 255 ),
                   "Elapsed time: 0.0f" );

    _GUI->addText( "RenderBinCount",
                   pixelPosition( 60, 135 ),
                   Font::BATANG,
                   UColour4( 164, 32, 32, 255 ),
                   "Number of items in Render Bin: 0" );
    Scene::postLoadMainThread();
}

}
