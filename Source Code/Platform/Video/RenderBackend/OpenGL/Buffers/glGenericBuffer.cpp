#include "stdafx.h"

#include "Headers/glGenericBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"

namespace Divide {
glGenericBuffer::glGenericBuffer(GFXDevice& context, const GenericBufferParams& params)
    : _elementCount(params._bufferParams._elementCount),
      _ringSizeFactor(params._ringSizeFactor)
{
    const size_t bufferSizeInBytes = _elementCount * params._bufferParams._elementSize;
    const size_t totalSizeInBytes = bufferSizeInBytes * _ringSizeFactor;
    
    BufferImplParams implParams;
    implParams._bufferParams = params._bufferParams;
    implParams._dataSize = totalSizeInBytes;
    implParams._target = params._usage;
    implParams._name = params._name;
    implParams._explicitFlush = true;

    _bufferImpl = MemoryManager_NEW glBufferImpl(context, implParams);

    if (params._bufferParams._initialData.second > 0) {
        for (U32 i = 1; i < _ringSizeFactor; ++i) {
            bufferImpl()->writeOrClearBytes(i * bufferSizeInBytes, params._bufferParams._initialData.second, params._bufferParams._initialData.first, false);
        }
    }
}

glGenericBuffer::~glGenericBuffer()
{
    MemoryManager::DELETE(_bufferImpl);
}

GLuint glGenericBuffer::bufferHandle() const {
    return bufferImpl()->memoryBlock()._bufferHandle;
}

void glGenericBuffer::writeData(const GLuint elementCount,
                                const GLuint elementOffset,
                                const GLuint ringWriteOffset,
                                bufferPtr data) const
{
    // Calculate the size of the data that needs updating
    const size_t dataCurrentSizeInBytes = elementCount * bufferImpl()->params()._bufferParams._elementSize;
    // Calculate the offset in the buffer in bytes from which to start writing
    size_t offsetInBytes = elementOffset * bufferImpl()->params()._bufferParams._elementSize;

    if (_ringSizeFactor > 1) {
        offsetInBytes += _elementCount * bufferImpl()->params()._bufferParams._elementSize * ringWriteOffset;
    }

    bufferImpl()->writeOrClearBytes(offsetInBytes, dataCurrentSizeInBytes, data, false);
}

void glGenericBuffer::readData(const GLuint elementCount,
                               const GLuint elementOffset,
                               const GLuint ringReadOffset,
                               bufferPtr dataOut) const
{
    // Calculate the size of the data that needs updating
    const size_t dataCurrentSizeInBytes = elementCount * bufferImpl()->params()._bufferParams._elementSize;
    // Calculate the offset in the buffer in bytes from which to start writing
    size_t offsetInBytes = elementOffset * bufferImpl()->params()._bufferParams._elementSize;

    if (_ringSizeFactor > 1) {
        offsetInBytes += _elementCount * bufferImpl()->params()._bufferParams._elementSize * ringReadOffset;
    }

    bufferImpl()->readBytes(offsetInBytes, dataCurrentSizeInBytes, dataOut);
}

void glGenericBuffer::clearData(const GLuint elementOffset, const GLuint ringWriteOffset) const
{
    const size_t rangeInBytes = _elementCount * bufferImpl()->params()._bufferParams._elementSize;
    size_t offsetInBytes = elementOffset * bufferImpl()->params()._bufferParams._elementSize;

    if (_ringSizeFactor > 1) {
        offsetInBytes += rangeInBytes * ringWriteOffset;
    }

    bufferImpl()->writeOrClearBytes(offsetInBytes, rangeInBytes, nullptr, true);
}

void glVertexDataContainer::rebuildCountAndIndexData(const U32 drawCount, const  U32 indexCount, const U32 firstIndex, const size_t indexBufferSize) {
    if (_lastDrawCount == drawCount && _lastIndexCount == indexCount && _lastFirstIndex == firstIndex) {
        return;
    }
    if (_indexInfo == nullptr) {
        _indexInfo = eastl::make_unique<glVertexDataIndexContainer>();
    }

    if (_lastDrawCount != drawCount || _lastIndexCount != indexCount) {
        if (drawCount >= _indexInfo->_countData.size()) {
            _indexInfo->_countData.resize(drawCount);
        }

        eastl::fill(begin(_indexInfo->_countData), begin(_indexInfo->_countData) + drawCount, indexCount);
    }

    if (indexBufferSize > 0 && (_lastDrawCount != drawCount || _lastFirstIndex != firstIndex)) {
        const U32 idxCountInternal = to_U32(drawCount * indexBufferSize);

        if (idxCountInternal >= _indexInfo->_indexOffsetData.size()) {
            _indexInfo->_indexOffsetData.resize(idxCountInternal);
        }

        if (_lastFirstIndex != firstIndex) {
            eastl::fill(begin(_indexInfo->_indexOffsetData), begin(_indexInfo->_indexOffsetData) + idxCountInternal, firstIndex);
        }
    }
    _lastDrawCount = drawCount;
    _lastIndexCount = indexCount;
    _lastFirstIndex = firstIndex;
}

}; //namespace Divide