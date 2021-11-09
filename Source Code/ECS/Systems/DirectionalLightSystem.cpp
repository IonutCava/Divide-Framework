#include "stdafx.h"

#include "Headers/DirectionalLightSystem.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/SceneManager.h"

namespace Divide {
    DirectionalLightSystem::DirectionalLightSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    DirectionalLightSystem::~DirectionalLightSystem()
    {
    }

    void DirectionalLightSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        const Camera* playerCam = Attorney::SceneManagerCameraAccessor::playerCamera(context().kernel().sceneManager());
        Parent::PreUpdate(dt);
        for (DirectionalLightComponent* comp : _componentCache) {
            if (comp->drawImpostor() || comp->showDirectionCone()) {
                constexpr F32 coneDist = 11.f;
                // Try and place the cone in such a way that it's always in view, because directional lights have no "source"
                const vec3<F32> min = -coneDist * comp->directionCache() +
                                      playerCam->getEye() +
                                      playerCam->getForwardDir() * 10.0f +
                                      playerCam->getRightDir() * 2.0f;

                context().gfx().debugDrawCone(min, comp->directionCache(), coneDist, 1.f, comp->getDiffuseColour());
            }
        }
    }

    void DirectionalLightSystem::Update(const F32 dt) {
        Parent::Update(dt);
    }

    void DirectionalLightSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void DirectionalLightSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void DirectionalLightSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool DirectionalLightSystem::saveCache(const SceneGraphNode * sgn, ByteBuffer & outputBuffer) {
        return Parent::saveCache(sgn, outputBuffer);
    }

    bool DirectionalLightSystem::loadCache(SceneGraphNode * sgn, ByteBuffer & inputBuffer) {
        return Parent::loadCache(sgn, inputBuffer);
    }
} //namespace Divide
