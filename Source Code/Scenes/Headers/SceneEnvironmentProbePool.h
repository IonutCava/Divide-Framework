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
#ifndef _SCENE_ENVIRONMENT_PROBE_POOL_H_
#define _SCENE_ENVIRONMENT_PROBE_POOL_H_

#include "Scenes/Headers/SceneComponent.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

namespace Divide {
class SceneGraphNode;

namespace GFX {
class CommandBuffer;
}

FWD_DECLARE_MANAGED_STRUCT(DebugView);
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

class Camera;
class Pipeline;
class EnvironmentProbeComponent;
using EnvironmentProbeList = vector<EnvironmentProbeComponent*>;

class GFXDevice;
class SceneRenderState;
class SceneEnvironmentProbePool final : public SceneComponent {
public:
    enum class ComputationStages : U8 {
        MIP_MAP_SOURCE,
        PREFILTER_MAP,
        IRRADIANCE_CALC,
        MIP_MAP_PREFILTER,
        MIP_MAP_IRRADIANCE,
        COUNT
    };

    SceneEnvironmentProbePool(Scene& parentScene) noexcept;
    ~SceneEnvironmentProbePool();

    static void Prepare(GFX::CommandBuffer& bufferInOut);
    static void UpdateSkyLight(GFXDevice& context, GFX::CommandBuffer& bufferInOut);
    static void OnStartup(GFXDevice& context);
    static void OnShutdown(GFXDevice& context);
    static RenderTargetHandle ReflectionTarget() noexcept;
    static RenderTargetHandle PrefilteredTarget() noexcept;
    static RenderTargetHandle IrradianceTarget() noexcept;
    static RenderTargetHandle BRDFLUTTarget() noexcept;

    const EnvironmentProbeList& sortAndGetLocked(const vec3<F32>& position);
    const EnvironmentProbeList& getLocked() const noexcept;

    void registerProbe(EnvironmentProbeComponent* probe);
    void unregisterProbe(const EnvironmentProbeComponent* probe);

    void lockProbeList() const noexcept;
    void unlockProbeList() const noexcept;

    void prepareDebugData();
    POINTER_RW(EnvironmentProbeComponent, debugProbe, nullptr);

    void onNodeUpdated(const SceneGraphNode& node) noexcept;

    static vector<Camera*>& ProbeCameras() noexcept { return s_probeCameras; }

    static I16  AllocateSlice(bool lock);
    static void UnlockSlice(I16 slice) noexcept;

    static bool ProbesDirty()                 noexcept { return s_probesDirty; }
    static void ProbesDirty(const bool state) noexcept { s_probesDirty = state; }

    static void OnTimeOfDayChange(const SceneEnvironmentProbePool& probePool) noexcept;

    static [[nodiscard]] bool DebuggingSkyLight() noexcept;
    static               void DebuggingSkyLight(bool state) noexcept;

    static [[nodiscard]] bool SkyLightNeedsRefresh() noexcept;
    static               void SkyLightNeedsRefresh(bool state) noexcept;

    static [[nodiscard]] U16  SkyProbeLayerIndex() noexcept;

protected:
    friend class EnvironmentProbeComponent;
    void createDebugView(U16 layerIndex);
    static void ProcessEnvironmentMap(GFXDevice& context, U16 layerID, bool highPriority, GFX::CommandBuffer& bufferInOut);

private:
    static void ProcessEnvironmentMapInternal(GFXDevice& context, const U16 layerID, ComputationStages& stage, GFX::CommandBuffer& bufferInOut);
    static void PrefilterEnvMap(GFXDevice& context, const U16 layerID, U8 faceIndex, GFX::CommandBuffer& bufferInOut);
    static void ComputeIrradianceMap(GFXDevice& context, const U16 layerID, U8 faceIndex, GFX::CommandBuffer& bufferInOut);
protected:
    mutable SharedMutex _probeLock;
    EnvironmentProbeList _envProbes;

    static vector<DebugView_ptr> s_debugViews;
    static vector<Camera*> s_probeCameras;
    static bool s_probesDirty;
    static bool s_debuggingSkyLight;
    static bool s_skyLightNeedsRefresh;

private:
    struct ProbeSlice {
        bool _available = true;
        bool _locked = false;
    };

    static std::array<ProbeSlice, Config::MAX_REFLECTIVE_PROBES_PER_PASS> s_availableSlices;
    static RenderTargetHandle s_reflection;
    static RenderTargetHandle s_prefiltered;
    static RenderTargetHandle s_irradiance;
    static RenderTargetHandle s_brdfLUT;
    static ShaderProgram_ptr s_previewShader;
    static ShaderProgram_ptr s_irradianceComputeShader;
    static ShaderProgram_ptr s_prefilterComputeShader;
    static ShaderProgram_ptr s_lutComputeShader;
    static Pipeline*         s_pipelineCalcPrefiltered;
    static Pipeline*         s_pipelineCalcIrradiance;
    static bool s_lutTextureDirty;
};

} //namespace Divide

#endif //_SCENE_ENVIRONMENT_PROBE_POOL_H_
