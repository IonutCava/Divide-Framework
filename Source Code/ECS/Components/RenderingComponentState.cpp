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
                RenderingComponent* const renderable = children._data[i]->get<RenderingComponent>();
                if (renderable) {
                    renderable->toggleRenderOption(option, state, recursive);
                }
            }
        }

        if (state) {
            SetBit(_renderMask, to_U32(option));
        } else {
            ClearBit(_renderMask, to_U32(option));
        }
        onRenderOptionChanged(option, state);
    }
}

bool RenderingComponent::renderOptionEnabled(const RenderOptions option) const noexcept {
    return BitCompare(_renderMask, option);
}

bool RenderingComponent::renderOptionsEnabledALL(const U32 mask) const noexcept {
    return BitCompare(_renderMask, mask);
}

bool RenderingComponent::renderOptionsEnabledANY(const U32 mask) const noexcept {
    return AnyCompare(_renderMask, mask);
}

void RenderingComponent::toggleBoundsDraw(const bool showAABB, const bool showBS, const bool showOBB, bool recursive) {
    if (recursive) {
        const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
        SharedLock<SharedMutex> w_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            RenderingComponent* const renderable = children._data[i]->get<RenderingComponent>();
            if (renderable) {
                renderable->toggleBoundsDraw(showAABB, showBS, showOBB, recursive);
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
              _materialInstance->receivesShadows(state);
          }
      } break;
      default: break;
    }
}
} //namespace Divide