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
    implParams._useChunkAllocation = params._usage != GL_ELEMENT_ARRAY_BUFFER;

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

GLuint glGenericBuffer::bufferHandle() const noexcept {
    return bufferImpl()->memoryBlock()._bufferHandle;
}

size_t glGenericBuffer::dataOffset() const noexcept {
    return bufferImpl()->memoryBlock()._offset;
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

bool glGenericBuffer::needsSynchronization() const noexcept {
    if (!bufferImpl()) {
        return false;
    }

    return bufferImpl()->params()._bufferParams._sync;
}


}; //namespace Divide