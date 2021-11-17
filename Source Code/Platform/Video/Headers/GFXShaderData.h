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
#include "Rendering/Camera/Headers/Frustum.h"

namespace Divide {

enum class RenderStage : U8;

struct GFXShaderData {
#pragma pack(push, 1)
      struct GPUData {
          mat4<F32> _ProjectionMatrix = MAT4_IDENTITY;
          mat4<F32> _InvProjectionMatrix = MAT4_IDENTITY;
          mat4<F32> _ViewMatrix = MAT4_IDENTITY;
          mat4<F32> _InvViewMatrix = MAT4_IDENTITY;
          mat4<F32> _ViewProjectionMatrix = MAT4_IDENTITY;
          mat4<F32> _PreviousViewProjectionMatrix = MAT4_IDENTITY;
          // xyz - position, w - aspect ratio
          vec4<F32> _cameraPosition = VECTOR4_ZERO;
          vec4<F32> _ViewPort = { 0.0f, 0.0f, 1.0f, 1.0f };
          //x - near plane, y - far plane, z - FoV, w - elapsed time (ms)
          vec4<F32> _renderProperties = { 0.01f, 1.0f, 40.0f, 0.0f };
          //x - cluster size X, y - cluster size Y, z - scale, w - bias
          vec4<F32> _lightingProperties = { 120.0f, 100.0f, 1.0f, 1.0f };
          //x - material debug flag, y - reserved, z - camera flag, w - active clip plane count
          vec4<F32> _otherProperties;
          vec4<F32> _frustumPlanes[to_base(FrustumPlane::COUNT)];
          vec4<F32> _clipPlanes[Config::MAX_CLIP_DISTANCES];
          vec4<F32> _padding0[10];
      };
#pragma pack(pop)

    GPUData _data{};

    bool _needsUpload = true;
};

[[nodiscard]] F32 AspectRatio(const GFXShaderData::GPUData& dataIn) noexcept;
[[nodiscard]] vec2<F32> CameraZPlanes(const GFXShaderData::GPUData& dataIn) noexcept;
[[nodiscard]] F32 FoV(const GFXShaderData::GPUData& dataIn) noexcept;

[[nodiscard]] bool ValidateGPUDataStructure() noexcept;
//RenderDoc: mat4 projection; mat4 invprojection; mat4 view; mat4 viewproj; vec4 cam; vec4 renderProp; vec4 frustum[6]; vec4 clip[6];
}; //namespace Divide

#endif //_HARDWARE_VIDEO_GFX_SHADER_DATA_H_

#include "GFXShaderData.inl"