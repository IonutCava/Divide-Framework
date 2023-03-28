#include "stdafx.h"

#include "Headers/RenderingComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Core/Headers/Configuration.h"
#include "Geometry/Material/Headers/Material.h"

namespace Divide {

void RenderingComponent::toggleRenderOption(RenderOptions option, bool state, bool recursive) {
    if (renderOptionEnabled(option) != state) {
        if (recursive) {
            const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
            SharedLock<SharedMutex> w_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i) {
                if (children._data[i]->HasComponents(ComponentType::RENDERING)) {
                    children._data[i]->get<RenderingComponent>()->toggleRenderOption(option, state, recursive);
                }
            }
        }

        state ? _renderMask |= to_U32(option) : _renderMask &= ~to_U32(option);

        onRenderOptionChanged(option, state);
    }
}

bool RenderingComponent::renderOptionEnabled(const RenderOptions option) const noexcept {
    return _renderMask & to_base(option);
}

void RenderingComponent::toggleBoundsDraw(const bool showAABB, const bool showBS, const bool showOBB, bool recursive) {
    if (recursive) {
        const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
        SharedLock<SharedMutex> w_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            if (children._data[i]->HasComponents(ComponentType::RENDERING)) {
                children._data[i]->get<RenderingComponent>()->toggleBoundsDraw(showAABB, showBS, showOBB, recursive);
            }
        }
    }
    _drawAABB = showAABB;
    _drawBS = showBS;
    _drawOBB = showOBB;
}

void RenderingComponent::onRenderOptionChanged(const RenderOptions option, const bool state) {
    switch (option) {
      case RenderOptions::RECEIVE_SHADOWS:
      {
          if (state && !_config.rendering.shadowMapping.enabled) {
              toggleRenderOption(RenderOptions::RECEIVE_SHADOWS, false);
              return;
          }
          if (_materialInstance != nullptr) {
              _materialInstance->properties().receivesShadows(state);
          }
      } break;
      default: break;
    }
}
} //namespace Divide