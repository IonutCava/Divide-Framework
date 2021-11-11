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
    TextureUsage::REFLECTION,
    TextureUsage::REFRACTION
};

constexpr size_t MATERIAL_TEXTURE_COUNT = (sizeof(g_materialTextures) / sizeof(g_materialTextures[0]));
using NodeMaterialTextures = std::array<SamplerAddress, MATERIAL_TEXTURE_COUNT>;

#pragma pack(push, 1)
    struct NodeTransformData
    {
        mat4<F32> _worldMatrix = MAT4_IDENTITY;
        mat4<F32> _prevWorldMatrix = MAT4_IDENTITY;

        // [0...2][0...2] = normal matrix
        // [3][0...2]     = bounds center
        // [0][3]         = 4x8U: bone count, lod level, animation ticked this frame (for motion blur), occlusion cull
        // [1][3]         = 2x16F: BBox HalfExtent (X, Y) 
        // [2][3]         = 2x16F: BBox HalfExtent (Z), BSphere Radius
        // [3][3]         = 2x16F: (Data Flag, reserved)
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
        //a - specular strength [0...1000]. Used mainly by Phong shading
        vec4<F32> _colourData;
        //x = 4x8U: occlusion, metallic, roughness, selection flag (1 == hovered, 2 == selected)
        //y = 4x8U: specularR, specularG, specularB, reserved
        //z = 4x8U: tex op Unit0, tex op Unit1, tex op Specular, bump method
        //w = Probe lookup index + 1 (0 = sky cubemap)
        vec4<U32> _data;

        vec4<U32> _textures[(MATERIAL_TEXTURE_COUNT + 1) / 2];
    };

    [[nodiscard]] size_t HashMaterialData(const NodeMaterialData& dataIn);
    [[nodiscard]] size_t HashTexturesData(const NodeMaterialTextures& dataIn);
#pragma pack(pop)

} //namespace Divide

#endif //_RENDER_PASS_NODE_BUFFERED_DATA_H_