#include "stdafx.h"

#include "Headers/VertexBuffer.h"

#include "Core/Headers/ByteBuffer.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
constexpr U16 BYTE_BUFFER_VERSION = 1u;


namespace {
// Once vertex buffers reach a certain size, the for loop grows really really fast up to millions of iterations.
// Multiple if-checks per loop are not an option, so do some template hacks to speed this function up
template<bool TexCoords, bool Normals, bool Tangents, bool Colour, bool Bones>
void FillSmallData5(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut) noexcept
{
    for (const VertexBuffer::Vertex& data : dataIn) {
        std::memcpy(dataOut, data._position._v, sizeof data._position);
        dataOut += sizeof data._position;

        if_constexpr (TexCoords) {
            std::memcpy(dataOut, data._texcoord._v, sizeof data._texcoord);
            dataOut += sizeof data._texcoord;
        }

        if_constexpr(Normals) {
            std::memcpy(dataOut, &data._normal, sizeof data._normal);
            dataOut += sizeof data._normal;
        }

        if_constexpr(Tangents) {
            std::memcpy(dataOut, &data._tangent, sizeof data._tangent);
            dataOut += sizeof data._tangent;
        }

        if_constexpr(Colour) {
            std::memcpy(dataOut, data._colour._v, sizeof data._colour);
            dataOut += sizeof data._colour;
        }

        if_constexpr(Bones) {
            std::memcpy(dataOut, &data._weights.i, sizeof data._weights.i);
            dataOut += sizeof data._weights.i;

            std::memcpy(dataOut, &data._indices.i, sizeof data._indices.i);
            dataOut += sizeof data._indices.i;
        }
    }
}

template <bool TexCoords, bool Normals, bool Tangents, bool Colour>
void FillSmallData4(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut, const bool bones) noexcept
{
    if (bones) {
        FillSmallData5<TexCoords, Normals, Tangents, Colour, true>(dataIn, dataOut);
    } else {
        FillSmallData5<TexCoords, Normals, Tangents, Colour, false>(dataIn, dataOut);
    }
}

template <bool TexCoords, bool Normals, bool Tangents>
void FillSmallData3(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut, const bool colour, const bool bones) noexcept
{
    if (colour) {
        FillSmallData4<TexCoords, Normals, Tangents, true>(dataIn, dataOut, bones);
    } else {
        FillSmallData4<TexCoords, Normals, Tangents, false>(dataIn, dataOut, bones);
    }
}

template <bool TexCoords, bool Normals>
void FillSmallData2(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut, const bool tangents, const bool colour, const bool bones) noexcept
{
    if (tangents) {
        FillSmallData3<TexCoords, Normals, true>(dataIn, dataOut, colour, bones);
    } else {
        FillSmallData3<TexCoords, Normals, false>(dataIn, dataOut, colour, bones);
    }
}

template <bool TexCoords>
void FillSmallData1(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut, const bool normals, const bool tangents, const bool colour, const bool bones) noexcept
{
    if (normals) {
        FillSmallData2<TexCoords, true>(dataIn, dataOut, tangents, colour, bones);
    } else {
        FillSmallData2<TexCoords, false>(dataIn, dataOut, tangents, colour, bones);
    }
}

void FillSmallData(const vector<VertexBuffer::Vertex>& dataIn, Byte* dataOut, const bool texCoords, const bool normals, const bool tangents, const bool colour, const bool bones) noexcept
{
    if (texCoords) {
        FillSmallData1<true>(dataIn, dataOut, normals, tangents, colour, bones);
    } else {
        FillSmallData1<false>(dataIn, dataOut, normals, tangents, colour, bones);
    }
}

} //namespace

VertexBuffer::VertexBuffer(GFXDevice& context)
    : VertexDataInterface(context, nullptr)
    , _internalGVD(context.newGVD(1u))
{
}

bool VertexBuffer::create(const bool staticDraw, const bool keepData) {
    const AttributeOffsets offsets = GetAttributeOffsets(_useAttribute, _effectiveEntrySize);
    DIVIDE_ASSERT(!_data.empty() && _effectiveEntrySize > 0u, Locale::Get(_ID("ERROR_VB_POSITION")));

    _staticBuffer = staticDraw;
    _keepData = keepData;

    _refreshQueued = true;

    return true;
}

