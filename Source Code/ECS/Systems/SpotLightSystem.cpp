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
