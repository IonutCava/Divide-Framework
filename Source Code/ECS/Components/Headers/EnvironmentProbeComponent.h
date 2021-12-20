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
#ifndef _ENVIRONMENT_PROBE_COMPONENT_H_
#define _ENVIRONMENT_PROBE_COMPONENT_H_

#include "SGNComponent.h"

#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

namespace Divide {

class Scene;
class Pipeline;
class GFXDevice;
class IMPrimitive;
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

namespace GFX {
    class CommandBuffer;
} //namespace GFX

BEGIN_COMPONENT_EXT1(EnvironmentProbe, ComponentType::ENVIRONMENT_PROBE, GUIDWrapper)

public:
    enum class ProbeType : U8 {
        TYPE_INFINITE = 0,
        TYPE_LOCAL,
        COUNT
    };

    enum class UpdateType : U8 {
        ALWAYS = 0,
        ON_DIRTY,
        ON_RATE,
        ONCE,
        COUNT
    };

    struct Names {
        inline static const char* updateType[] = {
            "Always", "On Dirty", "On Rate", "Once", "ERROR!"
        };
    };

public:
    explicit EnvironmentProbeComponent(SceneGraphNode* sgn, PlatformContext& context);
    ~EnvironmentProbeComponent();

    /// Returns true if the probe was updated, false if skipped
    bool refresh(GFX::CommandBuffer& bufferInOut);
    /// Checks if the given bounding sphere has collided with the probe's AABB and if so, mark it for update if required
    bool checkCollisionAndQueueUpdate(const BoundingSphere& sphere) noexcept;

    void updateType(UpdateType type);

    void setBounds(const vec3<F32>& min, const vec3<F32>& max) noexcept;
    void setBounds(const vec3<F32>& center, F32 radius) noexcept;

    void loadFromXML(const boost::property_tree::ptree& pt) override;

    [[nodiscard]] F32 distanceSqTo(const vec3<F32>& pos) const noexcept;
    
    [[nodiscard]] std::array<Camera*, 6> probeCameras() const noexcept;

    //Only "dirty" probes gets refreshed. A probe might get dirty if, for example, a reflective node
    //gets rendered that has this probe as the nearest one
    void queueRefresh() noexcept { _queueRefresh = true; }

    void enabled(bool state) override;

    PROPERTY_R_IW(I16, rtLayerIndex, 0u);
    PROPERTY_RW(bool, showParallaxAABB, false);
    PROPERTY_RW(bool, dirty, true);
    PROPERTY_RW(U8, updateRate, 1u);
    PROPERTY_R(UpdateType, updateType, UpdateType::ON_DIRTY);

    PROPERTY_R(U16, poolIndex, 0u);
protected:
    friend class SceneEnvironmentProbePool; //for poolIndex(U16)
    void poolIndex(U16 index) noexcept;

    void OnData(const ECS::CustomEvent& data) override;

    [[nodiscard]] SceneGraphNode* findNodeToIgnore() const noexcept;

    void updateProbeData() const noexcept;
protected:
    BoundingBox _aabb{ vec3<F32>(-1), vec3<F32>(1) };
    BoundingBox _refaabb{ vec3<F32>(-1), vec3<F32>(1) };

    U8 _currentUpdateCall = 0u;
    ProbeType _type = ProbeType::TYPE_LOCAL;

private:
    bool _queueRefresh = true;
    bool _drawImpostor = false;

END_COMPONENT(EnvironmentProbe);

namespace TypeUtil {
    const char* EnvProveUpdateTypeToString(EnvironmentProbeComponent::UpdateType type) noexcept;
    EnvironmentProbeComponent::UpdateType StringToEnvProveUpdateType(const char* name) noexcept;
}

} //namespace Divide

#endif //_ENVIRONMENT_PROBE_COMPONENT_H_
