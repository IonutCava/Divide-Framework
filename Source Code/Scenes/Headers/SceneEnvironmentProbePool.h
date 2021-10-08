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

class Camera;
class EnvironmentProbeComponent;
using EnvironmentProbeList = vector<EnvironmentProbeComponent*>;

class GFXDevice;
class SceneRenderState;
class SceneEnvironmentProbePool final : public SceneComponent {
public:
    SceneEnvironmentProbePool(Scene& parentScene);
    ~SceneEnvironmentProbePool();

    static void Prepare(GFX::CommandBuffer& bufferInOut);
    static void OnStartup(GFXDevice& context);
    static void OnShutdown(GFXDevice& context);
    static RenderTargetHandle ReflectionTarget();

    const EnvironmentProbeList& sortAndGetLocked(const vec3<F32>& position);
    const EnvironmentProbeList& getLocked() const;

    void registerProbe(EnvironmentProbeComponent* probe);
    void unregisterProbe(EnvironmentProbeComponent* probe);

    void lockProbeList() const;
    void unlockProbeList() const;

    void debugProbe(EnvironmentProbeComponent* probe);
    POINTER_R(EnvironmentProbeComponent, debugProbe, nullptr);

    static vector<Camera*>& ProbeCameras() noexcept { return s_probeCameras; }

    static I16  AllocateSlice(bool lock);
    static void UnlockSlice(I16 slice);

    static bool ProbesDirty()                 noexcept { return s_probesDirty; }
    static void ProbesDirty(const bool state) noexcept { s_probesDirty = state; }

    static void OnNodeUpdated(SceneEnvironmentProbePool& probePool, const SceneGraphNode& node);
    static void OnTimeOfDayChange(SceneEnvironmentProbePool& probePool);

protected:
    mutable SharedMutex _probeLock;
    EnvironmentProbeList _envProbes;

    static vector<DebugView_ptr> s_debugViews;
    static vector<Camera*> s_probeCameras;
    static bool s_probesDirty;

private:
    static std::array<std::pair<bool/*available*/, bool/*locked*/>, Config::MAX_REFLECTIVE_PROBES_PER_PASS> s_availableSlices;
    static RenderTargetHandle s_reflection;
};

} //namespace Divide

#endif //_SCENE_ENVIRONMENT_PROBE_POOL_H_
