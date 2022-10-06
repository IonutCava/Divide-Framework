#include "stdafx.h"

#include "Headers/SceneNodeRenderState.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Geometry/Material/Headers/Material.h"

namespace Divide {

bool SceneNodeRenderState::drawState(const RenderStagePass stagePass) const {
    PROFILE_SCOPE();

    if (!_drawState) {
        return false;
    }

    const auto checkIndex = [&stagePass](const RenderStagePass exclusion) noexcept {
        const bool mainIndexMatch = exclusion._index == g_AllIndicesID || //All Passes
                                    exclusion._index == stagePass._index;//Same pass

        const bool subIndexMatch = exclusion._pass == RenderStagePass::PassIndex::COUNT ||
            exclusion._pass == stagePass._pass; //Sub pass index 2 match or all index 2 sub pass indices

        return mainIndexMatch || subIndexMatch;
    };

    for (const RenderStagePass exclussionStagePass : _exclusionStagePasses) {
        if (checkIndex(exclussionStagePass) && 
            (exclussionStagePass._variant == RenderStagePass::VariantType::COUNT || exclussionStagePass._variant == stagePass._variant) &&
            (exclussionStagePass._stage == RenderStage::COUNT || exclussionStagePass._stage == stagePass._stage) &&
            (exclussionStagePass._passType == RenderPassType::COUNT || exclussionStagePass._passType == stagePass._passType))
        {
            return false;
        }
    }

    return true;
}

void SceneNodeRenderState::addToDrawExclusionMask(const RenderStage stage, const RenderPassType passType, const RenderStagePass::VariantType variant, const U16 index, const RenderStagePass::PassIndex pass) {
    const RenderStagePass stagePass{ 
        stage,
        passType,
        index,
        variant,
        pass
    };

    if (eastl::find(cbegin(_exclusionStagePasses), cend(_exclusionStagePasses), stagePass) == cend(_exclusionStagePasses)) {
        _exclusionStagePasses.emplace_back(stagePass);
    }
}

};