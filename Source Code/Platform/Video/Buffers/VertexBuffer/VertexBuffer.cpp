#include "stdafx.h"

#include "Headers/VertexBuffer.h"
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

vector<AttribFlags> VertexBuffer::s_attribMasks;

VertexBuffer::VertexBuffer(GFXDevice& context)
    : VertexDataInterface(context)
    , _internalGVD(context.newGVDLocked(1u))
{
    _internalGVD->create();
}

bool VertexBuffer::create(const bool staticDraw) {
    // If we want a dynamic buffer, then we are doing something outdated, such
    // as software skinning, or software water rendering
    if (_staticBuffer != staticDraw) {
        queueRefresh();
    }


    _staticBuffer = staticDraw;
    return true;
}

bool VertexBuffer::createInternal() {
    // Position data is a minim requirement
    if (keepData() && _data.empty()) {
        Console::errorfn(Locale::Get(_ID("ERROR_VB_POSITION")));
        return false;
    }

    return true;
}


void VertexBuffer::setAttribMasks(const size_t count, const AttribFlags& flagMask) {
    s_attribMasks.resize(0);
    s_attribMasks.reserve(count);
    for (size_t i = 0u; i < count; ++i) {
        s_attribMasks.push_back(flagMask);
    }
}

void VertexBuffer::setAttribMask(const size_t index, const AttribFlags& flagMask) {
    assert(index < s_attribMasks.size());
    s_attribMasks[index] = flagMask;
}

size_t VertexBuffer::populateAttributeSize() {
    size_t prevOffset = sizeof(vec3<F32>);

    if (_useAttribute[to_base(AttribLocation::TEXCOORD)]) {
        _attributeOffset[to_base(AttribLocation::TEXCOORD)] = to_U32(prevOffset);
        prevOffset += sizeof(vec2<F32>);
    }

    if (_useAttribute[to_base(AttribLocation::NORMAL)]) {
        _attributeOffset[to_base(AttribLocation::NORMAL)] = to_U32(prevOffset);
        prevOffset += sizeof(F32);
    }

    if (_useAttribute[to_base(AttribLocation::TANGENT)]) {
        _attributeOffset[to_base(AttribLocation::TANGENT)] = to_U32(prevOffset);
        prevOffset += sizeof(F32);
    }

    if (_useAttribute[to_base(AttribLocation::COLOR)]) {
        _attributeOffset[to_base(AttribLocation::COLOR)] = to_U32(prevOffset);
        prevOffset += sizeof(UColour4);
    }

    if (_useAttribute[to_base(AttribLocation::BONE_INDICE)]) {
        _attributeOffset[to_base(AttribLocation::BONE_WEIGHT)] = to_U32(prevOffset);
        prevOffset += sizeof(U32);
        _attributeOffset[to_base(AttribLocation::BONE_INDICE)] = to_U32(prevOffset);
        prevOffset += sizeof(U32);
    }

    return prevOffset;
}

