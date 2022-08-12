#include "stdafx.h"

#include "Headers/glGenericVertexData.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

glGenericVertexData::IndexBufferEntry::~IndexBufferEntry()
{
    if (_handle != GLUtil::k_invalidObjectID) {
        GLUtil::freeBuffer(_handle);
    }
}

glGenericVertexData::glGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
    : GenericVertexData(context, ringBufferLength, name)
{
}

void glGenericVertexData::reset() {
    _bufferObjects.clear();
    _idxBuffers.clear();
}

/// Submit a draw command to the GPU using this object and the specified command
void glGenericVertexData::draw(const GenericDrawCommand& command, [[maybe_unused]]VDIUserData* userData) {
    DIVIDE_ASSERT(GL_API::GetStateTracker()->_primitiveRestartEnabled == primitiveRestartRequired());
    DIVIDE_ASSERT(_idxBuffers.size() > command._bufferFlag);
    _lockManager.wait(false);

    // Update buffer bindings
    for (const auto& buffer : _bufferObjects) {
        bindBufferInternal(buffer._bindConfig);
    }

    const auto& idxBuffer = _idxBuffers[command._bufferFlag];
    if (GL_API::GetStateTracker()->setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, idxBuffer._handle) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    if (!renderIndirect() &&
        command._cmd.primCount == 1u &&
        command._drawCount > 1u)
    {
        rebuildCountAndIndexData(command._drawCount, static_cast<GLsizei>(command._cmd.indexCount), command._cmd.firstIndex, idxBuffer._data.count);
    }

    // Submit the draw command
    GLUtil::SubmitRenderCommand(command,
                                renderIndirect(),
                                idxBuffer._data.smallIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                _indexInfo._countData.data(),
                                (bufferPtr)_indexInfo._indexOffsetData.data());

    lockBuffersInternal(false);
}

