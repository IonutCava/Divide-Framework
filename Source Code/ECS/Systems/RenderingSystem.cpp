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
            if (comp->rebuildDrawCommands() || comp->parentSGN()->getNode().rebuildDrawCommands()) {
                comp->packageUpdateState(RenderingComponent::PackageUpdateState::NeedsRefresh);
                comp->rebuildDrawCommands(false);
            }
        }
    }

    void RenderingSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        Parent::Update(dt);

        const U64 microSec = Time::MillisecondsToMicroseconds(dt);

        for (RenderingComponent* comp : _componentCache) {
            if (comp->parentSGN()->getNode().rebuildDrawCommands()) {
                comp->parentSGN()->getNode().rebuildDrawCommands(false);
            }
            if (comp->_materialInstance != nullptr) {
                const U32 materialUpdateMask = comp->_materialInstance->update(microSec);
                if (BitCompare(materialUpdateMask, Material::UpdateResult::NewShader)) {
                    comp->rebuildDrawCommands(true);
                } else if (BitCompare(materialUpdateMask, Material::UpdateResult::NewCull)) {
                    comp->packageUpdateState(RenderingComponent::PackageUpdateState::NeedsNewCull);
                }
            }
        }

        Material::Update(microSec);
    }

    void RenderingSystem::PostUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PostUpdate(dt);
    }

    void RenderingSystem::OnFrameStart() {
        Parent::OnFrameStart();

        for (RenderingComponent* comp : _componentCache) {
            if (comp->packageUpdateState() == RenderingComponent::PackageUpdateState::Processed) {
                comp->packageUpdateState(RenderingComponent::PackageUpdateState::COUNT);
            }
        }
    }

    void RenderingSystem::OnFrameEnd() {
        Parent::OnFrameEnd();

        for (RenderingComponent* comp : _componentCache) {
            if (comp->packageUpdateState() == RenderingComponent::PackageUpdateState::NeedsRefresh) {
                comp->clearDrawPackages();
                if (comp->_materialInstance) {
                    comp->_materialInstance->clearRenderStates();
                }
                comp->packageUpdateState(RenderingComponent::PackageUpdateState::Processed);
            } else if (comp->packageUpdateState() == RenderingComponent::PackageUpdateState::NeedsNewCull) {
                comp->clearDrawPackages();
                if (comp->_materialInstance) {
                    comp->_materialInstance->updateCullState();
                }
                comp->packageUpdateState(RenderingComponent::PackageUpdateState::Processed);
            }
        }
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