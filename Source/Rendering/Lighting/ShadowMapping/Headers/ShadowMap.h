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
#ifndef DVD_SHADOW_MAP_H_
#define DVD_SHADOW_MAP_H_

#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

namespace Divide {

enum class ShadowType : U8 {
    SINGLE = 0,
    CSM,
    CUBEMAP,
    COUNT
};

namespace Names {
    static const char* shadowType[] = {
          "SINGLE", "CSM", "CUBEMAP", "UNKNOWN"
    };
}

class Light;
class Camera;
class ShadowMapInfo;
class SceneRenderState;

enum class LightType : U8;

namespace GFX {
    class CommandBuffer;
    struct MemoryBarrierCommand;
}

class LightPool;
class SceneState;
class PlatformContext;
class ShadowMapGenerator {

public:
    virtual ~ShadowMapGenerator() = default;

protected:
    explicit ShadowMapGenerator(GFXDevice& context, ShadowType type) noexcept;

    friend class ShadowMap;
    virtual void render(const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) = 0;

    virtual void updateMSAASampleCount([[maybe_unused]] const U8 sampleCount) { }

protected:
    GFXDevice& _context;
    const ShadowType _type;
};

FWD_DECLARE_MANAGED_STRUCT(DebugView);
/// All the information needed for a single light's shadowmap
NOINITVTABLE_CLASS(ShadowMap)
{
  public:
    static constexpr U8 MAX_SHADOW_FRAME_LIFETIME = 32u;
    static constexpr U8 WORLD_AO_LAYER_INDEX = (Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT) ;

  public:
    // Init and destroy buffers, shaders, etc
    static void initShadowMaps(GFXDevice& context);
    static void destroyShadowMaps(GFXDevice& context);

    static void reset();

    // Reset usage flags and set render targets back to default settings
    static void resetShadowMaps();

    static void bindShadowMaps(GFX::CommandBuffer& bufferInOut);
    static U32  getLightLayerRequirements(const Light& light);
    static bool freeShadowMapOffset(const Light& light);
    static bool markShadowMapsUsed(Light& light);
    static bool generateShadowMaps(const Camera& playerCamera, Light& light, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);

    static ShadowType getShadowTypeForLightType(LightType type) noexcept;
    static LightType getLightTypeForShadowType(ShadowType type) noexcept;

    static const RenderTargetHandle& getShadowMap(LightType type);
    static const RenderTargetHandle& getShadowMapCache(LightType type);

    static const RenderTargetHandle& getShadowMap(ShadowType type);
    static const RenderTargetHandle& getShadowMapCache(ShadowType type);

    static void setDebugViewLight(GFXDevice& context, Light* light);

    static void setMSAASampleCount(ShadowType type, U8 sampleCount);

    static vector<Camera*>& shadowCameras(const ShadowType type) noexcept { return s_shadowCameras[to_base(type)]; }

    static void generateWorldAO(const Camera& playerCamera, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

  protected:
      static bool commitLayerRange(Light& light);
      static bool freeShadowMapOffsetLocked(const Light& light);

  protected:
    struct ShadowLayerData {
        I64 _lightGUID = -1;
        U16 _lifetime = MAX_SHADOW_FRAME_LIFETIME;
    };
    using LayerLifetimeMask = vector<ShadowLayerData>;
    static Mutex s_shadowMapUsageLock;
    static std::array<LayerLifetimeMask, to_base(ShadowType::COUNT)> s_shadowMapLifetime;
    static vector<std::unique_ptr<ShadowMapGenerator>> s_shadowMapGenerators;

    static std::array<RenderTargetHandle, to_base(ShadowType::COUNT)> s_shadowMaps;
    static std::array<RenderTargetHandle, to_base(ShadowType::COUNT)> s_shadowMapCaches;
    static vector<DebugView_ptr> s_debugViews;

    static Light* s_shadowPreviewLight;
    using ShadowCameraPool = vector<Camera*>;
    static std::array<U16, to_base(ShadowType::COUNT)> s_shadowPassIndex;
    static std::array<ShadowCameraPool, to_base(ShadowType::COUNT)> s_shadowCameras;
};

}  // namespace Divide

#endif //DVD_SHADOW_MAP_H_
