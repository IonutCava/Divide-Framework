#include "stdafx.h"


#include "Headers/glGenericVertexData.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

glGenericVertexData::glGenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
    : GenericVertexData(context, ringBufferLength, name)
    , _smallIndices(false)
    , _idxBufferDirty(false)
    , _indexBuffer(0u)
    , _indexBufferSize(0u)
    , _indexBufferUsage(GL_NONE)
    , _vertexArray(0u)
{
}

glGenericVertexData::~glGenericVertexData()
{
    for (auto* it :_bufferObjects ) {
        MemoryManager::DELETE(it);
    }

    // Make sure we don't have any of our VAOs bound
    GL_API::getStateTracker().setActiveVAO(0);
    // Delete the rendering VAO
    if (_vertexArray > 0) {
        GL_API::s_vaoPool.deallocate(_vertexArray);
    }

    GLUtil::freeBuffer(_indexBuffer);
}

/// Create the specified number of buffers and queries and attach them to this
/// vertex data container
void glGenericVertexData::create(const U8 numBuffers) {
    // Prevent double create
    assert(_bufferObjects.empty() && "glGenericVertexData error: create called with no buffers specified!");
    GL_API::s_vaoPool.allocate(1, &_vertexArray);
    // Create our buffer objects
    _bufferObjects.resize(numBuffers, nullptr);
    _instanceDivisor.resize(numBuffers, 0);
}

/// Submit a draw command to the GPU using this object and the specified command
void glGenericVertexData::draw(const GenericDrawCommand& command) {
    _bufferLockQueue.resize(0);

    // Update buffer bindings
    setBufferBindings(command);
    // Update vertex attributes if needed (e.g. if offsets changed)
    setAttributes(command);

    // Delay this for as long as possible
    GL_API::getStateTracker().setActiveVAO(_vertexArray);
    if (_idxBufferDirty) {
        GL_API::getStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer);
        _idxBufferDirty = false;
    }    
    // Submit the draw command
    if (renderIndirect()) {
        GLUtil::SubmitRenderCommand(command,
                                    _indexBuffer > 0,
                                    true,
                                    _smallIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT);
    } else {
        rebuildCountAndIndexData(command._drawCount, command._cmd.indexCount, command._cmd.firstIndex, indexBuffer().count);
        GLUtil::SubmitRenderCommand(command,
                                    _indexBuffer > 0,
                                    false,
                                    _smallIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                    _indexInfo->_countData.data(),
                                    (bufferPtr)_indexInfo->_indexOffsetData.data());
    }
}

