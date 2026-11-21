// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/RenderingComponent.h"
#include "Headers/BoundsComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"

namespace Divide {

bool RenderingComponent::drawState(const RenderStagePass stagePass, const bool defaultState) const
{
    if (stagePass._stage == RenderStage::SHADOW &&
        !IsShadowCaster(_parentSGN->getNode().type(), renderOptionEnabled(RenderOptions::CAST_SHADOWS)))
    {
        return false;
    }
    const SceneNodeRenderState& source = _parentSGN->getNode().renderState();
    return _drawStateOverrides.drawState(stagePass, defaultState, &source);
}

std::optional<bool> RenderingComponent::drawStateOverride(const RenderStagePass stagePass) const
{
    return _drawStateOverrides.drawStateOverride(stagePass);
}

void RenderingComponent::setDrawStateOverride(const RenderStagePass stagePass, const bool state)
{
    const U64 revision = _drawStateOverrides.coverageRevision();
    _drawStateOverrides.setDrawStateOverride(stagePass, state);
    if (revision != _drawStateOverrides.coverageRevision())
    {
        _parentSGN->sceneGraph()->invalidateWorldAO();
        if (const BoundsComponent* bounds = _parentSGN->get<BoundsComponent>(); bounds != nullptr)
        {
            _parentSGN->sceneGraph()->invalidateShadowBounds(*_parentSGN, bounds->getBoundingSphere(),
                _parentSGN->usageContext() == NodeUsageContext::NODE_STATIC);
        }
    }
}

void RenderingComponent::clearDrawStateOverride(const RenderStagePass stagePass)
{
    const U64 revision = _drawStateOverrides.coverageRevision();
    _drawStateOverrides.clearDrawStateOverride(stagePass);
    if (revision != _drawStateOverrides.coverageRevision())
    {
        _parentSGN->sceneGraph()->invalidateWorldAO();
        if (const BoundsComponent* bounds = _parentSGN->get<BoundsComponent>(); bounds != nullptr)
        {
            _parentSGN->sceneGraph()->invalidateShadowBounds(*_parentSGN, bounds->getBoundingSphere(),
                _parentSGN->usageContext() == NodeUsageContext::NODE_STATIC);
        }
    }
}

bool RenderingComponent::hasTransparency() const
{
    return _materialInstance != INVALID_HANDLE<Material> && Get(_materialInstance)->hasTransparency();
}

bool RenderingComponent::worldAOContributor() const
{
    const SceneNodeType type = _parentSGN->getNode().type();
    if (_parentSGN->hasFlag(SceneGraphNode::Flags::LOADING) ||
        !_parentSGN->hasFlag(SceneGraphNode::Flags::ACTIVE) ||
        !renderOptionEnabled(RenderOptions::IS_VISIBLE) ||
        !IsShadowCaster(type, renderOptionEnabled(RenderOptions::CAST_SHADOWS)))
    {
        return false;
    }
    bool defaultState = (IsPrimitive(type) || type == SceneNodeType::TYPE_SUBMESH ||
                         type == SceneNodeType::TYPE_TERRAIN || type == SceneNodeType::TYPE_VEGETATION) &&
                        !_parentSGN->hasFlag(SceneGraphNode::Flags::IS_CONTAINER) &&
                        !RenderPassCuller::CullGeometryCategory(ShadowMap::WorldAOCullFlags(),
                            type == SceneNodeType::TYPE_VEGETATION || _parentSGN->hasFlag(SceneGraphNode::Flags::VEGETATION_TREE),
                            _parentSGN->HasComponents(ComponentType::ANIMATION), type == SceneNodeType::TYPE_TERRAIN,
                            hasTransparency());
    if (defaultState)
    {
        for (const SceneGraphNode* parent = _parentSGN->parent(); parent != nullptr; parent = parent->parent())
        {
            if (parent->getNode().type() == SceneNodeType::TYPE_SKY)
            {
                defaultState = false;
                break;
            }
        }
    }
    return drawState(ShadowMap::WorldAOStagePass(), defaultState);
}

void RenderingComponent::toggleRenderOption(RenderOptions option, bool state, bool recursive) {
    if (renderOptionEnabled(option) != state)
    {
        if (recursive)
        {
            const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i)
            {
                if (children._data[i]->HasComponents(ComponentType::RENDERING))
                {
                    children._data[i]->get<RenderingComponent>()->toggleRenderOption(option, state, recursive);
                }
            }
        }

        const bool previouslyCastShadows = renderOptionEnabled(RenderOptions::CAST_SHADOWS);
        state ? _renderMask |= to_U32(option) : _renderMask &= ~to_U32(option);

        if (option == RenderOptions::CAST_SHADOWS || option == RenderOptions::IS_VISIBLE)
        {
            const BoundsComponent* bounds = _parentSGN->get<BoundsComponent>();
            if (bounds != nullptr)
            {
                _parentSGN->sceneGraph()->invalidateShadowBounds(
                    *_parentSGN, bounds->getBoundingSphere(),
                    _parentSGN->usageContext() == NodeUsageContext::NODE_STATIC, previouslyCastShadows);
            }
        }

        onRenderOptionChanged(option, state);
    }
}

bool RenderingComponent::renderOptionEnabled(const RenderOptions option) const noexcept
{
    return _renderMask & to_base(option);
}

void RenderingComponent::onRenderOptionChanged(const RenderOptions option, const bool state)
{
    if ( option == RenderOptions::CAST_SHADOWS || option == RenderOptions::IS_VISIBLE )
    {
        _parentSGN->sceneGraph()->invalidateWorldAO();
    }
    switch (option)
    {
      case RenderOptions::RECEIVE_SHADOWS:
      {
          if (state && !_config.rendering.shadowMapping.enabled) {
              toggleRenderOption(RenderOptions::RECEIVE_SHADOWS, false);
              return;
          }
          if (_materialInstance != INVALID_HANDLE<Material>)
          {
              Get(_materialInstance)->properties().receivesShadows(state);
          }
      } break;
      default: break;
    }
}
} //namespace Divide