void glGenericVertexData::setIndexBuffer(const IndexBuffer& indices) {
    IndexBufferEntry* oldIdxBufferEntry = nullptr;

    bool found = false;
    for (auto& idxBuffer : _idxBuffers) {
        if (idxBuffer._data.id == indices.id) {
            oldIdxBufferEntry = &idxBuffer;
            found = true;
            break;
        }
    }

    if (!found) {
        _idxBuffers.push_back({ indices, GLUtil::k_invalidObjectID });
        oldIdxBufferEntry = &_idxBuffers.back();
    } else if (oldIdxBufferEntry->_handle != GLUtil::k_invalidObjectID &&
               (!AreCompatible(oldIdxBufferEntry->_data, indices) || indices.count == 0u))
    {
        GLUtil::freeBuffer(oldIdxBufferEntry->_handle);
    }

    IndexBuffer& oldIdxBuffer = oldIdxBufferEntry->_data;

    oldIdxBuffer.count = std::max(oldIdxBuffer.count, indices.count);

    if (indices.count > 0u) {
        const size_t elementSize = indices.smallIndices ? sizeof(GLushort) : sizeof(GLuint);

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

        if (oldIdxBufferEntry->_handle == GLUtil::k_invalidObjectID) {
            const size_t newDataSize = indices.count * elementSize;
            _indexBufferSize = std::max(newDataSize, _indexBufferSize);

            GLUtil::createBuffer(_indexBufferSize,
                                 oldIdxBufferEntry->_handle,
                                 _name.empty() ? nullptr : (_name + "_index").c_str());
        }

        const size_t range = indices.count * elementSize;
        DIVIDE_ASSERT(range <= _indexBufferSize);

        if (range == _indexBufferSize) {
            glNamedBufferData(oldIdxBufferEntry->_handle, range, data, indices.dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
        } else {
            glInvalidateBufferSubData(oldIdxBufferEntry->_handle, 0u, range);
            glNamedBufferSubData(oldIdxBufferEntry->_handle, 0u, range, data);
        }
        if (!Runtime::isMainThread()) {
            _lockManager.lock();
        }
    }
}

/// Specify the structure and data of the given buffer
void glGenericVertexData::setBuffer(const SetBufferParams& params) {
    // Make sure we specify buffers in order.
    GenericBufferImpl* impl = nullptr;
    for (auto& buffer : _bufferObjects) {
        if (buffer._bindConfig._bufferIdx == params._bindConfig._bufferIdx) {
            impl = &buffer;
            break;
        }
    }    
    if (impl == nullptr) {
        impl = &_bufferObjects.emplace_back();
    }

    const size_t ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
    const size_t bufferSizeInBytes = params._bufferParams._elementCount * params._bufferParams._elementSize;

    BufferImplParams implParams;
    implParams._bufferParams = params._bufferParams;
    implParams._dataSize = bufferSizeInBytes * ringSizeFactor;
    implParams._target = GL_ARRAY_BUFFER;
    implParams._useChunkAllocation = true;

    const size_t elementStride = params._elementStride == SetBufferParams::INVALID_ELEMENT_STRIDE
                                                    ? params._bufferParams._elementSize
                                                    : params._elementStride;
    impl->_ringSizeFactor = ringSizeFactor;
    impl->_useAutoSyncObjects = params._useAutoSyncObjects;
    impl->_bindConfig = params._bindConfig;
    impl->_elementStride = elementStride;

    bool skipUpdate = false;
    if (impl->_buffer == nullptr || impl->_buffer->params() != implParams) {
        impl->_buffer = eastl::make_unique<glBufferImpl>(_context, implParams, params._initialData, _name.empty() ? nullptr : _name.c_str());
        for (U32 i = 1u; i < ringSizeFactor; ++i) {
            impl->_buffer->writeOrClearBytes(
                i * bufferSizeInBytes,
                params._initialData.second > 0 ? params._initialData.second  : bufferSizeInBytes, 
                params._initialData.first, 
                params._initialData.first == nullptr,
                true);
        }
        skipUpdate = true;
    }

    if (!skipUpdate) {
        updateBuffer(params._bindConfig._bufferIdx, 0, params._bufferParams._elementCount, params._initialData.first);
    }
}

/// Update the elementCount worth of data contained in the buffer starting from elementCountOffset size offset
void glGenericVertexData::updateBuffer(const U32 buffer,
                                       const U32 elementCountOffset,
                                       const U32 elementCountRange,
                                       const bufferPtr data) {
    GenericBufferImpl* impl = nullptr;
    for (auto& bufferImpl : _bufferObjects) {
        if (bufferImpl._bindConfig._bufferIdx == buffer) {
            impl = &bufferImpl;
            break;
        }
    }

    DIVIDE_ASSERT(impl != nullptr && "glGenericVertexData error: set buffer called for invalid buffer index!");

    const BufferParams& bufferParams = impl->_buffer->params()._bufferParams;

    // Calculate the size of the data that needs updating
    const size_t dataCurrentSizeInBytes = elementCountRange * bufferParams._elementSize;
    // Calculate the offset in the buffer in bytes from which to start writing
    size_t offsetInBytes = elementCountOffset * bufferParams._elementSize;

    if (impl->_ringSizeFactor > 1u) {
        offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
    }

    impl->_buffer->writeOrClearBytes(offsetInBytes, dataCurrentSizeInBytes, data, false);
    Merge(impl->_writtenRange, {offsetInBytes, dataCurrentSizeInBytes});
}

void glGenericVertexData::bindBufferInternal(const SetBufferParams::BufferBindConfig& bindConfig) {
    GenericBufferImpl* impl = nullptr;
    for (auto& bufferImpl : _bufferObjects) {
        if (bufferImpl._bindConfig._bufferIdx == bindConfig._bufferIdx) {
            impl = &bufferImpl;
            break;
        }
    }

    if (impl == nullptr) {
        return;
    }

    const BufferParams& bufferParams = impl->_buffer->params()._bufferParams;
    size_t offsetInBytes = impl->_buffer->memoryBlock()._offset;

    if (impl->_ringSizeFactor > 1) {
        offsetInBytes += bufferParams._elementCount * bufferParams._elementSize * queueIndex();
    }

    const GLStateTracker::BindResult ret = 
        GL_API::GetStateTracker()->bindActiveBuffer(bindConfig._bindIdx,
                                                    impl->_buffer->memoryBlock()._bufferHandle,
                                                    offsetInBytes,
                                                    impl->_elementStride);

    if (ret == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }

    impl->_usedAfterWrite = impl->_writtenRange._length > 0u;
}

void glGenericVertexData::lockBuffersInternal(const bool force) {
    SyncObject* sync = nullptr;
    for (const auto& buffer : _bufferObjects) {
        if ((buffer._useAutoSyncObjects || force) && buffer._usedAfterWrite) {
            sync = glLockManager::CreateSyncObject();
            break;
        }
    }

    if (sync != nullptr) {
        for (auto& buffer : _bufferObjects) {
            if ((buffer._useAutoSyncObjects || force) && buffer._usedAfterWrite) {
                DIVIDE_ASSERT(buffer._writtenRange._length > 0u);

                if (!buffer._buffer->lockByteRange(buffer._writtenRange, sync)) {
                    DIVIDE_UNEXPECTED_CALL();
                }
                buffer._writtenRange = {};
                buffer._usedAfterWrite = false;
            }
        }
    }
}

void glGenericVertexData::rebuildCountAndIndexData(const U32 drawCount, const GLsizei indexCount, const U32 firstIndex, const size_t indexBufferSize) {
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
