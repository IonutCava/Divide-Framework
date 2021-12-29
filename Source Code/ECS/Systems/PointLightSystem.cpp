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
                IMPrimitive::SphereDescriptor descriptor;
                descriptor.center = comp->positionCache();

                descriptor.radius = 0.5f;
                descriptor.colour = Util::ToByteColour(comp->getDiffuseColour());
                context().gfx().debugDrawSphere(comp->getGUID() + 0, descriptor);

                descriptor.radius = comp->range();
                descriptor.colour = DefaultColours::GREEN_U8;
                context().gfx().debugDrawSphere(comp->getGUID() + 1, descriptor);
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
