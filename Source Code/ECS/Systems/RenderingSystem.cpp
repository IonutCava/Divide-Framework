#include "stdafx.h"

#include "Headers/RenderingSystem.h"

#include "Geometry/Material/Headers/Material.h"

namespace Divide {
    RenderingSystem::RenderingSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    RenderingSystem::~RenderingSystem()
    {
    }

    void RenderingSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);

        for (RenderingComponent* comp : _componentCache) {
            if (comp->parentSGN()->getNode().rebuildDrawCommands()) {
                comp->rebuildRenderPackages(true);
                comp->parentSGN()->getNode().rebuildDrawCommands(false);
            }
        }
    }

    void RenderingSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        Parent::Update(dt);

        const U64 microSec = Time::MillisecondsToMicroseconds(dt);

        for (RenderingComponent* comp : _componentCache) {
            if (comp->_materialInstance != nullptr && comp->_materialInstance->update(microSec)) {
                comp->onMaterialChanged();
            }

        }
    }

    void RenderingSystem::PostUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PostUpdate(dt);

        for (RenderingComponent* comp : _componentCache) {
            if (comp->rebuildRenderPackages()) {
                comp->rebuildRenderPackages(false);
            }
        }
    }

    void RenderingSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void RenderingSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    bool RenderingSystem::saveCache(const SceneGraphNode* sgn, ByteBuffer& outputBuffer) {
        if (Parent::saveCache(sgn, outputBuffer)) {
            const RenderingComponent* rComp = sgn->GetComponent<RenderingComponent>();
            if (rComp != nullptr && !rComp->saveCache(outputBuffer)) {
                return false;
            }
            return true;
        }

        return false;
    }

    bool RenderingSystem::loadCache(SceneGraphNode* sgn, ByteBuffer& inputBuffer) {
        if (Parent::loadCache(sgn, inputBuffer)) {
            RenderingComponent* rComp = sgn->GetComponent<RenderingComponent>();
            if (rComp != nullptr && !rComp->loadCache(inputBuffer)) {
                return false;
            }
            return true;
        }

        return false;
    }
} //namespace Divide