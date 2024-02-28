#pragma once
#ifndef _SCENE_NODE_RENDER_STATE_H_
#define _SCENE_NODE_RENDER_STATE_H_

#include "Platform/Video/Headers/RenderStagePass.h"

namespace Divide {

struct RenderStagePass;

struct SceneNodeRenderState {

    [[nodiscard]] bool drawState(RenderStagePass stagePass) const;
    void addToDrawExclusionMask(RenderStage stage,
                                RenderPassType passType = RenderPassType::COUNT,
                                RenderStagePass::VariantType variant = RenderStagePass::VariantType::COUNT,
                                U16 index = g_AllIndicesID,
                                RenderStagePass::PassIndex pass = RenderStagePass::PassIndex::PASS_0);

    PROPERTY_RW(bool, drawState, true);

    // If set to false, the closest AABB point to the camera will be used for LoD calculations
    PROPERTY_RW(bool, useBoundsCenterForLoD, true);
    // If set to true, we will always use LoD 0 for nodes who's AABB contain the camera (e.g. vegetation, particles, etc)
    PROPERTY_RW(bool, lod0OnCollision, false);
    PROPERTY_RW(U8, maxLodLevel, 255u);

   protected:
    vector<RenderStagePass> _exclusionStagePasses;
};

};  // namespace Divide

#endif
