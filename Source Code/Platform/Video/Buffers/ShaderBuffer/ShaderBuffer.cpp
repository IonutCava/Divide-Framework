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

ShaderBuffer::ShaderBuffer(GFXDevice& context,
                           const ShaderBufferDescriptor& descriptor)
      : GraphicsResource(context, Type::SHADER_BUFFER, getGUID(), _ID(descriptor._name.c_str())),
        RingBufferSeparateWrite(descriptor._ringBufferLength, descriptor._separateReadWrite),
        _params(descriptor._bufferParams),
        _flags(descriptor._flags),
        _usage(descriptor._usage),
        _name(descriptor._name)
{
    if (descriptor._bufferParams._updateFrequency == BufferUpdateFrequency::RARELY || BitCompare(_flags, Flags::NO_SYNC)) {
        _params._sync = false;
    }

    assert(descriptor._usage != Usage::COUNT);
    assert(descriptor._bufferParams._elementSize * descriptor._bufferParams._elementCount > 0 && "ShaderBuffer::Create error: Invalid buffer size!");
}

void ShaderBuffer::clearData(const U32 offsetElementCount, const U32 rangeElementCount) {
    clearBytes(static_cast<ptrdiff_t>(offsetElementCount * _params._elementSize),
               static_cast<ptrdiff_t>(rangeElementCount * _params._elementSize));
}

void ShaderBuffer::writeData(const U32 offsetElementCount, const U32 rangeElementCount, const bufferPtr data) {
    writeBytes(static_cast<ptrdiff_t>(offsetElementCount * _params._elementSize),
               static_cast<ptrdiff_t>(rangeElementCount * _params._elementSize),
               data);
}

void ShaderBuffer::readData(const U32 offsetElementCount, const U32 rangeElementCount, const bufferPtr result) const {
    readBytes(static_cast<ptrdiff_t>(offsetElementCount * _params._elementSize),
              static_cast<ptrdiff_t>(rangeElementCount * _params._elementSize),
              result);
}

bool ShaderBuffer::bindRange(const U8 bindIndex,
                             const U32 offsetElementCount,
                             const U32 rangeElementCount) {
    assert(rangeElementCount > 0);

    return bindByteRange(bindIndex,
                         static_cast<ptrdiff_t>(offsetElementCount * _params._elementSize),
                         static_cast<ptrdiff_t>(rangeElementCount * _params._elementSize));
}


} //namespace Divide;