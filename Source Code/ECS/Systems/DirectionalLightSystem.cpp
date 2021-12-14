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

        Parent::PreUpdate(dt);
        for (DirectionalLightComponent* comp : _componentCache) {
            if (comp->drawImpostor() || comp->showDirectionCone()) {
                context().gfx().debugDrawCone(comp->getGUID(),
                                              -comp->directionCache() * comp->range(),
                                               comp->directionCache(), 
                                               comp->range(),
                                               2.f,
                                               comp->getDiffuseColour());
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
