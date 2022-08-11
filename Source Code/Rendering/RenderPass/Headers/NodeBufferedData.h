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
#ifndef _RENDER_PASS_NODE_BUFFERED_DATA_H_
#define _RENDER_PASS_NODE_BUFFERED_DATA_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace Divide {
enum class TextureUsage : unsigned char;

constexpr TextureUsage g_materialTextures[] = {
    TextureUsage::UNIT0,
    TextureUsage::OPACITY,
    TextureUsage::NORMALMAP,
    TextureUsage::HEIGHTMAP,
    TextureUsage::SPECULAR,
    TextureUsage::METALNESS,
    TextureUsage::ROUGHNESS,
    TextureUsage::OCCLUSION,
    TextureUsage::EMISSIVE,
    TextureUsage::UNIT1,
    TextureUsage::PROJECTION,
    TextureUsage::REFLECTION_PLANAR,
    TextureUsage::REFRACTION_PLANAR,
    TextureUsage::REFLECTION_CUBE
};

constexpr size_t MATERIAL_TEXTURE_COUNT = std::size(g_materialTextures) + 2; /* padded for alignment. Keep at 128 bytes.*/

#pragma pack(push, 1)
    struct NodeTransformData
    {
        mat4<F32> _worldMatrix = MAT4_INITIAL_TRANSFORM;
        mat4<F32> _prevWorldMatrix = MAT4_INITIAL_TRANSFORM;
        //[0][0]...[2][2] - normalMatrix
        //[0][3] = 4x8U: reserverd, animation ticked this frame (for motion blur), LoDLevel, occlusion cull
        //[1][3] = selectionFlag
        //[2][3] = boneCount
        //[3][0..3] = boundingSphere
        mat4<F32> _normalMatrixW = MAT4_IDENTITY;
    };

    struct NodeMaterialData
    {
        //base colour
        vec4<F32> _albedo;
        //rgb - emissive
        //a   - parallax factor
        vec4<F32> _emissiveAndParallax;
        //rgb - ambientColour (Don't really need this. To remove eventually, but since we have the space, might as well)
        //a - specular strength [0...Material::MAX_SHININESS]. Used mainly by Phong shading
        vec4<F32> _colourData;
        //x = 4x8U: occlusion, metallic, roughness, isDoubleSided
        //y = 4x8U: specular.r, specular.g, specular.b, use packed OMR
        //z = 4x8U: bump method, shadingMode, reserved, reserved
        //w = Probe lookup index + 1 (0 = sky cubemap)
        vec4<U32> _data;
        //x = 4x8U: tex op Unit0, tex op Unit1, tex op Specular, Emissive
        //y = 4x8U: tex op Occlusion, tex op Metalness, tex op Roughness, tex op Opcaity
        //z = 4x8U: use albedo texture alpha channel, use opacity map alpha channel, specular Factor, gloss Factor
        //w = 4x8u: receives shadows, reserved, reserved, reserved
        vec4<U32> _textureOperations;
    };

    [[nodiscard]] size_t HashMaterialData(const NodeMaterialData& dataIn);
#pragma pack(pop)

} //namespace Divide

#endif //_RENDER_PASS_NODE_BUFFERED_DATA_H_