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

constexpr size_t MATERIAL_TEXTURE_COUNT = std::size(g_materialTextures);
using NodeMaterialTextures = std::array<vec2<U32>, MATERIAL_TEXTURE_COUNT + 1>;

FORCE_INLINE [[nodiscard]] vec2<U32> TextureToUVec2(const SamplerAddress address) noexcept {
    // GL_ARB_bindless_texture:
    // In the following four constructors, the low 32 bits of the sampler
    // type correspond to the .x component of the uvec2 and the high 32 bits
    // correspond to the .y component.
    // uvec2(any sampler type)     // Converts a sampler type to a pair of 32-bit unsigned integers
    // any sampler type(uvec2)     // Converts a pair of 32-bit unsigned integers to a sampler type
    // uvec2(any image type)       // Converts an image type to a pair of 32-bit unsigned integers
    // any image type(uvec2)       // Converts a pair of 32-bit unsigned integers to an image type

    return vec2<U32> {
        to_U32(address & 0xFFFFFFFF), //low -> x
        to_U32(address >> 32) // high -> y
    };
}

FORCE_INLINE [[nodiscard]] SamplerAddress Uvec2ToTexture(const vec2<U32> address) noexcept {
    return ((SamplerAddress(address.y) << 32) | address.x);
}

#pragma pack(push, 1)
    struct NodeTransformData
    {
        mat4<F32> _worldMatrix = MAT4_INITIAL_TRANSFORM;
        mat4<F32> _prevWorldMatrix = MAT4_INITIAL_TRANSFORM;

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
        //y = 4x8U: reserved, reserved, reserved, isDoubleSided
        //z = 4x8U: reserved, shadingMode, use packed OMR, bump method
        //w = Probe lookup index + 1 (0 = sky cubemap)
        vec4<U32> _data;
        //x = 4x8U: tex op Unit0, tex op Unit1, tex op Specular, Emissive
        //y = 4x8U: tex op Occlusion, tex op Metalness, tex op Roughness, tex op Opcaity
        //z = 4x8U: use albedo texture alpha channel, use opacity map alpha channel, specular Factor, gloss Factor
        //w = 4x8u: receives shadows, reserved, reserved, reserved
        vec4<U32> _textureOperations;
    };

    [[nodiscard]] size_t HashMaterialData(const NodeMaterialData& dataIn);
    [[nodiscard]] size_t HashTexturesData(const NodeMaterialTextures& dataIn);
#pragma pack(pop)

} //namespace Divide

#endif //_RENDER_PASS_NODE_BUFFERED_DATA_H_