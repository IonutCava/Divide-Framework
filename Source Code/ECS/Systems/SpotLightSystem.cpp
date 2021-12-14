#include "stdafx.h"

#include "Headers/SpotLightSystem.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    SpotLightSystem::SpotLightSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    SpotLightSystem::~SpotLightSystem()
    {
    }

    void SpotLightSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);

        for (SpotLightComponent* comp : _componentCache) {
            if (comp->_drawImpostor) {
               context().gfx().debugDrawCone(
                   comp->getGUID(),
                   comp->positionCache(),
                   comp->directionCache(),
                   comp->range(),
                   comp->outerConeRadius(),
                   comp->getDiffuseColour());
            }
        }
    }

    void SpotLightSystem::Update(const F32 dt) {
        Parent::Update(dt);
    }

    void SpotLightSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void SpotLightSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void SpotLightSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool SpotLightSystem::saveCache(const SceneGraphNode * sgn, ByteBuffer & outputBuffer) {
        return Parent::saveCache(sgn, outputBuffer);
    }

    bool SpotLightSystem::loadCache(SceneGraphNode * sgn, ByteBuffer & inputBuffer) {
        return Parent::loadCache(sgn, inputBuffer);
    }
} //namespace Divide
