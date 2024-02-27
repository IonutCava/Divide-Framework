

#include "Headers/DirectionalLightSystem.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/IMPrimitive.h"

namespace Divide {
    DirectionalLightSystem::DirectionalLightSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    void DirectionalLightSystem::PreUpdate(const F32 dt) {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);
        for (DirectionalLightComponent* comp : _componentCache) {
            if (comp->drawImpostor() || comp->showDirectionCone()) {
                IM::ConeDescriptor descriptor;
                descriptor.root = -comp->directionCache() * comp->range();
                descriptor.direction = comp->directionCache();
                descriptor.length = comp->range();
                descriptor.radius = 2.f;
                descriptor.noCull = true;
                descriptor.colour = Util::ToByteColour(comp->getDiffuseColour());
                context().gfx().debugDrawCone(comp->getGUID() + 0, descriptor);
            }
        }
    }

} //namespace Divide
