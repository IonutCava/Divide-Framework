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
#ifndef DVD_RENDER_PASS_NODE_BUFFERED_DATA_H_
#define DVD_RENDER_PASS_NODE_BUFFERED_DATA_H_

namespace Divide {
    static constexpr U8 TRANSFORM_IDX = 0u;
    static constexpr U8 MATERIAL_IDX = 1u;
    static constexpr U8 TEXTURES_IDX = 2u;

#pragma pack(push, 1)
    struct TransformData
    {
        float4 _position; //(w - unused)
        float4 _scale; //(w - unused)
        quatf  _rotation;
    };

    struct NodeTransformData
    {
        TransformData _transform;
        TransformData _prevTransform;
        float4        _boundingSphere;
        float4        _data; //x = animation frame, y = bone count, z = LoDLevel, w = 4x8u: occlusion cull flag, selection flag, unused, unused
    };

    static_assert(sizeof(NodeTransformData) == sizeof(mat4<F32>) * 2, "Wrong Node Transform Data entry size!");

    struct NodeMaterialData
    {
        //base colour
        float4 _albedo;
        //rgb - emissive
        //a   - parallax factor
        float4 _emissiveAndParallax;
        //rgb - ambientColour (Don't really need this. To remove eventually, but since we have the space, might as well)
        //a - specular strength [0...Material::MAX_SHININESS]. Used mainly by Phong shading
        float4 _colourData;
        //x = 4x8U: occlusion, metallic, roughness, isDoubleSided
        //y = 4x8U: specular.r, specular.g, specular.b, use packed OMR
        //z = 4x8U: bump method, shadingMode, reserved, reserved
        //w = Probe lookup index + 1 (0 = sky cubemap)
        uint4 _data;
        //x = 4x8U: tex op Unit0, tex op Unit1, tex op Specular, Emissive
        //y = 4x8U: tex op Occlusion, tex op Metalness, tex op Roughness, tex op Opcaity
        //z = 4x8U: use albedo texture alpha channel, use opacity map alpha channel, specular Factor, gloss Factor
        //w = 4x8u: receives shadows, reserved, reserved, reserved
        uint4 _textureOperations;
    };

    struct NodeIndirectionData
    {
        static constexpr U32 INVALID_IDX = U32_MAX;

        U32 _transformIDX = INVALID_IDX;
        U32 _materialIDX = INVALID_IDX;
        U32 _padding__0 = 0u;
        U32 _padding__1 = 0u;
    };
    [[nodiscard]] size_t HashMaterialData(const NodeMaterialData& dataIn);
#pragma pack(pop)

} //namespace Divide

#endif //DVD_RENDER_PASS_NODE_BUFFERED_DATA_H_
