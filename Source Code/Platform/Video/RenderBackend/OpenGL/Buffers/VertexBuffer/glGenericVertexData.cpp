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
    for (auto* it :_bufferObjects ) {
        MemoryManager::DELETE(it);
    }

    // Make sure we don't have any of our VAOs bound
    if (GL_API::GetStateTracker().setActiveVAO(0) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    // Delete the rendering VAO
    if (_vertexArray != GLUtil::k_invalidObjectID) {
        GL_API::DeleteVAOs(1, &_vertexArray);
    }

    GLUtil::freeBuffer(_indexBuffer);
}

/// Create the specified number of buffers and queries and attach them to this vertex data container
void glGenericVertexData::create(const U8 numBuffers) {
    // Prevent double create
    assert(_bufferObjects.empty() && "glGenericVertexData error: create called with no buffers specified!");
    // Create our buffer objects
    _bufferObjects.resize(numBuffers, nullptr);
    _instanceDivisor.resize(numBuffers, 0);
}

/// Submit a draw command to the GPU using this object and the specified command
void glGenericVertexData::draw(const GenericDrawCommand& command) {
    if (_vertexArray == GLUtil::k_invalidObjectID) {
        glCreateVertexArrays(1, &_vertexArray);
        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            glObjectLabel(GL_VERTEX_ARRAY, _vertexArray, -1, _name.c_str());
        }
    }

    // Update buffer bindings
    if (!_bufferObjects.empty()) {
        if (command._bufferIndex == GenericDrawCommand::INVALID_BUFFER_INDEX) {
            for (U32 i = 0u; i < _bufferObjects.size(); ++i) {
                bindBufferInternal(i, i);
            }
        } else {
            bindBufferInternal(command._bufferIndex, 0u);
        }
    }
    // Update vertex attributes if needed (e.g. if offsets changed)
    for (auto& [buf, descriptor] : _attributeMapDraw) {
        setAttributeInternal(command, descriptor);
    }

    // Delay this for as long as possible
    if (GL_API::GetStateTracker().setActiveVAO(_vertexArray) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (_idxBufferDirty) {
        if (GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
        _idxBufferDirty = false;
    }
    GL_API::GetStateTracker().togglePrimitiveRestart(primitiveRestartEnabled());
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

void glGenericVertexData::setIndexBuffer(const IndexBuffer& indices, const BufferUpdateFrequency updateFrequency) {
    GenericVertexData::setIndexBuffer(indices, updateFrequency);

    if (_indexBuffer != 0) {
        GLUtil::freeBuffer(_indexBuffer);
        _indexDataType = GL_NONE;
    }

    _indexBufferUsage = glBufferImpl::GetBufferUsage(updateFrequency, BufferUpdateUsage::CPU_W_GPU_R);

    if (indices.count > 0) {
        updateIndexBuffer(indices);
    }

    _idxBufferDirty = true;
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
        _idxBufferDirty = true;
    } else {
        const size_t offset = indices.offsetCount * elementSize;
        const size_t count = indices.count * elementSize;
        DIVIDE_ASSERT(offset + count < _indexBufferSize);

        glInvalidateBufferSubData(_indexBuffer, offset, count);
        glNamedBufferSubData(_indexBuffer, offset, count, data);
    }
}

/// Specify the structure and data of the given buffer
void glGenericVertexData::setBuffer(const SetBufferParams& params) {
    const U32 buffer = params._buffer;

    // Make sure the buffer exists
    DIVIDE_ASSERT(buffer >= 0 && buffer < _bufferObjects.size() &&
           "glGenericVertexData error: set buffer called for invalid buffer index!");

    if (_bufferObjects[buffer] != nullptr) {
        MemoryManager::DELETE(_bufferObjects[buffer]);
    }

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

    if (GL_API::GetStateTracker().bindActiveBuffer(_vertexArray, location, buffer->bufferHandle(), _instanceDivisor[bufferIdx], entry._offset, elementSize) == GLStateTracker::BindResult::FAILED) {
        DIVIDE_UNEXPECTED_CALL();
    }
    GL_API::RegisterBufferBind(MOV(entry), true);
}

/// Update internal attribute data
void glGenericVertexData::setAttributeInternal([[maybe_unused]] const GenericDrawCommand& command, AttributeDescriptor& descriptor) const {
    // Early out if the attribute didn't change
    if (!descriptor.dirty()) {
        return;
    }

    // If the attribute wasn't activate until now, enable it
    if (!descriptor.wasSet()) {
        assert(descriptor.index() < GFXDevice::GetDeviceInformation()._maxVertAttributeBindings &&
               "GL Wrapper: insufficient number of attribute binding locations available on current hardware!");

        if (descriptor.enabled()) {
            glEnableVertexArrayAttrib(_vertexArray, descriptor.index());
            glVertexArrayAttribBinding(_vertexArray, descriptor.index(), descriptor.parentBuffer());
        } else {
            glDisableVertexArrayAttrib(_vertexArray, descriptor.index());
        }
        descriptor.wasSet(true);
    }

    if (descriptor.enabled()) {
        // Update the attribute data
        const GFXDataFormat format = descriptor.dataType();
        const bool isIntegerType = format != GFXDataFormat::FLOAT_16 &&
                                   format != GFXDataFormat::FLOAT_32;
    
        if (!isIntegerType || descriptor.normalized()) {
            glVertexArrayAttribFormat(_vertexArray,
                                      descriptor.index(),
                                      descriptor.componentsPerElement(),
                                      GLUtil::glDataFormat[to_U32(format)],
                                      descriptor.normalized() ? GL_TRUE : GL_FALSE,
                                      static_cast<GLuint>(descriptor.strideInBytes()));
        } else {
            glVertexArrayAttribIFormat(_vertexArray,
                                       descriptor.index(),
                                       descriptor.componentsPerElement(),
                                       GLUtil::glDataFormat[to_U32(format)],
                                       static_cast<GLuint>(descriptor.strideInBytes()));
        }
    }

    // Inform the descriptor that the data was updated
    descriptor.clean();
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
