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
#ifndef _PARTICLE_DATA_H_
#define _PARTICLE_DATA_H_

#include "Graphs/Headers/SceneNode.h"

namespace Divide {

enum class ParticleDataProperties : U8
{
    PROPERTIES_POS = toBit(1),
    PROPERTIES_VEL = toBit(2),
    PROPERTIES_ACC = toBit(3),
    PROPERTIES_COLOR = toBit(4),
    PROPERTIES_COLOR_TRANS = toBit(5),
    COUNT = 5
};

namespace Names {
    static const char* particleDataProperties[] = {
          "Position", "Velocity", "Acceleration", "Colour", "Colour transform", "UNKNOWN"
    };
}

/// Container to store data for a given set of particles
class ParticleData {
   public:
    static constexpr U32 g_threadPartitionSize = 256;

    /// helper array used for sorting
    vector_fast<std::pair<U32, F32>> _indices;
    vector<vec4<F32>> _renderingPositions;
    vector<UColour4>  _renderingColours;
    /// x,y,z = position; w = size
    vector<vec4<F32>> _position;
    /// x,y,z = _velocity; w = angle;
    vector<vec4<F32>> _velocity;
    /// x,y,z = _acceleration; w = weight;
    vector<vec4<F32>> _acceleration;
    /// x = time; y = interpolation; z = 1 / time;  w = distance to camera sq;
    vector<vec4<F32>> _misc;
    /// r,g,b,a = colour and transparency
    vector<FColour4> _colour;
    /// r,g,b,a = colour and transparency
    vector<FColour4> _startColour;
    /// r,g,b,a = colour and transparency
    vector<FColour4> _endColour;
    /// Location of the texture file. Leave blank for colour only
    string _textureFileName;


    void setParticleGeometry(const vector<vec3<F32>>& particleGeometryVertices,
                             const vector<U32>& particleGeometryIndices,
                             PrimitiveTopology particleGeometryType);

    void setBillboarded(bool state) noexcept;

    [[nodiscard]] PrimitiveTopology particleGeometryType() const noexcept {
        return _particleGeometryType;
    }

    [[nodiscard]] const vector<vec3<F32>>& particleGeometryVertices() const noexcept {
        return _particleGeometryVertices;
    }

    [[nodiscard]] const vector<U32>& particleGeometryIndices() const noexcept {
        return _particleGeometryIndices;
    }

    [[nodiscard]] bool isBillboarded() const noexcept {
        return _isBillboarded;
    }

    PROPERTY_RW(U32, optionsMask, 0u);
   public:
    explicit ParticleData(GFXDevice& context, U32 particleCount, U32 optionsMask);
    ~ParticleData();

    void generateParticles(U32 particleCount, U32 optionsMask);
    void kill(U32 index);
    void wake(U32 index);
    void swapData(U32 indexA, U32 indexB);

    [[nodiscard]] U32 aliveCount() const noexcept { return _aliveCount; }
    [[nodiscard]] U32 totalCount() const noexcept { return _totalCount; }
    
    /// Sort ALIVE particles only
    void sort();

   protected:
    U32 _totalCount = 0u;
    U32 _aliveCount = 0u;

    bool _isBillboarded = true;
    vector<vec3<F32>> _particleGeometryVertices;
    vector<U32> _particleGeometryIndices;
    PrimitiveTopology _particleGeometryType = PrimitiveTopology::COUNT;

    GFXDevice& _context;
};

}  // namespace Divide

#endif