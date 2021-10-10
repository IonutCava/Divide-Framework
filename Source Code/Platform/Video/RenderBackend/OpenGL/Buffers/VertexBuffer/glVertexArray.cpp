#include "stdafx.h"

#include "Headers/glVertexArray.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

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
}

GLUtil::glVAOCache glVertexArray::_VAOMap;

void glVertexArray::cleanup() {
    _VAOMap.clear();
}

/// Default destructor
glVertexArray::glVertexArray(GFXDevice& context)
    : VertexBuffer(context)
{
}

glVertexArray::~glVertexArray()
{
    GLUtil::releaseVBO(_VBHandle._id, _VBHandle._offset);
}

void glVertexArray::reset() {
    _usage = GL_STATIC_DRAW;
    _prevSize = 0;
    _prevSizeIndices = 0;

    _useAttribute.fill(false);
    _attributeOffset.fill(0);
    VertexBuffer::reset();
}

size_t glVertexArray::populateAttributeSize() {
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
bool glVertexArray::getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, const size_t dataOutBufferLength) {
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

/// Create a dynamic or static VB
bool glVertexArray::create(const bool staticDraw) {
    // If we want a dynamic buffer, then we are doing something outdated, such
    // as software skinning, or software water rendering
    if (!staticDraw && _usage != GL_DYNAMIC_DRAW) {
        _usage = GL_DYNAMIC_DRAW;
        _refreshQueued = true;
    }

    return VertexBuffer::create(staticDraw);
}

/// Update internal data based on construction method (dynamic/static)
bool glVertexArray::refresh() {
    // Dynamic LOD elements (such as terrain) need dynamic indices
    // We can manually override index usage (again, used by the Terrain
    // rendering system)
    assert(!_indices.empty() && "glVertexArray::refresh error: Invalid index data on Refresh()!");

    const size_t nSizeIndices = _indices.size() * (usesLargeIndices() ? sizeof(U32) : sizeof(U16));

    const bool indicesChanged = nSizeIndices != _prevSizeIndices;
    _prevSizeIndices = nSizeIndices;
    _refreshQueued = indicesChanged;

    /// Can only add attributes for now. No removal support. -Ionut
    for (U8 i = 0; i < to_base(AttribLocation::COUNT); ++i) {
        _useAttribute[i] = _useAttribute[i] || _attribDirty[i];
        if (!_refreshQueued) {
           if (_attribDirty[i]) {
               _refreshQueued = true;
           }
        }
    }
    _attribDirty.fill(false);

    _effectiveEntrySize = populateAttributeSize();
    const size_t totalSize = _data.size() * _effectiveEntrySize;

    // If any of the VBO's components changed size, we need to recreate the
    // entire buffer.
    const bool sizeChanged = totalSize != _prevSize;
    bool needsReallocation = false;
    const U32 countRequirement = GLUtil::VBO::getChunkCountForSize(totalSize);
    if (sizeChanged) {
        needsReallocation = countRequirement > GLUtil::VBO::getChunkCountForSize(_prevSize);
        _prevSize = totalSize;
    }

    if (!_refreshQueued && !sizeChanged) {
        return false;
    }

    if (_VBHandle._id == 0 || needsReallocation) {
        if (_VBHandle._id != 0) {
            GLUtil::releaseVBO(_VBHandle._id, _VBHandle._offset);
        }
        // Generate a new Vertex Buffer Object
        GLUtil::commitVBO(countRequirement,_usage, _VBHandle._id, _VBHandle._offset);
        assert(_VBHandle._id != 0 && Locale::Get(_ID("ERROR_VB_INIT")));
    }
    // Refresh buffer data (if this is the first call to refresh, this will be true)
    if (sizeChanged) {
        Console::d_printfn(Locale::Get(_ID("DISPLAY_VB_METRICS")),
                           _VBHandle._id,
                           totalSize,
                           countRequirement * GLUtil::VBO::MAX_VBO_CHUNK_SIZE_BYTES,
                           GLUtil::getVBOMemUsage(_VBHandle._id),
                           GLUtil::getVBOCount());
    }

    // Allocate sufficient space in our buffer
    {
        vector_fast<Byte> smallData(_data.size() * _effectiveEntrySize);
        if (getMinimalData(_data, smallData.data(), smallData.size())) {
            glNamedBufferSubData(_VBHandle._id, _VBHandle._offset * GLUtil::VBO::MAX_VBO_CHUNK_SIZE_BYTES, smallData.size(), (bufferPtr)smallData.data());
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
        if (usesLargeIndices()) {
            const bufferPtr data = static_cast<bufferPtr>(_indices.data());
            // Update our IB
            glNamedBufferData(_IBid, nSizeIndices, data, GL_STATIC_DRAW);
        } else {
            vector<U16> smallIndices;
            smallIndices.reserve(getIndexCount());
            transform(cbegin(_indices),
                      cend(_indices),
                      back_inserter(smallIndices),
                      static_caster<U32, U16>());
            // Update our IB
            glNamedBufferData(_IBid, nSizeIndices, (bufferPtr)smallIndices.data(), GL_STATIC_DRAW);
        }
    }

    // Possibly clear client-side buffer for all non-required attributes?
    // foreach attribute if !required then delete else skip ?
    _refreshQueued = false;
    _uploadQueued = true;

    return true;
}

void glVertexArray::upload() {
    Console::printfn("VAO HASHES: ");

    constexpr size_t stageCount = to_size(to_base(RenderStage::COUNT) * to_base(RenderPassType::COUNT));

    vector<GLuint> vaos;
    vaos.reserve(stageCount);
    std::array<AttribFlags, stageCount> attributesPerStage = {};

    for (size_t i = 0; i < stageCount; ++i) {
        const AttribFlags& stageMask = _attribMasks[i];

        AttribFlags& stageUsage = attributesPerStage[i];
        for (U8 j = 0; j < to_base(AttribLocation::COUNT); ++j) {
            stageUsage[j] = _useAttribute[j] && stageMask[j];
        }

        size_t crtHash = 0;
        // Dirty on a VAO map cache miss
        if (!_VAOMap.getVAO(stageUsage, _vaoCaches[i], crtHash)) {
            const GLuint crtVao = _vaoCaches[i];
            if (eastl::find(cbegin(vaos), cend(vaos), crtVao) == cend(vaos)) {
                vaos.push_back(crtVao);
                // Set vertex attribute pointers
                uploadVBAttributes(crtVao);
            }
        }
        const RenderStage stage = static_cast<RenderStage>(i % to_base(RenderStage::COUNT));
        const RenderPassType pass = static_cast<RenderPassType>(i / to_base(RenderStage::COUNT));
        Console::printfn("      %s : %zu (pass: %s)", TypeUtil::RenderStageToString(stage), crtHash, TypeUtil::RenderPassTypeToString(pass));
    }

    _uploadQueued = false;
}
/// This method creates the initial VAO and VB OpenGL objects and queues a
/// Refresh call
bool glVertexArray::createInternal() {
    // Avoid double create calls
    assert(_VBHandle._id == 0 && "glVertexArray error: Attempted to double create a VB");
    // Position data is a minim requirement
    if (_data.empty()) {
        Console::errorfn(Locale::Get(_ID("ERROR_VB_POSITION")));
        return false;
    }
    VertexBuffer::createInternal();

    _formatInternal = GLUtil::glDataFormat[to_U32(_format)];
    // Generate an "Index Buffer Object"
    glCreateBuffers(1, &_IBid);
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_BUFFER,
            _IBid,
            -1,
            Util::StringFormat("DVD_VAO_INDEX_BUFFER_%d", _IBid).c_str());
    }
    // Validate buffer creation
    // Assert if the IB creation failed
    assert(_IBid != 0 && Locale::Get(_ID("ERROR_IB_INIT")));
    // Calling refresh updates all stored information and sends it to the GPU
    return queueRefresh();
}

