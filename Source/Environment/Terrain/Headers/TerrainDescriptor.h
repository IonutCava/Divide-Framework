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
#ifndef DVD_TERRAIN_DESCRIPTOR_H_
#define DVD_TERRAIN_DESCRIPTOR_H_

#include "Utility/Headers/XMLParser.h"
#include "Core/Resources/Headers/Resource.h"

namespace Divide {

class Terrain;

template<>
struct PropertyDescriptor<Terrain>
{
    using LayerDataEntry = std::array<float2, 4>;
    using LayerData = std::array<LayerDataEntry, 8>;

    hashMap<U64, string> _variables{};
    hashMap<U64, F32>    _variablesf{};
    LayerData            _layerDataEntries{};
    std::array<U8, 16>   _ringTileCount = {};
    string               _name;
    float2            _altitudeRange{0.f, 1.f};
    vec2<U16>            _dimensions{ U16_ONE, U16_ONE };
    F32                  _startWidth{0.25f};
    U8                   _ringCount{4u};
    U8                   _textureLayers{1u};
    bool                 _active{false};
};

using TerrainDescriptor = PropertyDescriptor<Terrain>;

template<>
inline size_t GetHash( const TerrainDescriptor& descriptor ) noexcept;

void Init( TerrainDescriptor& descriptor, std::string_view name );

[[nodiscard]] bool LoadFromXML( TerrainDescriptor& descriptor, const boost::property_tree::ptree& pt, std::string_view name );
              void SaveToXML( const TerrainDescriptor& descriptor, boost::property_tree::ptree& pt);

void AddVariable( TerrainDescriptor& descriptor, std::string_view name, std::string_view value );
[[nodiscard]] string GetVariable( const TerrainDescriptor& descriptor, std::string_view name );

void AddVariableF( TerrainDescriptor& descriptor, std::string_view name, F32 value );
[[nodiscard]] F32    GetVariableF( const TerrainDescriptor& descriptor, std::string_view name );

[[nodiscard]] U32    MaxNodesPerStage( const TerrainDescriptor& descriptor ) noexcept;
[[nodiscard]] U8     TileRingCount( const TerrainDescriptor& descriptor, const U8 index ) noexcept;

}  // namespace Divide

#endif //DVD_TERRAIN_DESCRIPTOR_H_

#include "TerrainDescriptor.inl"