void VertexBuffer::setVertexCount(const size_t size) {
    _dataLayoutChanged = true;
    _data.resize(size);
}

size_t VertexBuffer::getVertexCount() const noexcept {
    return _data.size();
}


const vector<VertexBuffer::Vertex>& VertexBuffer::getVertices() const noexcept {
    return _data;
}

void VertexBuffer::reserveIndexCount(const size_t size) {
    _indices.reserve(size);
}

void VertexBuffer::resizeVertexCount(const size_t size, const Vertex& defaultValue) {
    _dataLayoutChanged = true;
    _data.resize(size, defaultValue);
}

const vec3<F32>& VertexBuffer::getPosition(const U32 index) const {
    return _data[index]._position;
}

const vec2<F32>& VertexBuffer::getTexCoord(const U32 index) const {
    return _data[index]._texcoord;
}

F32 VertexBuffer::getNormal(const U32 index) const {
    return _data[index]._normal;
}

F32 VertexBuffer::getNormal(const U32 index, vec3<F32>& normalOut) const {
    const F32 normal = getNormal(index);
    Util::UNPACK_VEC3(normal, normalOut.x, normalOut.y, normalOut.z);
    return normal;
}

F32 VertexBuffer::getTangent(const U32 index) const {
    return _data[index]._tangent;
}

F32 VertexBuffer::getTangent(const U32 index, vec3<F32>& tangentOut) const {
    const F32 tangent = getTangent(index);
    Util::UNPACK_VEC3(tangent, tangentOut.x, tangentOut.y, tangentOut.z);
    return tangent;
}

P32 VertexBuffer::getBoneIndices(const U32 index) const {
    return _data[index]._indices;
}

P32 VertexBuffer::getBoneWeightsPacked(const U32 index) const {
    return _data[index]._weights;
}

vec4<F32> VertexBuffer::getBoneWeights(const U32 index) const {
    const P32& weight = _data[index]._weights;
    return vec4<F32>(UNORM_CHAR_TO_FLOAT(weight.b[0]),
        UNORM_CHAR_TO_FLOAT(weight.b[1]),
        UNORM_CHAR_TO_FLOAT(weight.b[2]),
        UNORM_CHAR_TO_FLOAT(weight.b[3]));
}

size_t VertexBuffer::getIndexCount() const noexcept {
    return _indices.size();
}

U32 VertexBuffer::getIndex(const size_t index) const {
    assert(index < getIndexCount());
    return _indices[index];
}

const vector<U32>& VertexBuffer::getIndices() const noexcept {
    return _indices;
}

void VertexBuffer::addIndex(const U32 index) {
    assert(useLargeIndices() || index <= U16_MAX);
    _indices.push_back(index);
    _indicesChanged = true;
}

void VertexBuffer::addIndices(const vector_fast<U16>& indices) {
    eastl::transform(eastl::cbegin(indices),
        eastl::cend(indices),
        back_inserter(_indices),
        static_caster<U16, U32>());

    _indicesChanged = true;
}

void VertexBuffer::addIndices(const vector_fast<U32>& indices) {
    _indices.insert(eastl::cend(_indices), eastl::cbegin(indices), eastl::cend(_indices));
    _indicesChanged = true;
}

void VertexBuffer::addRestartIndex() {
    primitiveRestartRequired(true);
    addIndex(useLargeIndices() ? PRIMITIVE_RESTART_INDEX_L : PRIMITIVE_RESTART_INDEX_S);
}

