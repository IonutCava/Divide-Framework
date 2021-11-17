#include "stdafx.h"

#include "Headers/SceneState.h"
#include "Platform/Video/Headers/RenderStagePass.h"

namespace Divide {

SceneRenderState::SceneRenderState(Scene& parentScene) noexcept
    : SceneComponent(parentScene)
{
    enableOption(RenderOptions::RENDER_GEOMETRY);
    _lodThresholds.set(25, 45, 85, 165);
}

void SceneRenderState::renderMask(const U16 mask) {
    constexpr bool validateRenderMask = false;

    if_constexpr (validateRenderMask) {
        const auto validateMask = [mask]() -> U16 {
            U16 validMask = 0;
            for (U16 stateIt = 1; stateIt <= to_base(RenderOptions::COUNT); ++stateIt) {
              const U16 bitState = toBit(stateIt);

                if (BitCompare(mask, bitState)) {
                    SetBit(validMask, bitState);
                }
            }
            return validMask;
        };

        const U16 parsedMask = validateMask();
        DIVIDE_ASSERT(parsedMask != 0 && parsedMask == mask,
                      "SceneRenderState::renderMask error: Invalid state specified!");
        _stateMask = parsedMask;
    } else {
        _stateMask = mask;
    }
}

bool SceneRenderState::isEnabledOption(const RenderOptions option) const noexcept {
    return BitCompare(_stateMask, option);
}

void SceneRenderState::enableOption(const RenderOptions option) noexcept {
    SetBit(_stateMask, option);
}

void SceneRenderState::disableOption(const RenderOptions option) noexcept {
    ClearBit(_stateMask, option);
}

void SceneRenderState::toggleOption(const RenderOptions option) noexcept {
    toggleOption(option, !isEnabledOption(option));
}

void SceneRenderState::toggleOption(const RenderOptions option, const bool state) noexcept {
    if (state) {
        enableOption(option);
    } else {
        disableOption(option);
    }
}

vec4<U16> SceneRenderState::lodThresholds(const RenderStage stage) const noexcept {
    // Hack dumping ground. Scene specific lod management can be tweaked here to keep the components clean
    if (stage == RenderStage::REFLECTION || stage == RenderStage::REFRACTION) {
        // cancel out LoD Level 0
        return {
            0u,
            _lodThresholds.y,
            _lodThresholds.z,
            _lodThresholds.w };
    }
    if (stage == RenderStage::SHADOW) {
        return _lodThresholds * 3u;
    }

    return _lodThresholds;
}
}  // namespace Divide