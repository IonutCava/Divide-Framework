#include "stdafx.h"

#include "Headers/PointLightSystem.h"

#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    PointLightSystem::PointLightSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    PointLightSystem::~PointLightSystem()
    {
    }

    void PointLightSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);
        for (PointLightComponent* comp : _componentCache) {
            if (comp->_drawImpostor) {
                context().gfx().debugDrawSphere(comp->getGUID(), comp->positionCache(), 0.5f, comp->getDiffuseColour());
                context().gfx().debugDrawSphere(comp->getGUID(), comp->positionCache(), comp->range(), DefaultColours::GREEN);
            }
        }
    }

    void PointLightSystem::Update(const F32 dt) {
        Parent::Update(dt);
    }

    void PointLightSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void PointLightSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void PointLightSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool PointLightSystem::saveCache(const SceneGraphNode * sgn, ByteBuffer & outputBuffer) {
        return Parent::saveCache(sgn, outputBuffer);
    }

    bool PointLightSystem::loadCache(SceneGraphNode * sgn, ByteBuffer & inputBuffer) {
        return Parent::loadCache(sgn, inputBuffer);
    }
} //namespace Divide
