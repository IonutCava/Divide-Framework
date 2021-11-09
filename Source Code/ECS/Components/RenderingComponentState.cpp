#include "stdafx.h"

#include "Headers/RenderingComponent.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Core/Headers/Configuration.h"
#include "Geometry/Material/Headers/Material.h"

namespace Divide {

void RenderingComponent::toggleRenderOption(RenderOptions option, bool state, bool recursive) {
    if (renderOptionEnabled(option) != state) {
        if (recursive) {
            _parentSGN->forEachChild([option, state, recursive](const SceneGraphNode* child, I32 /*childIdx*/) {
                RenderingComponent* const renderable = child->get<RenderingComponent>();
                if (renderable) {
                    renderable->toggleRenderOption(option, state, recursive);
                }
                return true;
            });
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
    return renderOptionsEnabled(to_U32(option));
}

bool RenderingComponent::renderOptionsEnabled(const U32 mask) const noexcept {
    return BitCompare(_renderMask, mask);
}

void RenderingComponent::toggleBoundsDraw(bool showAABB, bool showBS, bool recursive) {
    if (recursive) {
        _parentSGN->forEachChild([showAABB, showBS, recursive](const SceneGraphNode* child, I32 /*childIdx*/) {
            RenderingComponent* const renderable = child->get<RenderingComponent>();
            if (renderable) {
                renderable->toggleBoundsDraw(showAABB, showBS, recursive);
            }
            return true;
        });
    }
    _drawAABB = showAABB;
    _drawBS = showBS;
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