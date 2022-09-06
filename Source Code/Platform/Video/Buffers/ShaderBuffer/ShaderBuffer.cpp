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

void ShaderBuffer::readData(const BufferRange range, const std::pair<bufferPtr, size_t> outData) const {
    readBytes(
        {
            range._startOffset * _params._elementSize,
            range._length * _params._elementSize
        },
        outData);
}

} //namespace Divide;