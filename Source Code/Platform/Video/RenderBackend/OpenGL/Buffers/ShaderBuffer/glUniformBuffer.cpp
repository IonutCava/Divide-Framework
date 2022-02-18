#include "stdafx.h"

#include "Headers/glUniformBuffer.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Utility/Headers/Localization.h"

#include <iomanip>

namespace Divide {

glUniformBuffer::glUniformBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
    : ShaderBuffer(context, descriptor)
{
    _maxSize = _usage == Usage::CONSTANT_BUFFER ? GFXDevice::GetDeviceInformation()._UBOMaxSizeBytes : GFXDevice::GetDeviceInformation()._SSBOMaxSizeBytes;

    const size_t targetElementSize = getAlignmentCorrected(_params._elementSize);
    if (targetElementSize > _params._elementSize) {
        DIVIDE_ASSERT((_params._elementSize * _params._elementCount) % AlignmentRequirement(_usage) == 0u,
            "ERROR: glUniformBuffer - element size and count combo is less than the minimum alignment requirement for current hardware! Pad the element size and or count a bit");
    } else {
        DIVIDE_ASSERT(_params._elementSize == targetElementSize,
                        "ERROR: glUniformBuffer - element size is less than the minimum alignment requirement for current hardware! Pad the element size a bit");
    }
    _alignedBufferSize = _params._elementCount * _params._elementSize;
    _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset(_alignedBufferSize, AlignmentRequirement(_usage)));

    BufferImplParams implParams;
    implParams._bufferParams = _params;
    implParams._target = (_usage == Usage::UNBOUND_BUFFER || _usage == Usage::COMMAND_BUFFER)
                                 ? GL_SHADER_STORAGE_BUFFER
                                 : _usage == Usage::CONSTANT_BUFFER
                                           ? GL_UNIFORM_BUFFER
                                           : GL_ATOMIC_COUNTER_BUFFER;
    implParams._dataSize = _alignedBufferSize * queueLength();
    implParams._explicitFlush = BitCompare(_flags, Flags::EXPLICIT_RANGE_FLUSH);
    implParams._name = _name.empty() ? nullptr : _name.c_str();
    implParams._useChunkAllocation = _usage != Usage::COMMAND_BUFFER && _usage != Usage::ATOMIC_COUNTER;

    _bufferImpl = MemoryManager_NEW glBufferImpl(context, implParams);

    // Just to avoid issues with reading undefined or zero-initialised memory.
    // This is quite fast so far so worth it for now.
    if (descriptor._separateReadWrite && descriptor._bufferParams._initialData.second > 0) {
        for (U32 i = 1u; i < descriptor._ringBufferLength; ++i) {
            bufferImpl()->writeOrClearBytes(_alignedBufferSize * i, descriptor._bufferParams._initialData.second, descriptor._bufferParams._initialData.first, false);
        }
    }
}

glUniformBuffer::~glUniformBuffer() 
{
    MemoryManager::DELETE(_bufferImpl);
}

ptrdiff_t glUniformBuffer::getAlignmentCorrected(ptrdiff_t byteOffset) const noexcept {
    const size_t req = AlignmentRequirement(_usage);
    if (byteOffset % req != 0) {
        byteOffset = ((byteOffset + req - 1) / req) * req;
    }

    return byteOffset;
}

void glUniformBuffer::clearBytes(ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes) {
    if (rangeInBytes > 0) {
        OPTICK_EVENT();

        DIVIDE_ASSERT(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        assert(offsetInBytes + rangeInBytes <= _alignedBufferSize && "glUniformBuffer::UpdateData error: was called with an invalid range (buffer overflow)!");

        offsetInBytes += queueWriteIndex() * _alignedBufferSize;

        bufferImpl()->writeOrClearBytes(offsetInBytes, rangeInBytes, nullptr, true);
    }
}

void glUniformBuffer::readBytes(ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes, bufferPtr result) const {
    if (rangeInBytes > 0) {
        OPTICK_EVENT();

        DIVIDE_ASSERT(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        offsetInBytes += queueReadIndex() * _alignedBufferSize;

        bufferImpl()->readBytes(offsetInBytes, rangeInBytes, result);
    }
}

void glUniformBuffer::writeBytes(ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes, bufferPtr data) {
    if (rangeInBytes > 0) {
        OPTICK_EVENT();

        DIVIDE_ASSERT(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        offsetInBytes += queueWriteIndex() * _alignedBufferSize;

        bufferImpl()->writeOrClearBytes(offsetInBytes, rangeInBytes, data, false);
    }
}

bool glUniformBuffer::bindByteRange(const U8 bindIndex, ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes) {
    if (rangeInBytes > 0) {
        OPTICK_EVENT();

        DIVIDE_ASSERT(to_size(rangeInBytes) <= _maxSize && "glUniformBuffer::bindByteRange: attempted to bind a larger shader block than is allowed on the current platform");
        DIVIDE_ASSERT(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        offsetInBytes += queueReadIndex() * _alignedBufferSize;
        return bufferImpl()->bindByteRange(bindIndex, offsetInBytes, getAlignmentCorrected(rangeInBytes));
    }

    return false;
}

}  // namespace Divide