void VertexBuffer::modifyPositionValues(const U32 indexOffset, const vector<vec3<F32>>& newValues) {
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

void VertexBuffer::modifyPositionValue(const U32 index, const vec3<F32>& newValue) {
    modifyPositionValue(index, newValue.x, newValue.y, newValue.z);
}

void VertexBuffer::modifyPositionValue(const U32 index, const F32 x, const F32 y, const F32 z) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._position.set(x, y, z);
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::POSITION)];
    _useAttribute[to_base(AttribLocation::POSITION)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyColourValue(const U32 index, const UColour4& newValue) {
    modifyColourValue(index, newValue.r, newValue.g, newValue.b, newValue.a);
}

void VertexBuffer::modifyColourValue(const U32 index, const U8 r, const U8 g, const U8 b, const U8 a) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._colour.set(r, g, b, a);
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::COLOR)];
    _useAttribute[to_base(AttribLocation::COLOR)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyNormalValue(const U32 index, const vec3<F32>& newValue) {
    modifyNormalValue(index, newValue.x, newValue.y, newValue.z);
}

void VertexBuffer::modifyNormalValue(const U32 index, const F32 x, const F32 y, const F32 z) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._normal = Util::PACK_VEC3(x, y, z);
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::NORMAL)];
    _useAttribute[to_base(AttribLocation::NORMAL)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyTangentValue(const U32 index, const vec3<F32>& newValue) {
    modifyTangentValue(index, newValue.x, newValue.y, newValue.z);
}

void VertexBuffer::modifyTangentValue(const U32 index, const F32 x, const F32 y, const F32 z) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._tangent = Util::PACK_VEC3(x, y, z);
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::TANGENT)];
    _useAttribute[to_base(AttribLocation::TANGENT)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyTexCoordValue(const U32 index, const vec2<F32>& newValue) {
    modifyTexCoordValue(index, newValue.s, newValue.t);
}

void VertexBuffer::modifyTexCoordValue(const U32 index, const F32 s, const F32 t) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._texcoord.set(s, t);
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::TEXCOORD)];
    _useAttribute[to_base(AttribLocation::TEXCOORD)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyBoneIndices(const U32 index, const P32 indices) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._indices = indices;
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::BONE_INDICE)];
    _useAttribute[to_base(AttribLocation::BONE_INDICE)] = true;
    _refreshQueued = true;
}

void VertexBuffer::modifyBoneWeights(const U32 index, const FColour4& weights) {
    P32 boneWeights;
    boneWeights.b[0] = FLOAT_TO_CHAR_UNORM(weights.x);
    boneWeights.b[1] = FLOAT_TO_CHAR_UNORM(weights.y);
    boneWeights.b[2] = FLOAT_TO_CHAR_UNORM(weights.z);
    boneWeights.b[3] = FLOAT_TO_CHAR_UNORM(weights.w);
    modifyBoneWeights(index, boneWeights);
}

void VertexBuffer::modifyBoneWeights(const U32 index, const P32 packedWeights) {
    assert(index < _data.size());

    DIVIDE_ASSERT(_staticBuffer == false ||
        _staticBuffer == true && !_data.empty(),
        "VertexBuffer error: Modifying static buffers after creation is not allowed!");

    _data[index]._weights = packedWeights;
    _dataLayoutChanged = _dataLayoutChanged || !_useAttribute[to_base(AttribLocation::BONE_WEIGHT)];
    _useAttribute[to_base(AttribLocation::BONE_WEIGHT)] = true;
    _refreshQueued = true;
}

size_t VertexBuffer::partitionCount() const noexcept {
    return _partitions.size();
}

U16 VertexBuffer::partitionBuffer() {
    const size_t previousIndexCount = _partitions.empty() ? 0 : _partitions.back().second;
    const size_t previousOffset = _partitions.empty() ? 0 : _partitions.back().first;
    size_t partitionedIndexCount = previousIndexCount + previousOffset;
    _partitions.emplace_back(partitionedIndexCount, getIndexCount() - partitionedIndexCount);
    return to_U16(_partitions.size() - 1);
}

size_t VertexBuffer::getPartitionIndexCount(const U16 partitionID) {
    if (_partitions.empty()) {
        return getIndexCount();
    }
    assert(partitionID < _partitions.size() && "VertexBuffer error: Invalid partition offset!");
    return _partitions[partitionID].second;
}

size_t VertexBuffer::getPartitionOffset(const U16 partitionID) const {
    if (_partitions.empty()) {
        return 0;
    }
    assert(partitionID < _partitions.size() && "VertexBuffer error: Invalid partition offset!");
    return _partitions[partitionID].first;
}

size_t VertexBuffer::lastPartitionOffset() const {
    if (_partitions.empty()) {
        return 0;
    }
    return getPartitionOffset(to_U16(_partitions.size() - 1));
}

