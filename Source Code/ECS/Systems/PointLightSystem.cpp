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

    void PointLightSystem::PreUpdate(const F32 dt) {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);
        for (PointLightComponent* comp : _componentCache) {
            if (comp->_drawImpostor) {
                IM::SphereDescriptor descriptor;
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

} //namespace Divide