/// Trim down the Vertex vector to only upload the minimal amount of data to the GPU
bool VertexBuffer::getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, const size_t dataOutBufferLength) {
    assert(dataOut != nullptr);

    if (dataOutBufferLength == dataIn.size() * _effectiveEntrySize) {
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

bool VertexBuffer::refresh() {
    assert(!_indices.empty() && "glVertexArray::refresh error: Invalid index data on Refresh()!");

    const bool indicesChanged = _indices.size() != _prevSizeIndices;
    _prevSizeIndices = _indices.size();
    if (indicesChanged) {
        queueRefresh();
    }

    /// Can only add attributes for now. No removal support. -Ionut
    for (U8 i = 0u; i < to_base(AttribLocation::COUNT); ++i) {
        _useAttribute[i] = _useAttribute[i] || _attribDirty[i];
        if (!_refreshQueued) {
           if (_attribDirty[i]) {
               queueRefresh();
           }
        }
    }
    _attribDirty.fill(false);

    _effectiveEntrySize = populateAttributeSize();
    const size_t totalSize = _data.size() * _effectiveEntrySize;

    // If any of the VBO's components changed size, we need to recreate the
    // entire buffer.
    const bool sizeChanged = totalSize != _prevSize;
    if (sizeChanged) {
        _prevSize = totalSize;
    }

    if (!_refreshQueued && !sizeChanged) {
        return false;
    }
    {
        vector_fast<Byte> smallData(_data.size() * _effectiveEntrySize);
        if (getMinimalData(_data, smallData.data(), smallData.size())) {
            GenericVertexData::SetBufferParams setBufferParams{};
            setBufferParams._bufferParams._elementSize = _effectiveEntrySize;
            setBufferParams._bufferParams._elementCount = to_U32(_data.size());
            setBufferParams._bufferParams._sync = false;
            setBufferParams._bufferParams._syncAtEndOfCmdBuffer = false;
            setBufferParams._bufferParams._usePersistentMapping = false;
            setBufferParams._bufferParams._updateFrequency = _staticBuffer ? BufferUpdateFrequency::RARELY : BufferUpdateFrequency::OFTEN;
            setBufferParams._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
            setBufferParams._bufferParams._initialData = { smallData.data(), smallData.size()};
            _internalGVD->setBuffer(setBufferParams);
        } else {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    if (_staticBuffer && !keepData()) {
        _data.clear();
    } else {
        _data.shrink_to_fit();
    }

    // Check if we need to update the IBO (will be true for the first Refresh() call)
    if (indicesChanged) {
        GenericVertexData::IndexBuffer idxBuffer{};
        idxBuffer.count = _indices.size();
        idxBuffer.offsetCount = 0u;
        idxBuffer.smallIndices = !useLargeIndices();
        idxBuffer.indicesNeedCast = idxBuffer.smallIndices;
        idxBuffer.data = _indices.data();
        _internalGVD->setIndexBuffer(idxBuffer, BufferUpdateFrequency::RARELY);
    }

    // Possibly clear client-side buffer for all non-required attributes?
    // foreach attribute if !required then delete else skip ?
    _refreshQueued = false;
    _uploadQueued = true;

    return true;
}

void VertexBuffer::upload() {
    uploadVBAttributes();
    _uploadQueued = false;
}

/// Activate and set all of the required vertex attributes.
void VertexBuffer::uploadVBAttributes() {
    constexpr U32 positionLoc   = to_base(AttribLocation::POSITION);
    constexpr U32 texCoordLoc   = to_base(AttribLocation::TEXCOORD);
    constexpr U32 normalLoc     = to_base(AttribLocation::NORMAL);
    constexpr U32 tangentLoc    = to_base(AttribLocation::TANGENT);
    constexpr U32 colourLoc     = to_base(AttribLocation::COLOR);
    constexpr U32 boneWeightLoc = to_base(AttribLocation::BONE_WEIGHT);
    constexpr U32 boneIndiceLoc = to_base(AttribLocation::BONE_INDICE);

    _internalGVD->attribDescriptor(positionLoc).set(0u, 3, GFXDataFormat::FLOAT_32, false, _attributeOffset[positionLoc]);

    if (_useAttribute[texCoordLoc]) {
        _internalGVD->attribDescriptor(texCoordLoc).set(0u, 2, GFXDataFormat::FLOAT_32, false, _attributeOffset[texCoordLoc]);
    } else {
        _internalGVD->attribDescriptor(texCoordLoc).enabled(false);
    }

    if (_useAttribute[normalLoc]) {
        _internalGVD->attribDescriptor(normalLoc).set(0u, 1, GFXDataFormat::FLOAT_32, false, _attributeOffset[normalLoc]);
    } else {
        _internalGVD->attribDescriptor(normalLoc).enabled(false);
    }

    if (_useAttribute[tangentLoc]) {
        _internalGVD->attribDescriptor(tangentLoc).set(0u, 1, GFXDataFormat::FLOAT_32, false, _attributeOffset[tangentLoc]);
    } else {
        _internalGVD->attribDescriptor(tangentLoc).enabled(false);
    }

    if (_useAttribute[colourLoc]) {
        _internalGVD->attribDescriptor(colourLoc).set(0u, 4, GFXDataFormat::UNSIGNED_BYTE, true, _attributeOffset[colourLoc]);
    } else {
        _internalGVD->attribDescriptor(colourLoc).enabled(false);
    }

    if (_useAttribute[boneWeightLoc]) {
        assert(_useAttribute[boneIndiceLoc]);
        _internalGVD->attribDescriptor(boneWeightLoc).set(0u, 4, GFXDataFormat::UNSIGNED_BYTE, true, _attributeOffset[boneWeightLoc]);
        _internalGVD->attribDescriptor(boneIndiceLoc).set(0u, 4, GFXDataFormat::UNSIGNED_BYTE, false, _attributeOffset[boneIndiceLoc]);
    } else {
        _internalGVD->attribDescriptor(boneWeightLoc).enabled(false);
        _internalGVD->attribDescriptor(boneIndiceLoc).enabled(false);
    }
}

void VertexBuffer::draw(const GenericDrawCommand& command) {
    // Make sure the buffer is current
    // Make sure we have valid data (buffer creation is deferred to the first activate call)
    if (!createInternal()) {
        return;
    }

    // Check if we have a refresh request queued up
    if (_refreshQueued && !refresh()) {
        return;
    }

    if (_uploadQueued) {
        upload();
    }

    _internalGVD->draw(command);
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

    for (U32 i = 0; i < vertCount; ++i) {
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

    for (U32 i = 0; i < indexCount; i += 3) {
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
    _prevSize = 0u;
    _prevSizeIndices = 0u;
    _staticBuffer = false;
    _refreshQueued = false;
    _uploadQueued = false;
    primitiveRestartEnabled(false);
    _partitions.clear();
    _data.clear();
    _indices.clear();
    _attribDirty.fill(false);
    _useAttribute.fill(false);
    _attributeOffset.fill(0);
}

void VertexBuffer::fromBuffer(const VertexBuffer& other) {
    reset();
    _keepDataInMemory = other._keepDataInMemory;
    _staticBuffer = other._staticBuffer;
    _partitions = other._partitions;
    _attribDirty = other._attribDirty;
    useLargeIndices(other._useLargeIndices);
    primitiveRestartEnabled(other._primitiveRestartEnabled);
    unchecked_copy(_indices, other._indices);
    unchecked_copy(_data, other._data);
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
            dataIn >> _keepDataInMemory;
            dataIn >> _partitions;
            dataIn >> _indices;
            dataIn >> _data;
            dataIn >> _attribDirty;
            dataIn >> _useLargeIndices;
            dataIn >> _primitiveRestartEnabled;

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
        dataOut << _keepDataInMemory;
        dataOut << _partitions;
        dataOut << _indices;
        dataOut << _data;
        dataOut << _attribDirty;
        dataOut << _useLargeIndices;
        dataOut << _primitiveRestartEnabled;

        return true;
    }
    return false;
}

};