/// Trim down the Vertex vector to only upload the minimal amount of data to the GPU
bool VertexBuffer::getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, const size_t dataOutBufferLength) {
    assert(dataOut != nullptr);

    if (dataOutBufferLength >= dataIn.size() * _effectiveEntrySize) {
        FillSmallData(dataIn,
                     dataOut,
                     _useAttribute[to_base(AttribLocation::TEXCOORD)],
                     _useAttribute[to_base(AttribLocation::NORMAL)],
                     _useAttribute[to_base(AttribLocation::TANGENT)],
                     _useAttribute[to_base(AttribLocation::COLOR)],
                     _useAttribute[to_base(AttribLocation::BONE_INDICE)]);
        return true;
    }

    return false;
}

void VertexBuffer::refresh() {
    if (!_refreshQueued) {
        // Everything is fine, carry on.
        return;
    }
    _refreshQueued = false;

    assert(!_indices.empty() && "glVertexArray::refresh error: Invalid index data on Refresh()!");

    {
        vector_fast<Byte> smallData(_data.size() * _effectiveEntrySize);
        if (!getMinimalData(_data, smallData.data(), smallData.size())) {
            DIVIDE_UNEXPECTED_CALL();
        }

        if (_dataLayoutChanged) {
            GenericVertexData::SetBufferParams setBufferParams{};
            setBufferParams._bufferParams._elementSize = _effectiveEntrySize;
            setBufferParams._bufferParams._elementCount = to_U32(_data.size());
            setBufferParams._bufferParams._updateFrequency = _staticBuffer ? BufferUpdateFrequency::ONCE : BufferUpdateFrequency::OFTEN;
            setBufferParams._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
            setBufferParams._initialData = { smallData.data(), smallData.size() };
            setBufferParams._elementStride = setBufferParams._bufferParams._elementSize;
            _internalGVD->setBuffer(setBufferParams);
        } else {
            assert(!_staticBuffer);
            _internalGVD->updateBuffer(0u, 0u, to_U32(_data.size()), smallData.data());
        }
        if (_staticBuffer && !_keepData) {
            _data.clear();
        } else {
            _data.shrink_to_fit();
        }
    }

    if (_indicesChanged) {
        // Check if we need to update the IBO (will be true for the first Refresh() call)
        GenericVertexData::IndexBuffer idxBuffer{};
        idxBuffer.count = _indices.size();
        idxBuffer.smallIndices = !useLargeIndices();
        idxBuffer.indicesNeedCast = idxBuffer.smallIndices;
        idxBuffer.data = _indices.data();
        idxBuffer.dynamic = !_staticBuffer;
        _internalGVD->setIndexBuffer(idxBuffer);
        _indicesChanged = false;
    }
}

void VertexBuffer::draw(const GenericDrawCommand& command) {
    // Check if we have a refresh request queued up
    refresh();
    
    _internalGVD->draw(command);
}

