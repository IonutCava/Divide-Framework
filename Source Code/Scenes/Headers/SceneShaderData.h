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

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

constexpr U8 GLOBAL_WATER_BODIES_COUNT = 2u;
constexpr U16 GLOBAL_PROBE_COUNT = 512u;

class SceneShaderData {
    struct SceneShaderBufferData {
        // w - reserved
        vec4<F32> _sunPosition = VECTOR4_ZERO;
        // w - reserved
        FColour4 _sunColour = DefaultColours::WHITE;
        // x,y,z - direction, w - speed
        vec4<F32> _windDetails = VECTOR4_ZERO;
        FogDetails _fogDetails{};
        WaterBodyData _waterEntities[GLOBAL_WATER_BODIES_COUNT] = {};
        vec4<F32> _padding[7];
    };

  public:
    explicit SceneShaderData(GFXDevice& context);

    void sunDetails(const vec3<F32>& sunPosition, const FColour3& colour) noexcept {
        if (_sceneBufferData._sunPosition != sunPosition ||
            _sceneBufferData._sunColour != colour)
        {
            _sceneBufferData._sunPosition.set(sunPosition);
            _sceneBufferData._sunColour.set(colour);
            _sceneDataDirty = true;
        }
    }

    void fogDetails(const FogDetails& details) noexcept {
        if (_sceneBufferData._fogDetails != details) {
            _sceneBufferData._fogDetails = details;
            _sceneDataDirty = true;
        }
    }

    void fogDensity(F32 density, F32 scatter) noexcept {
        CLAMP_01(density);
        CLAMP_01(scatter);
        if (!COMPARE(_sceneBufferData._fogDetails._colourAndDensity.a, density) ||
            !COMPARE(_sceneBufferData._fogDetails._colourSunScatter.a, scatter))
        {
            _sceneBufferData._fogDetails._colourAndDensity.a = density;
            _sceneBufferData._fogDetails._colourSunScatter.a = scatter;
            _sceneDataDirty = true;
        }
    }

    void windDetails(const F32 directionX, const F32 directionY, const F32 directionZ, const F32 speed) noexcept {
        if (!COMPARE(_sceneBufferData._windDetails.x, directionX) ||
            !COMPARE(_sceneBufferData._windDetails.y, directionY) ||
            !COMPARE(_sceneBufferData._windDetails.z, directionZ) ||
            !COMPARE(_sceneBufferData._windDetails.w, speed))
        {
            _sceneBufferData._windDetails.set(directionX, directionY, directionZ, speed);
            _sceneDataDirty = true;
        }
    }

    bool waterDetails(const U8 index, const WaterBodyData& data) noexcept {
        if (index < GLOBAL_WATER_BODIES_COUNT && _sceneBufferData._waterEntities[index] != data) {
            _sceneBufferData._waterEntities[index] = data;
            _sceneDataDirty = true;
            return true;
        }

        return false;
    }

    bool probeState(const U16 index, const bool state) noexcept {
        if (index < GLOBAL_PROBE_COUNT) {
            ProbeData& data = _probeData[index];
            const F32 fState = state ? 1.f : 0.f;
            if (!COMPARE(data._positionW.w, fState)) {
                data._positionW.w = fState;
                _probeDataDirty = true;
                return true;
            }
        }

        return false;
    }

    bool probeData(const U16 index, const vec3<F32>& center, const vec3<F32>& halfExtents) noexcept {
        if (index < GLOBAL_PROBE_COUNT) {
            ProbeData& data = _probeData[index];
            if (data._positionW.xyz != center ||
                data._halfExtents.xyz != halfExtents)
            {
                data._positionW.xyz = center;
                data._halfExtents.xyz = halfExtents;
                _probeDataDirty = true;
            }
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
      ShaderBuffer_uptr _sceneShaderData = nullptr;
      ShaderBuffer_uptr _probeShaderData = nullptr;
};
} //namespace Divide

#endif //_SCENE_SHADER_DATA_H_
