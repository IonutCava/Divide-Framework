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
    _maxSize = _usage == Usage::CONSTANT_BUFFER ? GL_API::s_UBMaxSize : GL_API::s_SSBMaxSize;

    _alignedBufferSize = static_cast<ptrdiff_t>(realign_offset(_params._elementCount * _params._elementSize, AlignmentRequirement(_usage)));

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

    _bufferImpl = MemoryManager_NEW glBufferImpl(context, implParams);

    // Just to avoid issues with reading undefined or zero-initialised memory.
    // This is quite fast so far so worth it for now.
    if (descriptor._separateReadWrite && descriptor._bufferParams._initialData.second > 0) {
        assert(descriptor._bufferParams._updateFrequency != BufferUpdateFrequency::ONCE);

        for (U32 i = 1; i < descriptor._ringBufferLength; ++i) {
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
        byteOffset = (byteOffset + req - 1) / req * req;
    }

    return byteOffset;
}

ptrdiff_t glUniformBuffer::getCorrectedOffset(ptrdiff_t offsetInBytes) const noexcept {
    offsetInBytes = getAlignmentCorrected(offsetInBytes);
    if (queueLength() > 1) {
        offsetInBytes += queueReadIndex() * _alignedBufferSize;
    }
    return offsetInBytes;
}

void glUniformBuffer::clearBytes(const ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes) {
    OPTICK_EVENT();

    if (rangeInBytes > 0) {
        assert(offsetInBytes + rangeInBytes <= _alignedBufferSize && "glUniformBuffer::UpdateData error: was called with an invalid range (buffer overflow)!");

        bufferImpl()->writeOrClearBytes(getCorrectedOffset(offsetInBytes), rangeInBytes, nullptr, true);
    }
}

void glUniformBuffer::readBytes(const ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes, bufferPtr result) const {
    OPTICK_EVENT();

    if (rangeInBytes > 0) {
        bufferImpl()->readBytes(getCorrectedOffset(offsetInBytes), rangeInBytes, result);
    }
}

void glUniformBuffer::writeBytes(const ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes, bufferPtr data) {
    OPTICK_EVENT();

    if (rangeInBytes > 0) {
        assert(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        bufferImpl()->writeOrClearBytes(getCorrectedOffset(offsetInBytes), rangeInBytes, data, false);
    }
}

bool glUniformBuffer::bindByteRange(const U8 bindIndex, const ptrdiff_t offsetInBytes, const ptrdiff_t rangeInBytes) {
    if (rangeInBytes > 0) {
        OPTICK_EVENT();

        assert(to_size(rangeInBytes) <= _maxSize && "glUniformBuffer::bindByteRange: attempted to bind a larger shader block than is allowed on the current platform");
        assert(offsetInBytes == getAlignmentCorrected(offsetInBytes));
        return bufferImpl()->bindByteRange(bindIndex, getCorrectedOffset(offsetInBytes), getAlignmentCorrected(rangeInBytes));
    }

    return false;
}

void glUniformBuffer::OnGLInit() noexcept {
    s_boundAlignmentRequirement = GL_API::s_UBOffsetAlignment;
    s_unboundAlignmentRequirement = GL_API::s_SSBOffsetAlignment;
}

}  // namespace Divide
