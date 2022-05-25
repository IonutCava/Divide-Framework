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
#ifndef _CSM_GENERATOR_H_
#define _CSM_GENERATOR_H_

#include "ShadowMap.h"
#include "Platform/Video/Headers/CommandsImpl.h"

namespace Divide {

class Quad3D;
class Camera;
class Pipeline;
class GFXDevice;
class ShaderBuffer;
class DirectionalLightComponent;

struct DebugView;

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

/// Directional lights can't deliver good quality shadows using a single shadow map.
/// This technique offers an implementation of the CSM method
class CascadedShadowMapsGenerator final : public ShadowMapGenerator {
   public:
    explicit CascadedShadowMapsGenerator(GFXDevice& context);
    ~CascadedShadowMapsGenerator();

    void render(const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) override;

   protected:
    using SplitDepths = std::array<F32, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT>;

    void postRender(const DirectionalLightComponent& light, GFX::CommandBuffer& bufferInOut);

    SplitDepths calculateSplitDepths( DirectionalLightComponent& light, const vec2<F32>& nearFarPlanes) const noexcept;

    void applyFrustumSplits(DirectionalLightComponent& light, const Camera& shadowCamera, U8 numSplits) const;

    void updateMSAASampleCount(U8 sampleCount) override;

  protected:
    Pipeline* _blurPipeline = nullptr;
    ShaderProgram_ptr _blurDepthMapShader = nullptr;
    GFX::SendPushConstantsCommand  _shaderConstantsCmd;
    RenderTargetHandle _drawBufferDepth;
    RenderTargetHandle _blurBuffer;
};

};  // namespace Divide
#endif //_CSM_GENERATOR_H_