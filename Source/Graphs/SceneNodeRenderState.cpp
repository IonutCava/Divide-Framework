// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/SceneNodeRenderState.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Geometry/Material/Headers/Material.h"

namespace Divide {

bool SceneNodeRenderState::drawState(const RenderStagePass stagePass, const bool defaultState, const SceneNodeRenderState* source) const
{
    if (!_drawState || (source != nullptr && !source->drawState()))
    {
        return false;
    }
    if (const std::optional<bool> overrideState = drawStateOverride(stagePass); overrideState.has_value())
    {
        return *overrideState;
    }
    return defaultState && (source == nullptr || source->drawState(stagePass));
}

std::optional<bool> SceneNodeRenderState::drawStateOverride(const RenderStagePass stagePass) const
{
    const auto checkIndex = [&stagePass](const RenderStagePass exclusion) noexcept {
        const bool mainIndexMatch = exclusion._index == g_AllIndicesID || //All Passes
                                    exclusion._index == stagePass._index;//Same pass

        const bool subIndexMatch = exclusion._pass == RenderStagePass::PassIndex::COUNT ||
            exclusion._pass == stagePass._pass; //Sub pass index 2 match or all index 2 sub pass indices

        return mainIndexMatch && subIndexMatch;
    };

    std::optional<bool> result;
    U8 bestSpecificity = 0u;
    for (auto it = _drawMask.rbegin(); it != _drawMask.rend(); ++it)
    {
        const RenderStagePass exclusion = it->_stagePass;
        if (checkIndex(exclusion) &&
            (exclusion._variant == RenderStagePass::VariantType::COUNT || exclusion._variant == stagePass._variant) &&
            (exclusion._stage == RenderStage::COUNT || exclusion._stage == stagePass._stage) &&
            (exclusion._passType == RenderPassType::COUNT || exclusion._passType == stagePass._passType))
        {
            const U8 specificity = to_U8((exclusion._stage != RenderStage::COUNT) +
                                        (exclusion._passType != RenderPassType::COUNT) +
                                        (exclusion._variant != RenderStagePass::VariantType::COUNT) +
                                        (exclusion._index != g_AllIndicesID) +
                                        (exclusion._pass != RenderStagePass::PassIndex::COUNT));
            if (!result.has_value() || specificity > bestSpecificity)
            {
                result = it->_draw;
                bestSpecificity = specificity;
            }
        }
    }
    return result;
}

void SceneNodeRenderState::setDrawStateOverride(const RenderStagePass stagePass, const bool state)
{
    for (auto& entry : _drawMask)
    {
        if (entry._stagePass == stagePass)
        {
            if (entry._draw != state)
            {
                entry._draw = state;
                ++_coverageRevision;
            }
            return;
        }
    }
    _drawMask.push_back({stagePass, state});
    ++_coverageRevision;
}

void SceneNodeRenderState::clearDrawStateOverride(const RenderStagePass stagePass)
{
    if (erase_if(_drawMask, [stagePass](const DrawMaskEntry& entry) { return entry._stagePass == stagePass; }) > 0u)
    {
        ++_coverageRevision;
    }
}

void SceneNodeRenderState::addToDrawExclusionMask(const RenderStage stage, const RenderPassType passType, const RenderStagePass::VariantType variant, const U16 index, const RenderStagePass::PassIndex pass)
{
    const RenderStagePass stagePass{ 
        stage,
        passType,
        index,
        variant,
        pass
    };

    setDrawStateOverride(stagePass, false);
}

} //namespace Divide
