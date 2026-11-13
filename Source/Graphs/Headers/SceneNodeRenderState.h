#pragma once
// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#ifndef DVD_SCENE_NODE_RENDER_STATE_H_
#define DVD_SCENE_NODE_RENDER_STATE_H_

#include "Platform/Video/Headers/RenderStagePass.h"

namespace Divide {

struct RenderStagePass;

struct SceneNodeRenderState {

    [[nodiscard]] bool drawState(RenderStagePass stagePass, bool defaultState = true, const SceneNodeRenderState* source = nullptr) const;
    [[nodiscard]] std::optional<bool> drawStateOverride(RenderStagePass stagePass) const;
    void setDrawStateOverride(RenderStagePass stagePass, bool state);
    void clearDrawStateOverride(RenderStagePass stagePass);
    void addToDrawExclusionMask(RenderStage stage,
                                RenderPassType passType = RenderPassType::COUNT,
                                RenderStagePass::VariantType variant = RenderStagePass::VariantType::COUNT,
                                U16 index = g_AllIndicesID,
                                RenderStagePass::PassIndex pass = RenderStagePass::PassIndex::COUNT);

    PROPERTY_R(bool, drawState, true);
    void drawState(const bool state) noexcept
    {
        if (_drawState != state)
        {
            _drawState = state;
            ++_coverageRevision;
        }
    }
    PROPERTY_R(U64, coverageRevision, 0u);

    // If set to false, the closest AABB point to the camera will be used for LoD calculations
    PROPERTY_RW(bool, useBoundsCenterForLoD, true);
    // If set to true, we will always use LoD 0 for nodes who's AABB contain the camera (e.g. vegetation, particles, etc)
    PROPERTY_RW(bool, lod0OnCollision, false);
    PROPERTY_RW(U8, maxLodLevel, 255u);

   protected:
    struct DrawMaskEntry
    {
        RenderStagePass _stagePass{};
        bool _draw = false;
    };
    vector<DrawMaskEntry> _drawMask;
};

};  // namespace Divide

#endif //DVD_SCENE_NODE_RENDER_STATE_H_
