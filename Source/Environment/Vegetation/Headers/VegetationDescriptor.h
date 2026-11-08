/*
   Copyright (c) 2018-2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

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

#ifndef DVD_VEGETATION_DESCRIPTOR_H_
#define DVD_VEGETATION_DESCRIPTOR_H_

#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{

class Terrain;
class Vegetation;

constexpr size_t MAX_VEGETATION_TREE_MESHES = 4u;

template<>
struct PropertyDescriptor<Vegetation>
{
    std::shared_ptr<ImageTools::ImageData> grassMap;
    std::shared_ptr<ImageTools::ImageData> grassMapSecondary;
    std::shared_ptr<ImageTools::ImageData> treeMap;
    U8 grassMapChannelMask = 15u;
    U8 grassMapSecondaryChannelMask = 15u;
    vector<Str<256>> treeMeshes;
    Str<256> name = "";
    string billboardTextureArray = "";
    std::array<float3, MAX_VEGETATION_TREE_MESHES> treeRotations;
    float4 grassScales = VECTOR4_UNIT;
    float4 treeScales = VECTOR4_UNIT;
    float4 treeDensities = VECTOR4_UNIT;
    Terrain* parentTerrain = nullptr;
    U32 chunkSize = 0u;
};

using VegetationDescriptor = PropertyDescriptor<Vegetation>;

[[nodiscard]] inline UColour4 GrassPlacementColour( const UColour4& primary, const U8 primaryMask,
                                                   const UColour4& secondary, const U8 secondaryMask ) noexcept
{
    UColour4 colour{ 0u, 0u, 0u, 0u };
    for ( U8 channel = 0u; channel < 4u; ++channel )
    {
        colour[channel] = std::max( (primaryMask & (1u << channel)) != 0u ? primary[channel] : U8{ 0u },
                                    (secondaryMask & (1u << channel)) != 0u ? secondary[channel] : U8{ 0u } );
    }
    return colour;
}

template<>
inline size_t GetHash( const VegetationDescriptor& descriptor ) noexcept;

size_t GetHash( Terrain* terrain ) noexcept;

} //namespace Divide

#endif //DVD_VEGETATION_DESCRIPTOR_H_

#include "VegetationDescriptor.inl"