/// Activate and set all of the required vertex attributes.
AttributeMap VertexBuffer::generateAttributeMap() {
    AttributeMap retMap{};

    constexpr U32 positionLoc   = to_base(AttribLocation::POSITION);
    constexpr U32 texCoordLoc   = to_base(AttribLocation::TEXCOORD);
    constexpr U32 normalLoc     = to_base(AttribLocation::NORMAL);
    constexpr U32 tangentLoc    = to_base(AttribLocation::TANGENT);
    constexpr U32 colourLoc     = to_base(AttribLocation::COLOR);
    constexpr U32 boneWeightLoc = to_base(AttribLocation::BONE_WEIGHT);
    constexpr U32 boneIndiceLoc = to_base(AttribLocation::BONE_INDICE);

    for (AttributeDescriptor& desc : retMap) {
        desc._dataType = GFXDataFormat::COUNT;
    }

    size_t totalDataSize = 0u;
    const AttributeOffsets offsets = GetAttributeOffsets(_useAttribute, totalDataSize);
    {
        AttributeDescriptor& desc = retMap[to_base(AttribLocation::POSITION)];
        desc._bindingIndex = 0u;
        desc._componentsPerElement = 3;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = offsets[positionLoc];
    }

    if (_useAttribute[texCoordLoc]) {
        AttributeDescriptor& desc = retMap[to_base(AttribLocation::TEXCOORD)];
        desc._bindingIndex = 0u;
        desc._componentsPerElement = 2;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = offsets[texCoordLoc];
    }

    if (_useAttribute[normalLoc]) {
        AttributeDescriptor& desc = retMap[to_base(AttribLocation::NORMAL)];
        desc._bindingIndex = 0u;
        desc._componentsPerElement = 1;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = offsets[normalLoc];
    }

    if (_useAttribute[tangentLoc]) {
        AttributeDescriptor& desc = retMap[to_base(AttribLocation::TANGENT)];
        desc._bindingIndex = 0u;
        desc._componentsPerElement = 1;
        desc._dataType = GFXDataFormat::FLOAT_32;
        desc._normalized = false;
        desc._strideInBytes = offsets[tangentLoc];
    }

    if (_useAttribute[colourLoc]) {
        AttributeDescriptor& desc = retMap[to_base(AttribLocation::COLOR)];
        desc._bindingIndex = 0u;
        desc._componentsPerElement = 4;
        desc._dataType = GFXDataFormat::UNSIGNED_BYTE;
        desc._normalized = true;
        desc._strideInBytes = offsets[colourLoc];
    }

    if (_useAttribute[boneWeightLoc]) {
        assert(_useAttribute[boneIndiceLoc]);
        {
            AttributeDescriptor& desc = retMap[to_base(AttribLocation::BONE_WEIGHT)];
            desc._bindingIndex = 0u;
            desc._componentsPerElement = 4;
            desc._dataType = GFXDataFormat::UNSIGNED_BYTE;
            desc._normalized = true;
            desc._strideInBytes = offsets[boneWeightLoc];
        }
        {
            AttributeDescriptor& desc = retMap[to_base(AttribLocation::BONE_INDICE)];
            desc._bindingIndex = 0u;
            desc._componentsPerElement = 4;
            desc._dataType = GFXDataFormat::UNSIGNED_BYTE;
            desc._normalized = false;
            desc._strideInBytes = offsets[boneIndiceLoc];
        }
    }

    return retMap;
}

//ref: https://www.iquilezles.org/www/articles/normals/normals.htm
void VertexBuffer::computeNormals() {
    const size_t vertCount = getVertexCount();
    const size_t indexCount = getIndexCount();

    vector<vec3<F32>> normalBuffer(vertCount, 0.0f);
    for (size_t i = 0u; i < indexCount; i += 3) {

        const U32 idx0 = getIndex(i + 0);
        if (idx0 == PRIMITIVE_RESTART_INDEX_L || idx0 == PRIMITIVE_RESTART_INDEX_S) {
            assert(i > 2);
            i -= 2;
            continue;
        }

        const U32 idx1 = getIndex(i + 1);
        const U32 idx2 = getIndex(i + 2);

        // get the three vertices that make the faces
        const vec3<F32>& ia = getPosition(idx0);
        const vec3<F32>& ib = getPosition(idx1);
        const vec3<F32>& ic = getPosition(idx2);

        const vec3<F32> no = Cross(ia - ib, ic - ib);

        // Store the face's normal for each of the vertices that make up the face.
        normalBuffer[idx0] += no;
        normalBuffer[idx1] += no;
        normalBuffer[idx2] += no;
    }

    for (U32 i = 0u; i < vertCount; ++i) {
        modifyNormalValue(i, Normalized(normalBuffer[i]));
    }
}

