// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/BoundsSystem.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Core/Headers/PlatformContext.h"

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Graphs/Headers/SceneGraphNode.h"

// For debug rendering
#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Headers/Kernel.h"
#include "Managers/Headers/ProjectManager.h"

namespace Divide
{
    BoundsSystem::BoundsSystem(ECS::ECSEngine& parentEngine, PlatformContext& context)
        : PlatformContextComponent(context),
          ECSSystem(parentEngine)
    {
    }

    BoundsSystem::~BoundsSystem()
    {
    }


    void BoundsSystem::PreUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PreUpdate(dt);

        {
            SceneRenderState& renderState = context().kernel().projectManager()->activeProject()->getActiveScene()->state()->renderState();

            _renderAABB = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_AABB);
            _renderOBB  = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_OBB);
            _renderBS   = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_BSPHERES);
        }


        for (BoundsComponent* bComp : _componentCache)
        {
            if (bComp->_boundsChanges.changed(Attorney::SceneNodeBoundsSystem::boundsChangeRevision(bComp->parentSGN()->getNode())))
            {
                bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), true);

                MarkAncestorBoundsChanged(bComp->parentSGN()->parent(),
                    [](SceneGraphNode& node) { return node.parent(); },
                    [](SceneGraphNode& node) { Attorney::SceneNodeBoundsSystem::setBoundsChanged(node.getNode()); });
            }
        }
    }

    void BoundsSystem::Update(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::Update(dt);
        // This is fast and does not need to run in parallel
        for (BoundsComponent* bComp : _componentCache)
        {
            SceneGraphNode* node = bComp->parentSGN();
            SceneNode& sceneNode = node->getNode();
            const U64 revision = Attorney::SceneNodeBoundsSystem::boundsRevision(sceneNode);
            const bool sourceChanged = sceneNode.getState() == ResourceState::RES_LOADED &&
                                       bComp->_sourceBoundsRevision != revision;
            if (sourceChanged)
            {
                bComp->_sourceBoundsRevision = revision;
                bComp->flagBoundingBoxDirty(to_U32(TransformType::ALL), true);
                Attorney::SceneNodeBoundsSystem::setBoundsChanged(sceneNode);
                MarkAncestorBoundsChanged(node->parent(),
                    [](SceneGraphNode& parent) { return parent.parent(); },
                    [](SceneGraphNode& parent) { Attorney::SceneNodeBoundsSystem::setBoundsChanged(parent.getNode()); });
                if (node->usageContext() == NodeUsageContext::NODE_STATIC &&
                    (Is3DObject(sceneNode.type()) || sceneNode.type() == SceneNodeType::TYPE_VEGETATION))
                {
                    node->sceneGraph()->invalidateStaticGeometryBounds(*node);
                }
            }
        }

        for (BoundsComponent* bComp : _componentCache)
        {
            const SceneNode& sceneNode = bComp->parentSGN()->getNode();
            bComp->_boundsChanges.capture(Attorney::SceneNodeBoundsSystem::boundsChangeRevision(sceneNode));
            if (bComp->_boundsChanges.capturedChanged())
            {
                bComp->setRefBoundingBox(sceneNode.getBounds());
            }
        }

        for (BoundsComponent* bComp : _componentCache)
        {
            if (bComp->_boundsChanges.capturedChanged())
            {
                bComp->appendChildRefBBs();
            }
        }
    }

    void BoundsSystem::PostUpdate(const F32 dt)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        Parent::PostUpdate(dt);

        for (BoundsComponent* bComp : _componentCache)
        {
            bComp->updateBoundingBoxTransform();
        }

        for (BoundsComponent* bComp : _componentCache)
        {
            bComp->appendChildBBs();
        }

        for (BoundsComponent* bComp : _componentCache)
        {
            bComp->_boundsChanges.complete();
            if (bComp->_boundsUpdate.recomputed())
            {
                bComp->parentSGN()->SendEvent(
                {
                    ._type = ECS::CustomEvent::Type::BoundsUpdated,
                    ._sourceCmp = bComp,
                });
            }
        }

        for (BoundsComponent* bComp : _componentCache)
        {
            if (_renderAABB || bComp->showAABB())
            {
                const BoundingBox& bb = bComp->getBoundingBox();
                IM::BoxDescriptor descriptor;
                descriptor.min = bb._min;
                descriptor.max = bb._max;
                descriptor.colour = DefaultColours::WHITE_U8;
                context().gfx().debugDrawBox(bComp->getGUID() + 0, descriptor);
            }

            if (_renderOBB || bComp->showOBB())
            {
                const auto& obb = bComp->getOBB();
                IM::OBBDescriptor descriptor;
                descriptor.box = obb;
                descriptor.colour = UColour4(255, 0, 128, 255);

                context().gfx().debugDrawOBB(bComp->getGUID() + 123, descriptor);
            }

            if (_renderBS || bComp->showBS())
            {
                const BoundingSphere& bs = bComp->getBoundingSphere();
                IM::SphereDescriptor descriptor;
                descriptor.center = bs._sphere.center;
                descriptor.radius = bs._sphere.radius;
                descriptor.colour = UColour4(255, 255, 0, 255);
                descriptor.slices = 16u;
                descriptor.stacks = 16u;
                context().gfx().debugDrawSphere(bComp->getGUID() + 321, descriptor);
            }
        }
    }

} //namespace Divide
