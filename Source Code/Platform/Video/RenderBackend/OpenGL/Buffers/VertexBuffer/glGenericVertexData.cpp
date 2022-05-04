#include "stdafx.h"


#include "Headers/glGenericVertexData.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

glGenericVertexData::glGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
    : GenericVertexData(context, ringBufferLength, name)
{
}

glGenericVertexData::~glGenericVertexData()
{
    reset();
}

/// Create the specified number of buffers and queries and attach them to this vertex data container
void glGenericVertexData::create(const U8 numBuffers) {
    // Prevent double create
    assert(_bufferObjects.empty() && "glGenericVertexData error: create called with no buffers specified!");
    // Create our buffer objects
    _bufferObjects.resize(numBuffers, {});
}

void glGenericVertexData::reset() {
    for (genericBufferImpl& it : _bufferObjects) {
        MemoryManager::DELETE(it._buffer);
    }
    _bufferObjects.resize(0);

    GLUtil::freeBuffer(_indexBuffer);
}

/// Submit a draw command to the GPU using this object and the specified command
void glGenericVertexData::draw(const GenericDrawCommand& command) {
    // Update buffer bindings
    if (!_bufferObjects.empty()) {
        if (command._bufferIndex == GenericDrawCommand::INVALID_BUFFER_INDEX) {
            bindBuffersInternal();
        } else {
            bindBufferInternal(command._bufferIndex, 0u);
        }
    }

    if (GL_API::GetStateTracker()->setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    GL_API::GetStateTracker()->togglePrimitiveRestart(primitiveRestartEnabled());

    // Submit the draw command
    if (renderIndirect()) {
        GLUtil::SubmitRenderCommand(command,
                                    true,
                                    _indexDataType);
    } else {
        rebuildCountAndIndexData(command._drawCount, command._cmd.indexCount, command._cmd.firstIndex, idxBuffer().count);
        GLUtil::SubmitRenderCommand(command,
                                    false,
                                    _indexDataType,
                                    _indexInfo._countData.data(),
                                    (bufferPtr)_indexInfo._indexOffsetData.data());
    }
}

void glGenericVertexData::setIndexBuffer(const IndexBuffer& indices) {
    GenericVertexData::setIndexBuffer(indices);

    if (_indexBuffer != 0) {
        GLUtil::freeBuffer(_indexBuffer);
        _indexDataType = GL_NONE;
    }

    _indexBufferUsage = indices.dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW;

    if (indices.count > 0) {
        updateIndexBuffer(indices);
    }
}

void glGenericVertexData::updateIndexBuffer(const IndexBuffer& indices) {
    GenericVertexData::updateIndexBuffer(indices);

    DIVIDE_ASSERT(indices.count > 0, "glGenericVertexData::UpdateIndexBuffer error: Invalid index buffer data!");

    const size_t elementSize = indices.smallIndices ? sizeof(GLushort) : sizeof(GLuint);

    _indexDataType = indices.smallIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    vector_fast<U16> smallIndicesTemp;
    bufferPtr data = indices.data;

    if (indices.indicesNeedCast) {
        smallIndicesTemp.resize(indices.count);
        const U32* const dataIn = reinterpret_cast<U32*>(data);
        for (size_t i = 0u; i < indices.count; ++i) {
            smallIndicesTemp[i] = to_U16(dataIn[i]);
        }
        data = smallIndicesTemp.data();
    }

    if (_indexBuffer == 0u) {
        _indexBufferSize = static_cast<GLuint>(indices.count * elementSize);

        assert(indices.offsetCount == 0u);

        GLUtil::createAndAllocBuffer(
            _indexBufferSize,
            _indexBufferUsage,
            _indexBuffer,
            { data, _indexBufferSize },
            _name.empty() ? nullptr : (_name + "_index").c_str());
    } else {
        const size_t offset = indices.offsetCount * elementSize;
        const size_t count = indices.count * elementSize;
        DIVIDE_ASSERT(offset + count <= _indexBufferSize);

        glInvalidateBufferSubData(_indexBuffer, offset, count);
        glNamedBufferSubData(_indexBuffer, offset, count, data);
    }
}

/// Specify the structure and data of the given buffer
void glGenericVertexData::setBuffer(const SetBufferParams& params) {
    const U32 buffer = params._buffer;
    // Make sure the buffer exists
    DIVIDE_ASSERT(buffer >= 0 && buffer < _bufferObjects.size() && "glGenericVertexData error: set buffer called for invalid buffer index!");

    MemoryManager::SAFE_DELETE(_bufferObjects[buffer]._buffer);

    const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
    const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;
    const size_t totalSizeInBytes = bufferSizeInBytes * ringSizeFactor;

    BufferImplParams implParams;
    implParams._bufferParams = params._bufferParams;
    implParams._dataSize = totalSizeInBytes;
    implParams._target = GL_ARRAY_BUFFER;
    implParams._name = _name.empty() ? nullptr : _name.c_str();
    implParams._useChunkAllocation = true;

    _bufferObjects[buffer]._buffer = MemoryManager_NEW glBufferImpl(_context, implParams);
    _bufferObjects[buffer]._ringSizeFactor = ringSizeFactor;

    if (params._bufferParams._initialData.second > 0u) {
        for (U32 i = 1u; i < ringSizeFactor; ++i) {
            _bufferObjects[buffer]._buffer->writeOrClearBytes(i * bufferSizeInBytes, params._bufferParams._initialData.second, params._bufferParams._initialData.first, false);
        }
    }
    _bufferObjects[buffer]._wasWritten = true;
}

/// Update the elementCount worth of data contained in the buffer starting from elementCountOffset size offset
void glGenericVertexData::updateBuffer(const U32 buffer,
                                       const U32 elementCountOffset,
                                       const U32 elementCountRange,
                                       const bufferPtr data) {
    glBufferImpl* bufferImpl = _bufferObjects[buffer]._buffer;
    const BufferParams& bufferParams = bufferImpl->params()._bufferParams;

    // Calculate the size of the data that needs updating
    const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
    // Calculate the offset in the buffer in bytes from which to start writing
    size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;

    if (_bufferObjects[buffer]._ringSizeFactor > 1u) {
        offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
    }

    bufferImpl->writeOrClearBytes(offsetInBytes, dataCurrentSizeInBytes, data, false);
    _bufferObjects[buffer]._wasWritten = true;
}

void glGenericVertexData::bindBuffersInternal() {
    const size_t bufferCount = _bufferObjects.size();
    for (U32 i = 0u; i < bufferCount; ++i) {
       bindBufferInternal(i, i);
    }
}

void glGenericVertexData::bindBufferInternal(const U32 buffer, const  U32 location) {
    glBufferImpl* bufferImpl = _bufferObjects[buffer]._buffer;
    const BufferParams& bufferParams = bufferImpl->params()._bufferParams;
    size_t offsetInBytes = 0u;

    if (_bufferObjects[buffer]._ringSizeFactor > 1) {
        offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
    }

    const GLStateTracker::BindResult ret = GL_API::GetStateTracker()->bindActiveBuffer(location, bufferImpl->memoryBlock()._bufferHandle, bufferImpl->memoryBlock()._offset + offsetInBytes, bufferParams._elementSize);
    if (ret == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    if (_bufferObjects[buffer]._wasWritten) {
        const size_t rangeInBytes = bufferParams._elementCount * bufferParams._elementSize;
        // Register the bind even if we get GLStateTracker::BindResult::ALREADY_BOUND so that we know that the data is still being used by the GPU
        GL_API::RegisterBufferLock({ bufferImpl, offsetInBytes,  rangeInBytes }, ShaderBufferLockType::AFTER_DRAW_COMMANDS);
        _bufferObjects[buffer]._wasWritten = false;
    }
}

void glGenericVertexData::rebuildCountAndIndexData(const U32 drawCount, const  U32 indexCount, const U32 firstIndex, const size_t indexBufferSize) {
    if (_lastDrawCount == drawCount && _lastIndexCount == indexCount && _lastFirstIndex == firstIndex) {
        return;
    }

    const size_t idxCountInternal = drawCount * indexBufferSize;
    if (idxCountInternal >= _indexInfo._indexOffsetData.size()) {
        _indexInfo._indexOffsetData.resize(idxCountInternal, _lastFirstIndex);
    }

    if (_lastDrawCount != drawCount) {
        if (drawCount >= _indexInfo._countData.size()) {
            // No need to resize down. Cheap to keep in memory.
            _indexInfo._countData.resize(drawCount, _lastIndexCount);
        }
        _lastDrawCount = drawCount;
    }

    if (_lastIndexCount != indexCount) {
        eastl::fill(begin(_indexInfo._countData), begin(_indexInfo._countData) + drawCount, indexCount);
        _lastIndexCount = indexCount;
    }

    if (_lastFirstIndex != firstIndex) {
        if (idxCountInternal > 0u) {
            eastl::fill(begin(_indexInfo._indexOffsetData), begin(_indexInfo._indexOffsetData) + idxCountInternal, firstIndex);
        }
        _lastFirstIndex = firstIndex;
    }

}

};
