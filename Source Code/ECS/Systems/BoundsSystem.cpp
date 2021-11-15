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

        const U32 compCount = to_U32(_componentCache.size());
        if (compCount > g_parallelPartitionSize * 2) {
            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = compCount;
            descriptor._partitionSize = g_parallelPartitionSize;
            descriptor._cbk = [this](const Task*, const U32 start, const U32 end) {
                for (U32 i = start; i < end; ++i) {
                    BoundsComponent* bComp = _componentCache[i];
                    if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode())) {
                        bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), false);
                        OnBoundsChanged(bComp->parentSGN());
                    }
                }
            };

            parallel_for(_context, descriptor);
        } else {
            for (BoundsComponent* bComp : _componentCache) {
                if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode())) {
                    bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), false);
                    OnBoundsChanged(bComp->parentSGN());
                }
            }
        }
    }

    void BoundsSystem::Update(const F32 dt) {
        OPTICK_EVENT();

        Parent::Update(dt);

        const U32 compCount = to_U32(_componentCache.size());
        if (compCount > g_parallelPartitionSize * 2) {
            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = compCount;
            descriptor._partitionSize = g_parallelPartitionSize;
            descriptor._cbk = [this](const Task*, const U32 start, const U32 end) {
                for (U32 i = start; i < end; ++i) {
                    BoundsComponent* bComp = _componentCache[i];
                    const SceneNode& sceneNode = bComp->parentSGN()->getNode();
                    if (Attorney::SceneNodeBoundsSystem::boundsChanged(sceneNode)) {
                        bComp->setRefBoundingBox(sceneNode.getBounds());
                    }
                }
            };

            parallel_for(_context, descriptor);
        } else {
            for (BoundsComponent* bComp : _componentCache) {
                const SceneNode& sceneNode = bComp->parentSGN()->getNode();
                if (Attorney::SceneNodeBoundsSystem::boundsChanged(sceneNode)) {
                    bComp->setRefBoundingBox(sceneNode.getBounds());
                }
            }
        }
    }

    void BoundsSystem::PostUpdate(const F32 dt) {
        OPTICK_EVENT();

        Parent::PostUpdate(dt);

        const U32 compCount = to_U32(_componentCache.size());
        if (compCount > g_parallelPartitionSize * 2) {
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

    void BoundsSystem::OnFrameStart() {
        Parent::OnFrameStart();
    }

    void BoundsSystem::OnFrameEnd() {
        Parent::OnFrameEnd();
    }

    void BoundsSystem::OnBoundsChanged(const SceneGraphNode* sgn) {
        SceneGraphNode* parent = sgn->parent();
        if (parent != nullptr) {
            Attorney::SceneNodeBoundsSystem::setBoundsChanged(parent->getNode());
            OnBoundsChanged(parent);
        }
    }
} //namespace Divide