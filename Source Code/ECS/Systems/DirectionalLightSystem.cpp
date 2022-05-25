#include "stdafx.h"

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

    DirectionalLightSystem::~DirectionalLightSystem()
    {
    }

    void DirectionalLightSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);
        for (DirectionalLightComponent* comp : _componentCache) {
            if (comp->drawImpostor() || comp->showDirectionCone()) {
                IMPrimitive::ConeDescriptor descriptor;
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
