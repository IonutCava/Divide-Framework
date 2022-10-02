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

#include "Core/Headers/Application.h"
#include "Platform/Headers/DisplayWindow.h"

namespace Divide {
DefaultScene::DefaultScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name)
    : Scene(context, cache, parent, name)
{
    Scene::DEFAULT_SCENE_GUID = getGUID();
}

bool DefaultScene::load() {
    const bool loadState = Scene::load();
    state()->saveLoadDisabled(true);
    return loadState;
}

}