/// Render the current buffer data using the specified command
void glVertexArray::draw(const GenericDrawCommand& command) {

    // Make sure the buffer is current
    // Make sure we have valid data (buffer creation is deferred to the first activate call)
    if (_IBid == 0 && !createInternal()) {
        return;
    }

    // Check if we have a refresh request queued up
    if (_refreshQueued && !refresh()) {
        return;
    }

    if (_uploadQueued) {
        upload();
    }

    GLStateTracker& stateTracker = GL_API::getStateTracker();
    // Bind the vertex array object that in turn activates all of the bindings and pointers set on creation
    const GLuint vao = _vaoCaches[command._bufferIndex == GenericDrawCommand::INVALID_BUFFER_INDEX ? 0u : command._bufferIndex];
    stateTracker.setActiveVAO(vao);

    stateTracker.togglePrimitiveRestart(_primitiveRestartEnabled);

    // VAOs store vertex formats and are reused by multiple 3d objects, so the Index Buffer and Vertex Buffers need to be double checked
    stateTracker.setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, _IBid);

    stateTracker.bindActiveBuffer(vao, 0u, _VBHandle._id, 0u, _VBHandle._offset * GLUtil::VBO::MAX_VBO_CHUNK_SIZE_BYTES, _effectiveEntrySize);

    if (renderIndirect()) {
        GLUtil::SubmitRenderCommand(command,
                                    true,
                                    true,
                                    _formatInternal);
    } else {
        rebuildCountAndIndexData(command._drawCount, command._cmd.indexCount, command._cmd.firstIndex, getIndexCount());
        GLUtil::SubmitRenderCommand(command,
                                    true,
                                    false,
                                    _formatInternal,
                                    _indexInfo->_countData.data(),
                                    (bufferPtr)_indexInfo->_indexOffsetData.data());
    }
}

