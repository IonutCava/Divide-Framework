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
        Parent::PreUpdate(dt);
    }

    void RenderingSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        Parent::Update(dt);

        const U64 microSec = Time::MillisecondsToMicroseconds(dt);

        for (RenderingComponent* comp : _componentCache) {
            if (comp->_materialInstance != nullptr && comp->_materialInstance->update(microSec)) {
                comp->onMaterialChanged();
            }

            if (comp->parentSGN()->getNode().rebuildDrawCommands()) {
                for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
                    RenderingComponent::FlagsPerPassType& perPassFlags = comp->_rebuildDrawCommandsFlags[s];
                    for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
                        RenderingComponent::FlagsPerIndex& perIndexFlags = perPassFlags[p];
                        perIndexFlags.fill(true);
                    }
                }
                comp->parentSGN()->getNode().rebuildDrawCommands(false);
            }
        }
    }

    void RenderingSystem::PostUpdate(const F32 dt) {
        Parent::PostUpdate(dt);
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