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
#ifndef _VERTEX_BUFFER_OBJECT_H
#define _VERTEX_BUFFER_OBJECT_H

#include "VertexDataInterface.h"
#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace Divide {
class ByteBuffer;
FWD_DECLARE_MANAGED_CLASS(GenericVertexData);
/// Vertex Buffer interface class to allow API-independent implementation of data
/// This class does NOT represent an API-level VB, such as: GL_ARRAY_BUFFER / D3DVERTEXBUFFER
/// It is only a "buffer" for "vertex info" abstract of implementation. (e.g.: OGL uses a vertex array object for this)
class VertexBuffer final : public VertexDataInterface {
   public:
    constexpr static U32 PRIMITIVE_RESTART_INDEX_L = 0xFFFFFFFF;
    constexpr static U32 PRIMITIVE_RESTART_INDEX_S = 0xFFFF;
    constexpr static U16 INVALID_PARTITION_ID = 0xFFFF;

    struct Vertex {
        UColour4  _colour;
        vec3<F32> _position;
        vec2<F32> _texcoord;
        P32       _weights = {0u};
        P32       _indices = {0u};
        F32       _normal = 0.f;
        F32       _tangent = 0.f;

        bool operator==(const Vertex& other) const noexcept {
            return _position == other._position &&
                   COMPARE(_normal, other._normal) &&
                   COMPARE(_tangent, other._tangent) &&
                   _colour == other._colour &&
                   _texcoord == other._texcoord &&
                   _weights.i  == other._weights.i &&
                   _indices.i == other._indices.i;
        }

        bool operator!=(const Vertex& other) const noexcept {
            return _position != other._position ||
                   !COMPARE(_normal, other._normal) ||
                   !COMPARE(_tangent, other._tangent) ||
                   _colour != other._colour ||
                   _texcoord != other._texcoord ||
                   _weights.i != other._weights.i ||
                   _indices.i != other._indices.i;
        }
    };

    VertexBuffer(GFXDevice& context);
    ~VertexBuffer() = default;

    bool create(bool staticDraw, bool keepData);

    void setVertexCount(const size_t size) {
        _dataLayoutChanged = true;
        _data.resize(size);
    }

    [[nodiscard]] size_t getVertexCount() const noexcept {
        return _data.size();
    }

    [[nodiscard]] const vector<Vertex>& getVertices() const noexcept {
        return _data;
    }

    void reserveIndexCount(const size_t size) {
        _indices.reserve(size);
    }

    void resizeVertexCount(const size_t size, const Vertex& defaultValue = {}) {
        _dataLayoutChanged = true;
        _data.resize(size, defaultValue);
    }

    [[nodiscard]] const vec3<F32>& getPosition(const U32 index) const {
        return _data[index]._position;
    }

    [[nodiscard]] const vec2<F32>& getTexCoord(const U32 index) const {
        return _data[index]._texcoord;
    }

    [[nodiscard]] F32 getNormal(const U32 index) const {
        return _data[index]._normal;
    }

    F32 getNormal(const U32 index, vec3<F32>& normalOut) const {
        const F32 normal = getNormal(index);
        Util::UNPACK_VEC3(normal, normalOut.x, normalOut.y, normalOut.z);
        return normal;
    }

    [[nodiscard]] F32 getTangent(const U32 index) const {
        return _data[index]._tangent;
    }

    [[nodiscard]] F32 getTangent(const U32 index, vec3<F32>& tangentOut) const {
        const F32 tangent = getTangent(index);
        Util::UNPACK_VEC3(tangent, tangentOut.x, tangentOut.y, tangentOut.z);
        return tangent;
    }

    [[nodiscard]] P32 getBoneIndices(const U32 index) const {
        return _data[index]._indices;
    }

    [[nodiscard]] P32 getBoneWeightsPacked(const U32 index) const {
        return _data[index]._weights;
    }

    [[nodiscard]] vec4<F32> getBoneWeights(const U32 index) const {
        const P32& weight = _data[index]._weights;
        return vec4<F32>(UNORM_CHAR_TO_FLOAT(weight.b[0]),
                         UNORM_CHAR_TO_FLOAT(weight.b[1]),
                         UNORM_CHAR_TO_FLOAT(weight.b[2]),
                         UNORM_CHAR_TO_FLOAT(weight.b[3]));
    }

