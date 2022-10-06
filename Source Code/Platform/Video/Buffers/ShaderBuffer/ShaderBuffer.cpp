#include "stdafx.h"

#include "Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

size_t ShaderBuffer::AlignmentRequirement(const Usage usage) noexcept {
    if (usage == Usage::CONSTANT_BUFFER) {
        return GFXDevice::GetDeviceInformation()._UBOffsetAlignmentBytes;
    }
    if (usage == Usage::UNBOUND_BUFFER || usage == Usage::COMMAND_BUFFER) {
        return GFXDevice::GetDeviceInformation()._SSBOffsetAlignmentBytes;
    }

    return sizeof(U32);
}

ShaderBuffer::ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
      : GUIDWrapper(),
        GraphicsResource(context, Type::SHADER_BUFFER, getGUID(), _ID(descriptor._name.c_str())),
        RingBufferSeparateWrite(descriptor._ringBufferLength, descriptor._separateReadWrite),
        _params(descriptor._bufferParams),
        _usage(descriptor._usage),
        _name(descriptor._name)
{
    assert(descriptor._usage != Usage::COUNT);
    assert(descriptor._bufferParams._elementSize * descriptor._bufferParams._elementCount > 0 && "ShaderBuffer::Create error: Invalid buffer size!");
    _maxSize = _usage == Usage::CONSTANT_BUFFER ? GFXDevice::GetDeviceInformation()._UBOMaxSizeBytes : GFXDevice::GetDeviceInformation()._SSBOMaxSizeBytes;
}

BufferLock ShaderBuffer::clearData(const BufferRange range) {
    return clearBytes(
               {
                   range._startOffset * _params._elementSize,
                   range._length * _params._elementSize
               });
}

BufferLock ShaderBuffer::writeData(const BufferRange range, const bufferPtr data) {
    return writeBytes(
               {
                   range._startOffset * _params._elementSize,
                   range._length * _params._elementSize
               },
               data);
}

void ShaderBuffer::readData(const BufferRange range, const std::pair<bufferPtr, size_t> outData) {
    readBytes(
        {
            range._startOffset * _params._elementSize,
            range._length * _params._elementSize
        },
        outData);
}

void ShaderBuffer::readBytes(BufferRange range, std::pair<bufferPtr, size_t> outData) {
    PROFILE_SCOPE();

    DIVIDE_ASSERT(range._length > 0u &&
                  _params._hostVisible &&
                  _usage == ShaderBuffer::Usage::UNBOUND_BUFFER  &&
                  range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));

    range._startOffset += getStartOffset(true);
    readBytesInternal(range, outData);
    _lastReadFrame = GFXDevice::FrameCount();
}

BufferLock ShaderBuffer::clearBytes(const BufferRange range) {
    return writeBytes(range, nullptr);
}

BufferLock ShaderBuffer::writeBytes(BufferRange range, bufferPtr data) {
    PROFILE_SCOPE();

    DIVIDE_ASSERT(range._length > 0 &&
                  _params._updateFrequency != BufferUpdateFrequency::ONCE &&
                  range._startOffset == Util::GetAlignmentCorrected(range._startOffset, AlignmentRequirement(_usage)));

    range._startOffset += getStartOffset(false);
    writeBytesInternal(range, data);
    _lastWriteFrameNumber = GFXDevice::FrameCount();

    return { this, range };
}
} //namespace Divide;