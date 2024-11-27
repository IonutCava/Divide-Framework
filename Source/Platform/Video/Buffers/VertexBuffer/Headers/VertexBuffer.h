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
#ifndef DVD_VERTEX_BUFFER_OBJECT_H
#define DVD_VERTEX_BUFFER_OBJECT_H

#include "VertexDataInterface.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace Divide
{

class ByteBuffer;
FWD_DECLARE_MANAGED_CLASS(GenericVertexData);
/// Vertex Buffer interface class to allow API-independent implementation of data
/// This class does NOT represent an API-level VB, such as: GL_ARRAY_BUFFER / D3DVERTEXBUFFER
/// It is only a "buffer" for "vertex info" abstract of implementation. (e.g.: OGL uses a vertex array object for this)
class VertexBuffer final : public VertexDataInterface
{
   public:
    constexpr static U32 PRIMITIVE_RESTART_INDEX_L = 0xFFFFFFFF;
    constexpr static U32 PRIMITIVE_RESTART_INDEX_S = 0xFFFF;
    constexpr static U16 INVALID_PARTITION_ID = 0xFFFF;

    struct Vertex
    {
        UColour4 _colour{0u, 0u, 0u, 1u};
        float3   _position{};
        float2   _texcoord{};
        vec4<U8> _weights{0u};
        vec4<U8> _indices{0u};
        F32      _normal{0.f};
        F32      _tangent{0.f};
    };

    struct Descriptor
    {
        Str<256> _name;
        bool     _largeIndices{false};
        bool     _keepCPUData{false};
        bool     _allowDynamicUpdates{false};
    };

    VertexBuffer(GFXDevice& context, const Descriptor& descriptor);

    void reserveIndexCount(const size_t size);

    void setVertexCount(const size_t size);

    void resizeVertexCount( const size_t size, const Vertex& defaultValue);
    inline void resizeVertexCount(const size_t size ) { resizeVertexCount( size, {} ); }

    [[nodiscard]] size_t getVertexCount() const noexcept;

    [[nodiscard]] const vector<Vertex>& getVertices() const noexcept;

    [[nodiscard]] const float3& getPosition(const U32 index) const;

    [[nodiscard]]  float2       getTexCoord(const U32 index) const;

    [[nodiscard]]  F32 getNormal(const U32 index) const;

                   F32 getNormal(const U32 index, float3& normalOut) const;

    [[nodiscard]] F32 getTangent(const U32 index) const;

    [[nodiscard]] F32 getTangent(const U32 index, float3& tangentOut) const;

    [[nodiscard]] vec4<U8> getBoneIndices(const U32 index) const;

    [[nodiscard]] vec4<U8> getBoneWeightsPacked(const U32 index) const;

    [[nodiscard]] float4 getBoneWeights(const U32 index) const;

    [[nodiscard]] size_t getIndexCount() const noexcept;

    [[nodiscard]] U32 getIndex(const size_t index) const;

    [[nodiscard]] const vector<U32>& getIndices() const noexcept;

    void addIndex(const U32 index);

    void addIndices(const vector<U16>& indices);

    void addIndices(const vector<U32>& indices);

    void addRestartIndex();

    void modifyPositionValues(const U32 indexOffset, const vector<float3>& newValues);

    void modifyPositionValue(const U32 index, const float3& newValue);

    void modifyPositionValue(const U32 index, const F32 x, const F32 y, const F32 z);

    void modifyColourValue(const U32 index, const UColour4& newValue);

    void modifyColourValue(const U32 index, const U8 r, const U8 g, const U8 b, const U8 a);

    void modifyNormalValue(const U32 index, const float3& newValue);

    void modifyNormalValue(const U32 index, const F32 x, const F32 y, const F32 z);

    void modifyTangentValue(const U32 index, const float3& newValue);

    void modifyTangentValue(const U32 index, const F32 x, const F32 y, const F32 z);

    void modifyTexCoordValue(const U32 index, float2 newValue);

    void modifyTexCoordValue(const U32 index, const F32 s, const F32 t);

    void modifyBoneIndices(const U32 index, const vec4<U8> indices);

    void modifyBoneWeights(const U32 index, const FColour4& weights);

    void modifyBoneWeights(const U32 index, const vec4<U8> packedWeights);

    [[nodiscard]] size_t partitionCount() const noexcept;

    U16 partitionBuffer();

    size_t getPartitionIndexCount(const U16 partitionID);

    [[nodiscard]] size_t getPartitionOffset(const U16 partitionID) const;

    [[nodiscard]] size_t lastPartitionOffset() const;

    [[nodiscard]] AttributeMap generateAttributeMap();

    void reset();

    void fromBuffer(const VertexBuffer& other);
    bool deserialize(ByteBuffer& dataIn);
    bool serialize(ByteBuffer& dataOut) const;

    void computeNormals();
    void computeTangents();

    PROPERTY_R_IW(size_t, firstIndexOffsetCount, 0u);

   protected:
    /// Returns true if data was updated
    bool refresh(size_t& indexOffsetCountOut, BufferLock& dataLockOut, BufferLock& indexLockOut);

    bool getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, size_t dataOutBufferLength);
    /// Calculates the appropriate attribute offsets and returns the total size of a vertex for this buffer
    void draw(const GenericDrawCommand& command, VDIUserData* data) override;

    [[nodiscard]] static size_t GetTotalDataSize(const AttributeFlags& usedAttributes);
    [[nodiscard]] static AttributeOffsets GetAttributeOffsets(const AttributeFlags& usedAttributes, size_t& totalDataSizeOut);

   protected:
    Descriptor _descriptor;
    // first: offset, second: count
    vector<std::pair<size_t, size_t>> _partitions;
    vector<Vertex> _data;
    /// Used for creating an "IB". If it's empty, then an outside source should provide the indices
    vector<U32> _indices;
    AttributeFlags _useAttribute{};
    GenericVertexData_ptr _internalGVD = nullptr;
    bool _refreshQueued = false;
    bool _dataLayoutChanged = false;
    bool _indicesChanged = true;
};

FWD_DECLARE_MANAGED_CLASS(VertexBuffer);

};  // namespace Divide

#endif //DVD_VERTEX_BUFFER_OBJECT_H
