#include "stdafx.h"

#include "Headers/BoundsSystem.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide {
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

        for (BoundsComponent* bComp : _componentCache) {
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode())) {
                bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), false);

                SceneGraphNode* parent = bComp->parentSGN()->parent();
                while (parent != nullptr) {
                    Attorney::SceneNodeBoundsSystem::setBoundsChanged(parent->getNode());
                    parent = parent->parent();
                }
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

        for (BoundsComponent* bComp : _componentCache) {
            bComp->updateBoundingBoxTransform();
        }

        for (BoundsComponent* bComp : _componentCache) {
            bComp->appendChildBBs();
        }

        for (BoundsComponent* bComp : _componentCache) {
            Attorney::SceneNodeBoundsSystem::clearBoundsChanged(bComp->parentSGN()->getNode());
            bComp->parentSGN()->SendEvent(
            {
                ECS::CustomEvent::Type::BoundsUpdated,
                bComp,
            });
        }
    }

} //namespace Divide