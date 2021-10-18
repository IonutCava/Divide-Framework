#include "stdafx.h"

#include "Headers/DefaultScene.h"

#include "GUI/Headers/GUIButton.h"

#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"

#include "Dynamics/Entities/Units/Headers/Player.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"

#include <CEGUI/CEGUI.h>


#include "Core/Headers/Application.h"
#include "Platform/Headers/DisplayWindow.h"

namespace Divide {
DefaultScene::DefaultScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name)
    : Scene(context, cache, parent, name)
{
    Scene::DEFAULT_SCENE_GUID = getGUID();
}

bool DefaultScene::load(const Str256& name) {
    const bool loadState = Scene::load(name);
    state()->saveLoadDisabled(true);
    return loadState;
}

void DefaultScene::processGUI(const U64 deltaTimeUS) {
    Scene::processGUI(deltaTimeUS);
}

void DefaultScene::postLoadMainThread() {
    Scene::postLoadMainThread();
}

void DefaultScene::processInput(const PlayerIndex idx, const U64 deltaTimeUS) {
    Scene::processInput(idx, deltaTimeUS);
}

void DefaultScene::processTasks(const U64 deltaTimeUS) {
    Scene::processTasks(deltaTimeUS);
}

void DefaultScene::onSetActive() {
    Scene::onSetActive();
}
}
