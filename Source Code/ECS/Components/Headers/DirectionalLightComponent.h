/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _DIRECTIONAL_LIGHT_COMPONENT_H_
#define _DIRECTIONAL_LIGHT_COMPONENT_H_

#include "Rendering/Lighting/Headers/Light.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"

namespace Divide {

BEGIN_COMPONENT_EXT1(DirectionalLight, ComponentType::DIRECTIONAL_LIGHT, Light)
   public:
    using Light::getSGN;
    using PerSplitToggle = std::array<bool, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT>;

   public:

    explicit DirectionalLightComponent(SceneGraphNode* sgn, PlatformContext& context);

    void setDirection(const vec3<F32>& direction);

    // Quick hack to store previous frame's culling results
    [[nodiscard]] vector<FeedBackContainer>& feedBackContainers() noexcept { return _feedbackContainers; }

    PROPERTY_RW(U8, csmSplitCount, 3u);
    /// CSM extra back up distance for light position
    PROPERTY_RW(F32, csmNearClipOffset, 0.0f);
    /// If this is true, we use the combined AABB of culled shadow casters to "correct" each split frustum to avoid near-clipping/culling artefacts.
    PROPERTY_RW(PerSplitToggle, csmUseSceneAABBFit, create_array<Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT>(true));
    /// If this is true, we render a cone narrow cone to approximate the light's direction
    PROPERTY_RW(bool, showDirectionCone, false);
    /// If true, the direction of this light component can't be modified in the editor (e.g. the light attached to the sun depends on the sun position!)
    PROPERTY_R(bool, lockDirection, false);

    void lockDirection(const bool state) noexcept;

protected:
     void OnData(const ECS::CustomEvent& data) override;

protected:
    //Used to adjust ortho-matrix's near/far planes per pass
    vector<FeedBackContainer> _feedbackContainers;
END_COMPONENT(DirectionalLight);

}  // namespace Divide

#endif //_DIRECTIONAL_LIGHT_COMPONENT_H_