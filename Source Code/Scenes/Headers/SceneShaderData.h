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
#ifndef _SCENE_SHADER_DATA_H_
#define _SCENE_SHADER_DATA_H_

#include "Scenes/Headers/SceneState.h"
#include "Utility/Headers/Colours.h"

namespace Divide {

class GFXDevice;
class ShaderBuffer;

constexpr U8 GLOBAL_WATER_BODIES_COUNT = 2u;
constexpr U16 GLOBAL_PROBE_COUNT = 512u;

class SceneShaderData {
    struct SceneShaderBufferData {
        // w - reserved
        vec4<F32> _sunDirection = { 0.0f };
        // w - reserved
        FColour4 _sunColour = DefaultColours::WHITE;
        // w - reserved
        FColour4 _zenithColour = { 0.025f, 0.1f, 0.5f, 1.0f};
        // w - reserved
        FColour4 _horizonColour = { 0.6f, 0.7f, 1.0f, 0.0f};
        // x,y,z - direction, w - speed
        vec4<F32> _windDetails = {0.0f};
        //x - light bleed bias, y - min shadow variance, z - reserved, w - reserved
        vec4<F32> _shadowingSettings = {0.2f, 0.001f, 1.0f, 1.0f};
        FogDetails _fogDetails{};
        WaterBodyData _waterEntities[GLOBAL_WATER_BODIES_COUNT] = {};

        //RenderDoc: vec4 fogDetails; vec4 windDetails; vec4 shadowSettings; vec4 otherData;
    };

  public:
    explicit SceneShaderData(GFXDevice& context);

    void sunDetails(const vec3<F32>& direction, const FColour3& colour) noexcept {
        _sceneBufferData._sunDirection.set(direction);
        _sceneBufferData._sunColour.set(colour);
        _sceneDataDirty = true;
    }

    void skyColour(const FColour4& horizon, const FColour4& zenith) noexcept {
        _sceneBufferData._horizonColour = horizon;
        _sceneBufferData._zenithColour = zenith;
        _sceneDataDirty = true;
    }

    void fogDetails(const FogDetails& details) noexcept {
        _sceneBufferData._fogDetails = details;
        _sceneDataDirty = true;
    }

    void fogDensity(F32 densityB, F32 densityC) noexcept {
        CLAMP_01(densityB);
        CLAMP_01(densityC);
        _sceneBufferData._fogDetails._colourAndDensity.a = densityB;
        _sceneBufferData._fogDetails._colourSunScatter.a = densityC;
        _sceneDataDirty = true;
    }

    void shadowingSettings(const F32 lightBleedBias, const F32 minShadowVariance) noexcept {
        _sceneBufferData._shadowingSettings.xy = { lightBleedBias, minShadowVariance };
        _sceneDataDirty = true;
    }

    void windDetails(const F32 directionX, const F32 directionY, const F32 directionZ, const F32 speed) noexcept {
        _sceneBufferData._windDetails.set(directionX, directionY, directionZ, speed);
        _sceneDataDirty = true;
    }

    bool waterDetails(const U8 index, const WaterBodyData& data) noexcept {
        if (index < GLOBAL_WATER_BODIES_COUNT) {
            _sceneBufferData._waterEntities[index] = data;
            _sceneDataDirty = true;

            return true;
        }

        return false;
    }

    bool probeState(const U16 index, const bool state) noexcept {
        if (index < GLOBAL_PROBE_COUNT) {
            _probeData[index]._positionW.w = state ? 1.f : 0.f;
            return true;
        }

        return false;
    }

    bool probeData(const U16 index, const vec3<F32>& center, const vec3<F32>& halfExtents) noexcept {
        if (index < GLOBAL_PROBE_COUNT) {
            _probeData[index]._positionW.xyz = center;
            _probeData[index]._halfExtents.xyz = halfExtents;
            _probeDataDirty = true;
            return true;
        }

        return false;
    }

    void uploadToGPU();

  private:
      using ProbeBufferData = std::array<ProbeData, GLOBAL_PROBE_COUNT>;

      GFXDevice& _context;
      bool _sceneDataDirty = true;
      bool _probeDataDirty = true;
      SceneShaderBufferData _sceneBufferData;
      ProbeBufferData _probeData = {};
      /// Generic scene data that doesn't change per shader
      ShaderBuffer* _sceneShaderData = nullptr;
      ShaderBuffer* _probeShaderData = nullptr;
};
} //namespace Divide

#endif //_SCENE_SHADER_DATA_H_