void VertexBuffer::computeTangents() {
    const size_t indexCount = getIndexCount();

    // Code from:
    // http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping/#header-1

    vec3<F32> deltaPos1, deltaPos2;
    vec2<F32> deltaUV1, deltaUV2;
    vec3<F32> tangent;

    for (U32 i = 0u; i < indexCount; i += 3) {
        // get the three vertices that make the faces
        const U32 idx0 = getIndex(i + 0);
        if (idx0 == PRIMITIVE_RESTART_INDEX_L || idx0 == PRIMITIVE_RESTART_INDEX_S) {
            assert(i > 2);
            i -= 2;
            continue;
        }

        const U32 idx1 = getIndex(to_size(i) + 1);
        const U32 idx2 = getIndex(to_size(i) + 2);

        const vec3<F32>& v0 = getPosition(idx0);
        const vec3<F32>& v1 = getPosition(idx1);
        const vec3<F32>& v2 = getPosition(idx2);

        // Shortcuts for UVs
        const vec2<F32>& uv0 = getTexCoord(idx0);
        const vec2<F32>& uv1 = getTexCoord(idx1);
        const vec2<F32>& uv2 = getTexCoord(idx2);

        // Edges of the triangle : position delta
        deltaPos1.set(v1 - v0);
        deltaPos2.set(v2 - v0);

        // UV delta
        deltaUV1.set(uv1 - uv0);
        deltaUV2.set(uv2 - uv0);

        const F32 r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
        tangent.set((deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r);
        tangent.normalize();
        // Set the same tangent for all three vertices of the triangle.
        // They will be merged later, in vbindexer.cpp
        modifyTangentValue(idx0, tangent);
        modifyTangentValue(idx1, tangent);
        modifyTangentValue(idx2, tangent);
    }
}

void VertexBuffer::reset() {
    _staticBuffer = false;
    primitiveRestartRequired(false);
    _partitions.clear();
    _data.clear();
    _indices.clear();
    _useAttribute.fill(false);
}

void VertexBuffer::fromBuffer(const VertexBuffer& other) {
    reset();
    staticBuffer(other.staticBuffer());
    useLargeIndices(other.useLargeIndices());
    primitiveRestartRequired(other.primitiveRestartRequired());
    unchecked_copy(_indices, other._indices);
    unchecked_copy(_data, other._data);
    _partitions = other._partitions;
    _keepData = other._keepData;
    _useAttribute = other._useAttribute;
    _effectiveEntrySize = other._effectiveEntrySize;
    _refreshQueued = true;
    _dataLayoutChanged = true;
}

bool VertexBuffer::deserialize(ByteBuffer& dataIn) {
    assert(!dataIn.bufferEmpty());

    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    dataIn >> tempVer;

    if (tempVer == BYTE_BUFFER_VERSION) {
        U64 idString = 0u;
        dataIn >> idString;
        if (idString == _ID("VB")) {
            reset();
            dataIn >> _staticBuffer;
            dataIn >> _keepData;
            dataIn >> _partitions;
            dataIn >> _indices;
            dataIn >> _data;
            dataIn >> _useAttribute;
            dataIn >> _useLargeIndices;
            dataIn >> _primitiveRestartRequired;

            return true;
        }
    }

    return false;
}

bool VertexBuffer::serialize(ByteBuffer& dataOut) const {
    if (!_data.empty()) {
        dataOut << BYTE_BUFFER_VERSION;
        dataOut << _ID("VB");
        dataOut << _staticBuffer;
        dataOut << _keepData;
        dataOut << _partitions;
        dataOut << _indices;
        dataOut << _data;
        dataOut << _useAttribute;
        dataOut << _useLargeIndices;
        dataOut << _primitiveRestartRequired;

        return true;
    }
    return false;
}

AttributeOffsets VertexBuffer::GetAttributeOffsets(const AttributeFlags& usedAttributes, size_t& totalDataSizeOut) {
    AttributeOffsets offsets{};

    totalDataSizeOut = sizeof(vec3<F32>);

    if (usedAttributes[to_base(AttribLocation::TEXCOORD)]) {
        offsets[to_base(AttribLocation::TEXCOORD)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(vec2<F32>);
    }

    if (usedAttributes[to_base(AttribLocation::NORMAL)]) {
        offsets[to_base(AttribLocation::NORMAL)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(F32);
    }

    if (usedAttributes[to_base(AttribLocation::TANGENT)]) {
        offsets[to_base(AttribLocation::TANGENT)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(F32);
    }

    if (usedAttributes[to_base(AttribLocation::COLOR)]) {
        offsets[to_base(AttribLocation::COLOR)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(UColour4);
    }

    if (usedAttributes[to_base(AttribLocation::BONE_INDICE)]) {
        offsets[to_base(AttribLocation::BONE_WEIGHT)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(U32);
        offsets[to_base(AttribLocation::BONE_INDICE)] = to_U32(totalDataSizeOut);
        totalDataSizeOut += sizeof(U32);
    }

    return offsets;
}

};