void glGenericVertexData::lockBuffers() {
    for (const BufferLockEntry& lock : _bufferLockQueue) {
        if (!lock._buffer->lockByteRange(lock._offset, lock._length, _context.frameCount())) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
}

bool glGenericVertexData::waitBufferRange(const U32 buffer, const U32 elementCountOffset, const U32 elementCountRange, [[maybe_unused]] const bool blockClient) {
    if (_bufferObjects.empty()) {
        return false;
    }

    const glGenericBuffer* buf = _bufferObjects[buffer];

    if (!buf->bufferImpl()->params()._bufferParams._sync) {
        const size_t elementSize = buf->bufferImpl()->params()._bufferParams._elementSize;
        const size_t offset = (buf->elementCount() * elementSize * queueIndex()) + elementCountOffset * elementSize;
        return buf->bufferImpl()->waitByteRange(offset, elementCountRange * elementSize, true);
    }

    return false;
}

void glGenericVertexData::setIndexBuffer(const IndexBuffer& indices, const BufferUpdateFrequency updateFrequency) {
    GenericVertexData::setIndexBuffer(indices, updateFrequency);

    if (_indexBuffer != 0) {
        GLUtil::freeBuffer(_indexBuffer);
    }

    if (indices.count > 0) {
        _indexBufferUsage = glBufferImpl::GetBufferUsage(updateFrequency, BufferUpdateUsage::CPU_W_GPU_R);

        // Generate an "Index Buffer Object"
        _indexBufferSize = static_cast<GLuint>(indices.count * (indices.smallIndices ? sizeof(U16) : sizeof(U32)));
        _smallIndices = indices.smallIndices;

        GLUtil::createAndAllocBuffer(
            _indexBufferSize,
            _indexBufferUsage,
            _indexBuffer,
            indices.data,
            _name.empty() ? nullptr : (_name + "_index").c_str());
    }

    _idxBufferDirty = true;
}

void glGenericVertexData::updateIndexBuffer(const IndexBuffer& indices) {
    GenericVertexData::updateIndexBuffer(indices);

    DIVIDE_ASSERT(indices.count > 0, "glGenericVertexData::UpdateIndexBuffer error: Invalid index buffer data!");
    assert(_indexBuffer != 0 && "glGenericVertexData::UpdateIndexBuffer error: no valid index buffer found!");

    const size_t elementSize = indices.smallIndices ? sizeof(GLushort) : sizeof(GLuint);

    if (indices.offsetCount == 0) {
        _indexBufferSize = static_cast<GLuint>(indices.count * elementSize);

        glInvalidateBufferData(_indexBuffer);
        glNamedBufferData(_indexBuffer,
                          _indexBufferSize,
                          indices.data,
                          _indexBufferUsage);
    } else {
        const size_t size = (indices.count + indices.offsetCount) * elementSize;
        DIVIDE_ASSERT(size < _indexBufferSize);
        glInvalidateBufferSubData(_indexBuffer,
                                  indices.offsetCount * elementSize,
                                  indices.count * elementSize);
        glNamedBufferSubData(_indexBuffer,
                             indices.offsetCount * elementSize,
                             indices.count * elementSize,
                             indices.data);
    }
}

/// Specify the structure and data of the given buffer
void glGenericVertexData::setBuffer(const SetBufferParams& params) {
    const U32 buffer = params._buffer;

    // Make sure the buffer exists
    assert(buffer >= 0 && buffer < _bufferObjects.size() &&
           "glGenericVertexData error: set buffer called for invalid buffer index!");

    assert(_bufferObjects[buffer] == nullptr &&
           "glGenericVertexData::setBuffer : buffer re-purposing is not supported at the moment");

    GenericBufferParams paramsOut;
    paramsOut._bufferParams = params._bufferParams;
    paramsOut._usage = GL_ARRAY_BUFFER;
    paramsOut._ringSizeFactor = params._useRingBuffer ? queueLength() : 1;
    paramsOut._name = _name.empty() ? nullptr : _name.c_str();

    glGenericBuffer * tempBuffer = MemoryManager_NEW glGenericBuffer(_context, paramsOut);
    _bufferObjects[buffer] = tempBuffer;
    _instanceDivisor[buffer] = params._instanceDivisor;
}

/// Update the elementCount worth of data contained in the buffer starting from elementCountOffset size offset
void glGenericVertexData::updateBuffer(const U32 buffer,
                                       const U32 elementCountOffset,
                                       const U32 elementCountRange,
                                       const bufferPtr data) {
    _bufferObjects[buffer]->writeData(elementCountRange, elementCountOffset, queueIndex(), data);
}

void glGenericVertexData::bindBufferInternal(const U32 bufferIdx, const  U32 location) {
    const glGenericBuffer* buffer = _bufferObjects[bufferIdx];
    const size_t elementSize = buffer->bufferImpl()->params()._bufferParams._elementSize;

    BufferLockEntry entry;
    entry._buffer = buffer->bufferImpl();
    entry._length = buffer->elementCount() * elementSize;
    entry._offset = entry._length * queueIndex();

    GL_API::getStateTracker().bindActiveBuffer(_vertexArray, location, buffer->bufferHandle(), _instanceDivisor[bufferIdx], entry._offset, elementSize);
    if (!buffer->bufferImpl()->params()._bufferParams._sync) {
        bool foundExistingEntry = false;
        for (BufferLockEntry& lock : _bufferLockQueue) {
            if (lock._buffer->getGUID() == entry._buffer->getGUID()) {
                lock._offset = std::min(lock._offset, entry._offset);
                lock._length = std::max(lock._length, entry._length);
                foundExistingEntry = true;
                break;
            }
        }
        if (!foundExistingEntry) {
            _bufferLockQueue.push_back(entry);
        }
    } else {
        GL_API::RegisterBufferBind(MOV(entry), true);
    }
}

void glGenericVertexData::setBufferBindings(const GenericDrawCommand& command) {
    if (_bufferObjects.empty()) {
        return;
    }

    if (command._bufferIndex == GenericDrawCommand::INVALID_BUFFER_INDEX) {
        for (U32 i = 0u; i < _bufferObjects.size(); ++i) {
            bindBufferInternal(i, i);
        }
    } else {
        bindBufferInternal(command._bufferIndex, 0u);
    }
}

/// Update the appropriate attributes 
void glGenericVertexData::setAttributes(const GenericDrawCommand& command) {
    // Get the appropriate list of attributes and update them in turn
    for (auto& [buf, descriptor] : _attributeMapDraw) {
        setAttributeInternal(command, descriptor);
    }
}

/// Update internal attribute data
void glGenericVertexData::setAttributeInternal([[maybe_unused]] const GenericDrawCommand& command, AttributeDescriptor& descriptor) const {
    // Early out if the attribute didn't change
    if (!descriptor.dirty()) {
        return;
    }

    // If the attribute wasn't activate until now, enable it
    if (!descriptor.wasSet()) {
        assert(descriptor.attribIndex() < GL_API::s_maxAttribBindings &&
               "GL Wrapper: insufficient number of attribute binding locations available on current hardware!");

        glEnableVertexArrayAttrib(_vertexArray, descriptor.attribIndex());
        glVertexArrayAttribBinding(_vertexArray, descriptor.attribIndex(), descriptor.bufferIndex());
        descriptor.wasSet(true);
    }

    // Update the attribute data
    const GFXDataFormat format = descriptor.dataType();

    const bool isIntegerType = format != GFXDataFormat::FLOAT_16 &&
                               format != GFXDataFormat::FLOAT_32;
    
    if (!isIntegerType || descriptor.normalized()) {
        glVertexArrayAttribFormat(_vertexArray,
                                  descriptor.attribIndex(),
                                  descriptor.componentsPerElement(),
                                  GLUtil::glDataFormat[to_U32(format)],
                                  descriptor.normalized() ? GL_TRUE : GL_FALSE,
                                  static_cast<GLuint>(descriptor.strideInBytes()));
    } else {
        glVertexArrayAttribIFormat(_vertexArray,
                                   descriptor.attribIndex(),
                                   descriptor.componentsPerElement(),
                                   GLUtil::glDataFormat[to_U32(format)],
                                   static_cast<GLuint>(descriptor.strideInBytes()));
    }

    // Inform the descriptor that the data was updated
    descriptor.clean();
}

};
