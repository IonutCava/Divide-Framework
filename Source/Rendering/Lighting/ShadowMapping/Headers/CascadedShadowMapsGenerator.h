/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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
#ifndef DVD_CSM_GENERATOR_H_
#define DVD_CSM_GENERATOR_H_

#include "ShadowMap.h"
#include "Rendering/Lighting/Headers/ShadowPassState.h"
#include "Rendering/Lighting/Headers/ShadowCasterBounds.h"
#include "Platform/Video/Headers/Commands.h"

namespace Divide {

class Quad3D;
class Camera;
class Pipeline;
class GFXDevice;
class ShaderBuffer;
class ShaderProgram;
class DirectionalLightComponent;

struct DebugView;

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

/// Directional lights can't deliver good quality shadows using a single shadow map.
/// This technique offers an implementation of the CSM method
class CascadedShadowMapsGenerator final : public ShadowMapGenerator {
   public:
    explicit CascadedShadowMapsGenerator(GFXDevice& context);
    ~CascadedShadowMapsGenerator() override;

    void render(const Camera& playerCamera, Light& light, U16 lightIndex, bool& pendingCastersOut, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut, const ShadowRefreshPlan& refreshPlan) override;
    [[nodiscard]] mat4<F32> generateWorldAO( const WorldAOCacheKey& key, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

   protected:
    using SplitDepths = std::array<F32, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT>;

    SplitDepths calculateSplitDepths( DirectionalLightComponent& light, const float2 nearFarPlanes) const noexcept;

    void applyFrustumSplits(DirectionalLightComponent& light, const Camera& shadowCamera, U8 numSplits, U32 layerMask) const;

    void updateMSAASampleCount(U8 sampleCount) override;
    void finishRender() override;

  protected:
    struct StaticCacheLayer
    {
        CSMStaticCacheState _state{};
        CSMStaticCacheKey _pendingKey{};
        ShadowCasterBounds _staticBounds{};
        ShadowCasterBounds* _combinedBounds = nullptr;
        bool* _pendingOut = nullptr;
        bool _pendingCasters = false;
        bool _rebuilt = false;
    };
    std::array<StaticCacheLayer, ShadowMap::WORLD_AO_LAYER_INDEX> _staticLayers{};

    void blurTarget(U16 layerOffset, U16 layerCount, GFX::CommandBuffer& bufferInOut, U8 firstLayer = 0u);

  protected:
    Camera* _worldAOCamera = nullptr;
    WorldAOCacheState _worldAOCache{};
    mat4<F32> _worldAOVP{};
    bool _worldAOPendingCasters = false;
    Pipeline* _blurPipelineCSM = nullptr;
    Pipeline* _blurPipelineAO = nullptr;
    Handle<ShaderProgram> _blurDepthMapShader = INVALID_HANDLE<ShaderProgram>;
    Handle<ShaderProgram> _blurAOMapShader = INVALID_HANDLE<ShaderProgram>;
    PushConstantsStruct _shaderConstants;
    RenderTargetHandle _drawBufferDepth;
    RenderTargetHandle _blurBuffer;
};

};  // namespace Divide

#endif //DVD_CSM_GENERATOR_H_
