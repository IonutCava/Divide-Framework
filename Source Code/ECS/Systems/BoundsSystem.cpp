#include "stdafx.h"

#include "Headers/BoundsSystem.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide {
    namespace {
        constexpr U32 g_parallelPartitionSize = 32;
    }

    BoundsSystem::BoundsSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    BoundsSystem::~BoundsSystem()
    {
    }

    void BoundsSystem::PreUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PreUpdate(dt);
        // This is fast and does not need to run in parallel
        for (BoundsComponent* bComp : _componentCache) {
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode())) {
                bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), false);
                OnBoundsChanged(bComp->parentSGN());
            }
        }
    }

    void BoundsSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        Parent::Update(dt);
        // This is fast and does not need to run in parallel
        for (BoundsComponent* bComp : _componentCache) {
            const SceneNode& sceneNode = bComp->parentSGN()->getNode();
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(sceneNode)) {
                bComp->setRefBoundingBox(sceneNode.getBounds());
            }
        }
        
    }

    void BoundsSystem::PostUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PostUpdate(dt);

        // Updating bounding boxes could be slow so we need to make sure we have a fast parallel approach in case of many
        // update requests
        const U32 compCount = to_U32(_componentCache.size());
        U32 requiredUpdates = 0u;
        for (const BoundsComponent* bComp : _componentCache) {
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode()) ||
                !bComp->isClean())
            {
                ++requiredUpdates;
            }
        }

        if (requiredUpdates > g_parallelPartitionSize * 2) {
            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = compCount;
            descriptor._partitionSize = g_parallelPartitionSize;
            descriptor._cbk = [this](const Task*, const U32 start, const U32 end) {
                for (U32 i = start; i < end; ++i) {
                    BoundsComponent* bComp = _componentCache[i];
                    Attorney::SceneNodeBoundsSystem::clearBoundsChanged(bComp->parentSGN()->getNode());
                    bComp->updateAndGetBoundingBox();
                }
            };

            parallel_for(_context, descriptor);
        } else {
            for (BoundsComponent* bComp : _componentCache) {
                Attorney::SceneNodeBoundsSystem::clearBoundsChanged(bComp->parentSGN()->getNode());
                bComp->updateAndGetBoundingBox();
            }
        }
    }

    void BoundsSystem::OnBoundsChanged(const SceneGraphNode* sgn) {
        SceneGraphNode* parent = sgn->parent();
        if (parent != nullptr) {
            Attorney::SceneNodeBoundsSystem::setBoundsChanged(parent->getNode());
            OnBoundsChanged(parent);
        }
    }
} //namespace Divide