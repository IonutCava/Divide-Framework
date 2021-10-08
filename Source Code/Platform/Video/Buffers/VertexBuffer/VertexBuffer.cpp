#include "stdafx.h"

#include "Headers/VertexBuffer.h"

#include "Platform/Video/Headers/RenderAPIWrapper.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

vector<AttribFlags> VertexBuffer::_attribMasks;

VertexBuffer::VertexBuffer(GFXDevice& context)
    : VertexDataInterface(context)
{
}

bool VertexBuffer::create(const bool staticDraw) {
    _staticBuffer = staticDraw;
    return true;
}

bool VertexBuffer::createInternal() {
    return true;
}


void VertexBuffer::setAttribMasks(const size_t count, const AttribFlags& flagMask) {
    _attribMasks.resize(0);
    _attribMasks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        _attribMasks.push_back(flagMask);
    }
}

void VertexBuffer::setAttribMask(const size_t index, const AttribFlags& flagMask) {
    assert(index < _attribMasks.size());
    _attribMasks[index] = flagMask;
}

//ref: https://www.iquilezles.org/www/articles/normals/normals.htm
void VertexBuffer::computeNormals() {
    const size_t vertCount = getVertexCount();
    const size_t indexCount = getIndexCount();

    vector<vec3<F32>> normalBuffer(vertCount, 0.0f);
    for (U32 i = 0; i < indexCount; i += 3) {

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

        const U32 idx1 = getIndex(i + 1);
        const U32 idx2 = getIndex(i + 2);

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

void VertexBuffer::fromBuffer(VertexBuffer& other) {
    reset();
    _staticBuffer = other._staticBuffer;
    _format = other._format;
    _partitions = other._partitions;
    _primitiveRestartEnabled = other._primitiveRestartEnabled;
    _attribDirty = other._attribDirty;
    unchecked_copy(_indices, other._indices);
    unchecked_copy(_data, other._data);
}

bool VertexBuffer::deserialize(ByteBuffer& dataIn) {
    assert(!dataIn.bufferEmpty());

    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    dataIn >> tempVer;

    if (tempVer == BYTE_BUFFER_VERSION) {
        U64 idString;
        dataIn >> idString;
        if (idString == _ID("VB")) {
            reset();
            U32 format;

            dataIn >> _staticBuffer;
            dataIn >> _keepDataInMemory;
            dataIn >> format;
            _format = static_cast<GFXDataFormat>(format);
            dataIn >> _partitions;
            dataIn >> _indices;
            dataIn >> _data;
            dataIn >> _attribDirty;
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
        dataOut << to_U32(_format);
        dataOut << _partitions;
        dataOut << _indices;
        dataOut << _data;
        dataOut << _attribDirty;
        dataOut << _primitiveRestartEnabled;

        return true;
    }
    return false;
}

};