    [[nodiscard]] size_t getIndexCount() const noexcept {
        return _indices.size();
    }

    [[nodiscard]] U32 getIndex(const size_t index) const {
        assert(index < getIndexCount());
        return _indices[index];
    }

    [[nodiscard]] const vector<U32>& getIndices() const noexcept {
        return _indices;
    }

    void addIndex(const U32 index) {
        assert(useLargeIndices() || index <= U16_MAX);
        _indices.push_back(index);
        _indicesChanged = true;
    }

    template <typename T>
    void addIndices(const vector_fast<T>& indices, const bool containsRestartIndex) {
        eastl::transform(eastl::cbegin(indices),
                         eastl::cend(indices),
                         back_inserter(_indices),
                         static_caster<T, U32>());

        if (containsRestartIndex) {
            hasRestartIndex(true);
        }
        _indicesChanged = true;
    }

    void addRestartIndex() {
        primitiveRestartEnabled(true);
        addIndex(useLargeIndices() ? PRIMITIVE_RESTART_INDEX_L : PRIMITIVE_RESTART_INDEX_S);
     }

    void modifyPositionValues(const U32 indexOffset, const vector<vec3<F32>>& newValues) {
       assert(indexOffset + newValues.size() - 1 < _data.size());
       DIVIDE_ASSERT(_staticBuffer == false ||
           _staticBuffer == true && !_data.empty(),
           "VertexBuffer error: Modifying static buffers after creation is not allowed!");

       vector<Vertex>::iterator it = _data.begin() + indexOffset;
       for (const vec3<F32>& value : newValues) {
            it++->_position.set(value);
       }

       _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::POSITION)];
       _useAttribute[to_base(AttribLocation::POSITION)] = true;
       _refreshQueued = true;
    }

    void modifyPositionValue(const U32 index, const vec3<F32>& newValue) {
        modifyPositionValue(index, newValue.x, newValue.y, newValue.z);
    }

    void modifyPositionValue(const U32 index, const F32 x, const F32 y, const F32 z) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._position.set(x, y, z);
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::POSITION)];
        _useAttribute[to_base(AttribLocation::POSITION)] = true;
        _refreshQueued = true;
    }

    void modifyColourValue(const U32 index, const UColour4& newValue) {
        modifyColourValue(index, newValue.r, newValue.g, newValue.b, newValue.a);
    }

    void modifyColourValue(const U32 index, const U8 r, const U8 g, const U8 b, const U8 a) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._colour.set(r, g, b, a);
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::COLOR)];
        _useAttribute[to_base(AttribLocation::COLOR)] = true;
        _refreshQueued = true;
    }

    void modifyNormalValue(const U32 index, const vec3<F32>& newValue) {
        modifyNormalValue(index, newValue.x, newValue.y, newValue.z);
    }

    void modifyNormalValue(const U32 index, const F32 x, const F32 y, const F32 z) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._normal = Util::PACK_VEC3(x, y, z);
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::NORMAL)];
        _useAttribute[to_base(AttribLocation::NORMAL)] = true;
        _refreshQueued = true;
    }

    void modifyTangentValue(const U32 index, const vec3<F32>& newValue) {
        modifyTangentValue(index, newValue.x, newValue.y, newValue.z);
    }

    void modifyTangentValue(const U32 index, const F32 x, const F32 y, const F32 z) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                     _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._tangent = Util::PACK_VEC3(x, y, z);
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::TANGENT)];
        _useAttribute[to_base(AttribLocation::TANGENT)] = true;
        _refreshQueued = true;
    }

    void modifyTexCoordValue(const U32 index, const vec2<F32>& newValue) {
        modifyTexCoordValue(index, newValue.s, newValue.t);
    }

    void modifyTexCoordValue(const U32 index, const F32 s, const F32 t) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._texcoord.set(s, t);
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::TEXCOORD)];
        _useAttribute[to_base(AttribLocation::TEXCOORD)] = true;
        _refreshQueued = true;
    }

    void modifyBoneIndices(const U32 index, const P32 indices) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._indices = indices;
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::BONE_INDICE)];
        _useAttribute[to_base(AttribLocation::BONE_INDICE)] = true;
        _refreshQueued = true;
    }

    void modifyBoneWeights(const U32 index, const FColour4& weights) {
        P32 boneWeights;
        boneWeights.b[0] = FLOAT_TO_CHAR_UNORM(weights.x);
        boneWeights.b[1] = FLOAT_TO_CHAR_UNORM(weights.y);
        boneWeights.b[2] = FLOAT_TO_CHAR_UNORM(weights.z);
        boneWeights.b[3] = FLOAT_TO_CHAR_UNORM(weights.w);
        modifyBoneWeights(index, boneWeights);
    }

    void modifyBoneWeights(const U32 index, const P32 packedWeights) {
        assert(index < _data.size());

        DIVIDE_ASSERT(_staticBuffer == false ||
                      _staticBuffer == true && !_data.empty(),
                      "VertexBuffer error: Modifying static buffers after creation is not allowed!");

        _data[index]._weights = packedWeights;
        _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::BONE_WEIGHT)];
        _useAttribute[to_base(AttribLocation::BONE_WEIGHT)] = true;
        _refreshQueued = true;
    }

    [[nodiscard]] size_t partitionCount() const noexcept {
        return _partitions.size();
    }

    U16 partitionBuffer() {
        const size_t previousIndexCount = _partitions.empty() ? 0 : _partitions.back().second;
        const size_t previousOffset = _partitions.empty() ? 0 : _partitions.back().first;
        size_t partitionedIndexCount = previousIndexCount + previousOffset;
        _partitions.emplace_back(partitionedIndexCount, getIndexCount() - partitionedIndexCount);
        return to_U16(_partitions.size() - 1);
    }

    size_t getPartitionIndexCount(const U16 partitionID) {
        if (_partitions.empty()) {
            return getIndexCount();
        }
        assert(partitionID < _partitions.size() && "VertexBuffer error: Invalid partition offset!");
        return _partitions[partitionID].second;
    }

    [[nodiscard]] size_t getPartitionOffset(const U16 partitionID) const {
        if (_partitions.empty()) {
            return 0;
        }
        assert(partitionID < _partitions.size() && "VertexBuffer error: Invalid partition offset!");
        return _partitions[partitionID].first;
    }

    [[nodiscard]] size_t lastPartitionOffset() const {
        if (_partitions.empty()) {
            return 0;
        }
        return getPartitionOffset(to_U16(_partitions.size() - 1));
    }

    [[nodiscard]] AttributeMap generateAttributeMap();

    void reset();

    void fromBuffer(const VertexBuffer& other);
    bool deserialize(ByteBuffer& dataIn);
    bool serialize(ByteBuffer& dataOut) const;

    void computeNormals();
    void computeTangents();

    /// Flag used to prevent clearing of the _data vector for static buffers
    PROPERTY_RW(bool, useLargeIndices, false);
    /// If this flag is true, no further modification are allowed on the buffer (static geometry)
    PROPERTY_R_IW(bool, staticBuffer, false);

   protected:

    void refresh();

    bool getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, size_t dataOutBufferLength);
    /// Calculates the appropriate attribute offsets and returns the total size of a vertex for this buffer
    void draw(const GenericDrawCommand& command) override;

    static [[nodiscard]] AttributeOffsets GetAttributeOffsets(const AttributeFlags& usedAttributes, size_t& totalDataSizeOut);

   protected:
    // first: offset, second: count
    vector<std::pair<size_t, size_t>> _partitions;
    vector<Vertex> _data;
    /// Used for creating an "IB". If it's empty, then an outside source should provide the indices
    vector<U32> _indices;
    AttributeFlags _useAttribute{};
    size_t _effectiveEntrySize = 0u;
    GenericVertexData_ptr _internalGVD = nullptr;
    bool _refreshQueued = false;
    bool _dataLayoutChanged = false;
    bool _indicesChanged = true;
    bool _keepData = false;
};

FWD_DECLARE_MANAGED_CLASS(VertexBuffer);

};  // namespace Divide
#endif
