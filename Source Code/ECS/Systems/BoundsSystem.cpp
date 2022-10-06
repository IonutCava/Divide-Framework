#include "stdafx.h"

#include "Headers/BoundsSystem.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"

#include "Graphs/Headers/SceneNode.h"

// For debug rendering
#include "Platform/Video/Headers/GFXDevice.h"

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
        PROFILE_SCOPE();

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
        PROFILE_SCOPE();

        Parent::Update(dt);
        // This is fast and does not need to run in parallel
        for (BoundsComponent* bComp : _componentCache) {
            const SceneNode& sceneNode = bComp->parentSGN()->getNode();
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(sceneNode)) {
                bComp->setRefBoundingBox(sceneNode.getBounds());
            }
        }
        for (BoundsComponent* bComp : _componentCache) {
            if (Attorney::SceneNodeBoundsSystem::boundsChanged(bComp->parentSGN()->getNode())) {
                bComp->appendChildRefBBs();
            }
        }
    }

    void BoundsSystem::PostUpdate(const F32 dt) {
        PROFILE_SCOPE();

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
        for (BoundsComponent* bComp : _componentCache) {
            if (!bComp->showAABB()) {
                continue;
            }

            const BoundingBox& bb = bComp->getBoundingBox();
            IM::BoxDescriptor descriptor;
            descriptor.min = bb.getMin();
            descriptor.max = bb.getMax();
            descriptor.colour = DefaultColours::WHITE_U8;
            context().gfx().debugDrawBox(bComp->getGUID() + 0, descriptor);
        }
    }

} //namespace Divide