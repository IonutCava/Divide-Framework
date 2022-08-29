#include "stdafx.h"

#include "Headers/EnvironmentProbeSystem.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    EnvironmentProbeSystem::EnvironmentProbeSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    EnvironmentProbeSystem::~EnvironmentProbeSystem()
    {
    }

    void EnvironmentProbeSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);

        for (EnvironmentProbeComponent* comp : _componentCache) {
            if (comp->_drawImpostor || comp->showParallaxAABB()) {
                const BoundingBox& aabb = comp->_aabb;
                {
                    IM::SphereDescriptor descriptor;
                    descriptor.center = aabb.getCenter();
                    descriptor.radius = 0.5f;
                    descriptor.colour = DefaultColours::BLUE_U8;
                    context().gfx().debugDrawSphere(comp->getGUID() + 0, descriptor);
                }
                {
                    IM::BoxDescriptor descriptor;
                    descriptor.min = aabb.getMin();
                    descriptor.max = aabb.getMax();
                    descriptor.colour = DefaultColours::BLUE_U8;
                    context().gfx().debugDrawBox(comp->getGUID() + 1, descriptor);
                }
            }

            switch (comp->_updateType) {
                case EnvironmentProbeComponent::UpdateType::ALWAYS:
                    if (comp->_queueRefresh) {
                        comp->dirty(true);
                    }
                    break;
                case EnvironmentProbeComponent::UpdateType::ON_RATE:
                    if (comp->_queueRefresh) {
                        comp->dirty(++comp->_currentUpdateCall % comp->_updateRate == 0);
                    }
                    break;
                case EnvironmentProbeComponent::UpdateType::ONCE:
                case EnvironmentProbeComponent::UpdateType::ON_DIRTY:
                    break;//Nothing needed
                case EnvironmentProbeComponent::UpdateType::COUNT:
                    DIVIDE_UNEXPECTED_CALL();
                    break;
            }
        }
    }

    void EnvironmentProbeSystem::Update(const F32 dt) {
        Parent::Update(dt);
    }

    void EnvironmentProbeSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
    }

    void EnvironmentProbeSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void EnvironmentProbeSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool EnvironmentProbeSystem::saveCache(const SceneGraphNode * sgn, ByteBuffer & outputBuffer) {
        return Parent::saveCache(sgn, outputBuffer);
    }

    bool EnvironmentProbeSystem::loadCache(SceneGraphNode * sgn, ByteBuffer & inputBuffer) {
        return Parent::loadCache(sgn, inputBuffer);
    }
} //namespace Divide