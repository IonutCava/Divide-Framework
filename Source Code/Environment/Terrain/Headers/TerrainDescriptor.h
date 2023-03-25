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
#ifndef _TERRAIN_DESCRIPTOR_H_
#define _TERRAIN_DESCRIPTOR_H_

#include "Core/Resources/Headers/ResourceDescriptor.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide {

class TerrainDescriptor final : public PropertyDescriptor {
   public:
    explicit TerrainDescriptor(std::string_view name) noexcept;
    virtual ~TerrainDescriptor();

    bool loadFromXML(const boost::property_tree::ptree& pt, const string& name);

    void addVariable(const string& name, const string& value);
    void addVariable(const string& name, F32 value);

    [[nodiscard]] string getVariable(const string& name) const {
        const hashMap<U64, string>::const_iterator it = _variables.find(_ID(name.c_str()));
        if (it != std::end(_variables)) {
            return it->second;
        }
        return "";
    }

    [[nodiscard]] F32 getVariablef(const string& name) const {
        const hashMap<U64, F32>::const_iterator it = _variablesf.find(_ID(name.c_str()));
        if (it != std::end(_variablesf)) {
            return it->second;
        }
        return 0.0f;
    }

    [[nodiscard]] U32 maxNodesPerStage() const noexcept {
        // Quadtree, so assume worst case scenario
        return dimensions().maxComponent() / 4;
    }

    [[nodiscard]] U8 tileRingCount(const U8 index) const noexcept {
        assert(index < ringCount());

        return _ringTileCount[index];
    }

    [[nodiscard]] size_t getHash() const noexcept override
    {
        _hash = PropertyDescriptor::getHash();
        for (const auto& it : _variables) {
            Util::Hash_combine(_hash, it.first, it.second);
        }
        for (hashMap<U64, F32>::value_type it : _variablesf) {
            Util::Hash_combine(_hash, it.first, it.second);
        }
        Util::Hash_combine(_hash, _active,
                                  _textureLayers,
                                  _altitudeRange.x,
                                  _altitudeRange.y,
                                  _dimensions.x,
                                  _dimensions.y,
                                  _startWidth);
        for(U8 i = 0; i < ringCount(); ++i) {
            Util::Hash_combine(_hash, _ringTileCount[i]);
        }
        return _hash;
    }

private:
    hashMap<U64, string> _variables{};
    hashMap<U64, F32> _variablesf{};
    string _name;

protected:
    friend class Terrain;
    friend class TerrainLoader;
    using LayerDataEntry = std::array<vec2<F32>, 4>;
    using LayerData = std::array<LayerDataEntry, 8>;

    //x - chunk size, y - patch size in meters
    PROPERTY_RW(LayerData, layerDataEntries);
    PROPERTY_RW(vec2<F32>, altitudeRange);
    PROPERTY_RW(vec2<U16>, dimensions, { 1 });
    PROPERTY_RW(F32, startWidth, 0.25f);
    PROPERTY_RW(U8, ringCount, 4);
    PROPERTY_RW(U8, textureLayers, 1u);
    PROPERTY_RW(bool, active, false);
    std::array<U8, 16> _ringTileCount = {};


};

}  // namespace Divide

#endif