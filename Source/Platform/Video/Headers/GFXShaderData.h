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
#ifndef _HARDWARE_VIDEO_GFX_SHADER_DATA_H_
#define _HARDWARE_VIDEO_GFX_SHADER_DATA_H_

#include "config.h"

namespace Divide {

enum class RenderStage : U8;

struct GFXShaderData {
#pragma pack(push, 1)
      struct CamData {
          mat4<F32> _ProjectionMatrix = MAT4_IDENTITY;
          mat4<F32> _ViewMatrix = MAT4_IDENTITY;
          mat4<F32> _InvViewMatrix = MAT4_IDENTITY;
          mat4<F32> _WorldAOVPMatrix = MAT4_IDENTITY;
          vec4<F32> _ViewPort = { 0.0f, 0.0f, 1.0f, 1.0f };
          // x - scale, y - bias, z - light bleed bias, w - min shadow variance
          vec4<F32> _lightingTweakValues = { 1.f, 1.f, 0.2f, 0.001f};
          //x - nearPlane, y - farPlane, z - FoV, w - clip plane count
          vec4<F32> _cameraProperties = { 0.01f, 1.0f, 40.f, 0.f };
          //xy - depth range, zw - light cluster size X / Y
          vec4<F32> _renderTargetInfo{0.f, 1.f, 1.f, 1.f};
          vec4<F32> _clipPlanes[Config::MAX_CLIP_DISTANCES];
          vec4<F32> _padding__[9];
      } _camData;
#pragma pack(pop)

    struct PrevFrameData
    {
        mat4<F32> _PreviousViewMatrix = MAT4_IDENTITY;
        mat4<F32> _PreviousProjectionMatrix = MAT4_IDENTITY;
        mat4<F32> _PreviousViewProjectionMatrix = MAT4_IDENTITY;
    } _prevFrameData;

    bool _camNeedsUpload = true;
};

[[nodiscard]] F32 AspectRatio(const GFXShaderData::CamData& dataIn) noexcept;
[[nodiscard]] vec2<F32> CameraZPlanes(const GFXShaderData::CamData& dataIn) noexcept;
[[nodiscard]] F32 FoV(const GFXShaderData::CamData& dataIn) noexcept;

[[nodiscard]] bool ValidateGPUDataStructure() noexcept;
}; //namespace Divide

#endif //_HARDWARE_VIDEO_GFX_SHADER_DATA_H_

#include "GFXShaderData.inl"