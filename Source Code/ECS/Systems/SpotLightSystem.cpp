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

    void SpotLightSystem::PreUpdate(const F32 dt) {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);

        for (SpotLightComponent* comp : _componentCache) {
            if (comp->_drawImpostor) {
                IM::ConeDescriptor descriptor;
                descriptor.root = comp->positionCache();
                descriptor.direction = comp->directionCache();
                descriptor.length = comp->range();
                descriptor.radius = comp->outerConeRadius();
                descriptor.colour = Util::ToByteColour(comp->getDiffuseColour());
                descriptor.noCull = true;

                context().gfx().debugDrawCone(comp->getGUID() + 0, descriptor);
            }
        }
    }

} //namespace Divide