/// Activate and set all of the required vertex attributes.
void glVertexArray::uploadVBAttributes(const GLuint VAO) {
    // Bind the current VAO to save our attributes
    GL_API::getStateTracker().setActiveVAO(VAO);
    // Bind the vertex buffer and index buffer
    GL_API::getStateTracker().bindActiveBuffer(VAO,
                                               0u,
                                               _VBHandle._id,
                                               0u,
                                               _VBHandle._offset * GLUtil::VBO::MAX_VBO_CHUNK_SIZE_BYTES,
                                               _effectiveEntrySize);

    constexpr U32 positionLoc   = to_base(AttribLocation::POSITION);
    constexpr U32 texCoordLoc   = to_base(AttribLocation::TEXCOORD);
    constexpr U32 normalLoc     = to_base(AttribLocation::NORMAL);
    constexpr U32 tangentLoc    = to_base(AttribLocation::TANGENT);
    constexpr U32 colourLoc     = to_base(AttribLocation::COLOR);
    constexpr U32 boneWeightLoc = to_base(AttribLocation::BONE_WEIGHT);
    constexpr U32 boneIndiceLoc = to_base(AttribLocation::BONE_INDICE);

    glEnableVertexAttribArray(positionLoc);
    glVertexAttribFormat(positionLoc, 3, GL_FLOAT, GL_FALSE, _attributeOffset[positionLoc]);
    glVertexAttribBinding(positionLoc, 0);

    if (_useAttribute[texCoordLoc]) {
        glEnableVertexAttribArray(texCoordLoc);
        glVertexAttribFormat(texCoordLoc, 2, GL_FLOAT, GL_FALSE, _attributeOffset[texCoordLoc]);
        glVertexAttribBinding(texCoordLoc, 0);
    } else {
        glDisableVertexAttribArray(texCoordLoc);
    }

    if (_useAttribute[normalLoc]) {
        glEnableVertexAttribArray(normalLoc);
        glVertexAttribFormat(normalLoc, 1, GL_FLOAT, GL_FALSE, _attributeOffset[normalLoc]);
        glVertexAttribBinding(normalLoc, 0);
    } else {
        glDisableVertexAttribArray(normalLoc);
    }

    if (_useAttribute[tangentLoc]) {
        glEnableVertexAttribArray(tangentLoc);
        glVertexAttribFormat(tangentLoc, 1, GL_FLOAT, GL_FALSE, _attributeOffset[tangentLoc]);
        glVertexAttribBinding(tangentLoc, 0);
    } else {
        glDisableVertexAttribArray(tangentLoc);
    }

    if (_useAttribute[colourLoc]) {
        glEnableVertexAttribArray(colourLoc);
        glVertexAttribFormat(colourLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, _attributeOffset[colourLoc]);
        glVertexAttribBinding(colourLoc, 0);
    } else {
        glDisableVertexAttribArray(colourLoc);
    }

    if (_useAttribute[boneWeightLoc]) {
        assert(_useAttribute[boneIndiceLoc]);

        glEnableVertexAttribArray(boneWeightLoc);
        glEnableVertexAttribArray(boneIndiceLoc);
        glVertexAttribFormat(boneWeightLoc, 4, GL_UNSIGNED_BYTE, GL_TRUE, _attributeOffset[boneWeightLoc]);
        glVertexAttribIFormat(boneIndiceLoc, 4, GL_UNSIGNED_BYTE, _attributeOffset[boneIndiceLoc]);
        glVertexAttribBinding(boneWeightLoc, 0);
        glVertexAttribBinding(boneIndiceLoc, 0);
    } else {
        glDisableVertexAttribArray(boneWeightLoc);
        glDisableVertexAttribArray(boneIndiceLoc);
    }
}

};
