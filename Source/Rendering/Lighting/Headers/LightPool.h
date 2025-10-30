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
#ifndef DVD_LIGHT_POOL_H_
#define DVD_LIGHT_POOL_H_

#include "Light.h"

#include "Scenes/Headers/SceneComponent.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Managers/Headers/FrameListenerManager.h"

namespace Divide {

namespace Time {
    class ProfileTimer;
};

class Frustum;
class ShaderProgram;
class SceneGraphNode;
class SceneRenderState;

struct CameraSnapshot;

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

class LightPool final : public FrameListener, 
                        public SceneComponent,
                        public PlatformContextComponent {
  protected:
      struct LightProperties {
          /// rgb = diffuse
          /// w = cosOuterConeAngle;
          FColour4 _diffuse = { DefaultColours::WHITE.rgb, 0.0f };
          /// light position ((0,0,0) for directional lights)
          /// w = range
          float4 _position = { 0.0f, 0.0f, 0.0f, 0.0f };
          /// xyz = light direction (spot and directional lights only. (0,0,0) for point)
          /// w = spot angle
          float4 _direction = { 0.0f, 0.0f, 0.0f, 45.0f };
          /// x = light type: 0 - directional, 1 - point, 2 - spot, 3 - none
          /// y = shadow index (-1 = no shadows)
          /// z = reserved
          /// w = reserved
          int4 _options = { 3, -1, 0, 0 };
      };

#pragma pack(push, 1)
      struct PointShadowProperties
      {
          float4 _details;
          float4 _position;
      };
      struct SpotShadowProperties
      {
          float4 _details;
          float4 _position;
          mat4<F32> _vpMatrix;
      };
      struct CSMShadowProperties
      {
          float4 _details;
          std::array<float4, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT> _position{};
          std::array<mat4<F32>, Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT> _vpMatrix{};
      };
      struct ShadowProperties {
          std::array<PointShadowProperties, Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS> _pointLights{};
          std::array<SpotShadowProperties, Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS> _spotLights{};
          std::array<CSMShadowProperties, Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS> _dirLights{};

          [[nodiscard]] bufferPtr data() const noexcept { return (bufferPtr)_pointLights.data(); }
      };
#pragma pack(pop)

  public:
    struct ShadowLightList
    {
        std::array<Light*, Config::Lighting::MAX_SHADOW_CASTING_LIGHTS> _entries{};
        U16 _count = 0u;
    };
    struct MovingVolume {
        BoundingSphere _volume;
        bool _staticSource = false;
    };
    using LightList = eastl::fixed_vector<Light*, 32u, true>;

    static void InitStaticData( PlatformContext& context );
    static void DestroyStaticData();

    explicit LightPool(Scene& parentScene, PlatformContext& context);
    ~LightPool() override;

    /// Add a new light to the manager
    [[nodiscard]] bool addLight(Light& light);
    /// remove a light from the manager
    [[nodiscard]] bool removeLight(const Light& light);
    /// disable or enable a specific light type
    void toggleLightType(const LightType type)                   noexcept { toggleLightType(type, !lightTypeEnabled(type)); }
    void toggleLightType(const LightType type, const bool state) noexcept { _lightTypeState[to_U32(type)] = state; }

    [[nodiscard]] bool lightTypeEnabled(const LightType type) const noexcept { return _lightTypeState[to_U32(type)]; }
    /// Retrieve the number of active lights in the scene;
    [[nodiscard]] U32 getActiveLightCount(const RenderStage stage, const LightType type) const noexcept { return _activeLightCount[to_base(stage)][to_U32(type)]; }

    bool clear() noexcept;
    [[nodiscard]] LightList& getLights(const LightType type) {
        SharedLock<SharedMutex> r_lock(_lightLock); 
        return _lights[to_U32(type)];
    }

    [[nodiscard]] Light* getLight(I64 lightGUID, LightType type) const;

    void sortLightData(RenderStage stage, const CameraSnapshot& cameraSnapshot);
    void uploadLightData(RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut);

    void drawLightImpostors(GFX::CommandBuffer& bufferInOut) const;

    void preRenderAllPasses(const Camera* playerCamera);

    void onVolumeMoved(const BoundingSphere& volume, bool staticSource);

    /// nullptr = disabled
    void debugLight(Light* light);

    /// Get the appropriate shadow bind slot for every light's shadow
    [[nodiscard]] static U8 GetShadowBindSlotOffset(const ShadowType type) noexcept {
        return s_shadowLocation[to_U32(type)];
    }

    PROPERTY_RW(bool, lightImpostorsEnabled, false);
    POINTER_R(Light, debugLight, nullptr);

  protected:
    [[nodiscard]] bool frameStarted(const FrameEvent& evt) override;
    [[nodiscard]] bool frameEnded(const FrameEvent& evt) override;

  protected:
    using LightShadowProperties = std::array<Light::ShadowProperties, Config::Lighting::MAX_SHADOW_CASTING_LIGHTS>;

    friend class RenderPass;
    void generateShadowMaps(const Camera& playerCamera, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);

    friend class ProjectManager;
    [[nodiscard]] LightList::const_iterator findLight(const I64 GUID, const LightType type) const {
        SharedLock<SharedMutex> r_lock(_lightLock);
        return findLightLocked(GUID, type);
    }

    [[nodiscard]] LightList::const_iterator findLightLocked(const I64 GUID, const LightType type) const {
        return eastl::find_if(cbegin(_lights[to_U32(type)]), cend(_lights[to_U32(type)]),
                              [GUID](Light* const light) noexcept {
                                  return light && light->getGUID() == GUID;
                              });
    }

    [[nodiscard]] bool isShadowCacheInvalidated(const float3& cameraPosition, Light* light);


    [[nodiscard]] static bool IsLightInViewFrustum(const Frustum& frustum, const Light* light) noexcept;


    friend class ShadowMap;
    static ShaderBuffer* ShadowBuffer() { return s_shadowBuffer.get(); }

    friend class Renderer;
    [[nodiscard]] static ShaderBuffer* LightBuffer() { return s_lightBuffer.get(); }
    [[nodiscard]] static ShaderBuffer* SceneBuffer() { return s_sceneBuffer.get(); }

    [[nodiscard]] U32           sortedLightCount(const RenderStage stage) const { return _sortedLightPropertiesCount[to_base(stage)]; }

  private:
      U32 uploadLightList(RenderStage stage,
                          const LightList& lights,
                          const mat4<F32>& viewMatrix);

  private:
     struct SceneData {
         // x = directional light count, y = point light count, z = spot light count, w = reserved
         uint4 _globalData = { 0, 0, 0, 0 };
         // a = reserved
         float4 _ambientColour = DefaultColours::BLACK;
         mat4<F32> _padding0[3]; float4 _padding1[2];
     };

    using LightData = std::array<LightProperties, Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME>;

    using LightCountPerType = std::array<U32, to_base(LightType::COUNT)>;

    std::array<LightCountPerType, to_base(RenderStage::COUNT)> _activeLightCount{};
    std::array<LightList,         to_base(RenderStage::COUNT)> _sortedLights{};
    std::array<LightData,         to_base(RenderStage::COUNT)> _sortedLightProperties{};
    std::array<U32,               to_base(RenderStage::COUNT)> _sortedLightPropertiesCount{};
    std::array<SceneData,         to_base(RenderStage::COUNT)> _sortedSceneProperties{};

    std::array<LightList,         to_base(LightType::COUNT)>   _lights{};
    std::array<bool,              to_base(LightType::COUNT)>   _lightTypeState{};
    ShadowProperties _shadowBufferData;

    mutable SharedMutex _movedSceneVolumesLock;
    eastl::fixed_vector<MovingVolume, Config::MAX_VISIBLE_NODES, true> _movedSceneVolumes;

    mutable SharedMutex _lightLock{};
    Time::ProfileTimer& _shadowPassTimer;
    U32                 _totalLightCount = 0u;
    bool                _shadowBufferDirty = false;

    static std::array<U8, to_base(ShadowType::COUNT)> s_shadowLocation;
    static Handle<Texture>       s_lightIconsTexture;
    static Handle<ShaderProgram> s_lightImpostorShader;
    static ShaderBuffer_uptr s_lightBuffer;
    static ShaderBuffer_uptr s_sceneBuffer;
    static ShaderBuffer_uptr s_shadowBuffer;
};

FWD_DECLARE_MANAGED_CLASS(LightPool);

};  // namespace Divide

#endif //DVD_LIGHT_POOL_H_
