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
#ifndef DVD_SCENE_SHADER_DATA_H_
#define DVD_SCENE_SHADER_DATA_H_

#include "Scenes/Headers/SceneState.h"
#include "Utility/Headers/Colours.h"
#include "Geometry/Material/Headers/MaterialEnums.h"

namespace Divide
{

class GFXDevice;

namespace GFX
{
    class CommandBuffer;
    struct MemoryBarrierCommand;
};

FWD_DECLARE_MANAGED_CLASS(ShaderBuffer);

constexpr U8 GLOBAL_WATER_BODIES_COUNT = 2u;
constexpr U16 GLOBAL_PROBE_COUNT = 512u;

class SceneShaderData
{
    struct SceneShaderBufferData
    {
        // w - altitude
        float4 _sunDirection = {-0.3f, -0.8f, 0.5f, 1.f};
        // w - azimuth
        FColour4 _sunColour = DefaultColours::WHITE;
        // x,y,z - direction, w - speed
        float4 _windDetails = VECTOR4_ZERO;
        // x - elapsed game time (ms), x - elapsed app time, z - material debug flag, w - unused
        float4 _appData = {0.f, 0.f, to_F32(MaterialDebugFlag::COUNT), 0.f};
        FogDetails _fogDetails{};
        WaterBodyData _waterEntities[GLOBAL_WATER_BODIES_COUNT] = {};
        float4 _padding[6];
    };

  public:
    explicit SceneShaderData(GFXDevice& context);

    void appData(const U32 elapsedGameTimeMS, const U32 elapsedAppTimeMS,const MaterialDebugFlag materialDebugFlag)
    {
        if (!COMPARE(_sceneBufferData._appData.x, to_F32(elapsedGameTimeMS)))
        {
            _sceneBufferData._appData.x = to_F32(elapsedGameTimeMS);
            _sceneDataDirty = true;
        }
        if (!COMPARE(_sceneBufferData._appData.y, to_F32(elapsedAppTimeMS)))
        {
            _sceneBufferData._appData.y = to_F32(elapsedAppTimeMS);
            _sceneDataDirty = true;
        }
        if (!COMPARE(_sceneBufferData._appData.z, to_F32(materialDebugFlag)))
        {
            _sceneBufferData._appData.z = to_F32(materialDebugFlag);
            _sceneDataDirty = true;
        }
    }

    void sunDetails(const float3& sunDirection, const FColour3& colour, const F32 altitude, const F32 azimuth) noexcept
    {
        if (_sceneBufferData._sunDirection.xyz != sunDirection ||
            _sceneBufferData._sunColour.xyz != colour ||
            !COMPARE(_sceneBufferData._sunDirection.w, altitude) ||
            !COMPARE(_sceneBufferData._sunColour.w, azimuth))
        {
            _sceneBufferData._sunDirection.set( sunDirection, altitude );
            _sceneBufferData._sunColour.set(colour, azimuth);
            _sceneDataDirty = true;
        }
    }

    void fogDetails(const FogDetails& details) noexcept
    {
        if (_sceneBufferData._fogDetails != details)
        {
            _sceneBufferData._fogDetails = details;
            _sceneDataDirty = true;
        }
    }

    void fogDensity(F32 density, F32 scatter) noexcept
    {
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

    void windDetails(const F32 directionX, const F32 directionY, const F32 directionZ, const F32 speed) noexcept 
    {
        if (!COMPARE(_sceneBufferData._windDetails.x, directionX) ||
            !COMPARE(_sceneBufferData._windDetails.y, directionY) ||
            !COMPARE(_sceneBufferData._windDetails.z, directionZ) ||
            !COMPARE(_sceneBufferData._windDetails.w, speed))
        {
            _sceneBufferData._windDetails.set(directionX, directionY, directionZ, speed);
            _sceneDataDirty = true;
        }
    }

    bool waterDetails(const U8 index, const WaterBodyData& data) noexcept
    {
        if (index < GLOBAL_WATER_BODIES_COUNT && _sceneBufferData._waterEntities[index] != data)
        {
            _sceneBufferData._waterEntities[index] = data;
            _sceneDataDirty = true;
            return true;
        }

        return false;
    }

    bool probeState(const U16 index, const bool state) noexcept
    {
        if (index < GLOBAL_PROBE_COUNT)
        {
            ProbeData& data = _probeData[index];
            const F32 fState = state ? 1.f : 0.f;
            if (!COMPARE(data._positionW.w, fState))
            {
                data._positionW.w = fState;
                _probeDataDirty = true;
                return true;
            }
        }

        return false;
    }

    bool probeData(const U16 index, const float3& center, const float3& halfExtents) noexcept
    {
        if (index < GLOBAL_PROBE_COUNT)
        {
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

    void updateSceneDescriptorSet(GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

  private:
      using ProbeBufferData = std::array<ProbeData, GLOBAL_PROBE_COUNT>;

      GFXDevice& _context;
      bool _sceneDataDirty = true;
      bool _probeDataDirty = true;
      SceneShaderBufferData _sceneBufferData;
      ProbeBufferData _probeData = {};
      /// Generic scene data that doesn't change per shader
      ShaderBuffer_uptr _sceneShaderData;
      ShaderBuffer_uptr _probeShaderData;
};
} //namespace Divide

#endif //DVD_SCENE_SHADER_DATA_H_
