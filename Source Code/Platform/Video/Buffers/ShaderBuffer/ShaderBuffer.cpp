#include "stdafx.h"

#include "Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

size_t ShaderBuffer::s_boundAlignmentRequirement = 0;
size_t ShaderBuffer::s_unboundAlignmentRequirement = 0;

ShaderBuffer::ShaderBuffer(GFXDevice& context,
                           const ShaderBufferDescriptor& descriptor)
      : GraphicsResource(context, Type::SHADER_BUFFER, getGUID(), _ID(descriptor._name.c_str())),
        RingBufferSeparateWrite(descriptor._ringBufferLength, descriptor._separateReadWrite),
        _params(descriptor._bufferParams),
        _flags(descriptor._flags),
        _usage(descriptor._usage),
        _name(descriptor._name)
{
    if (_params._sync) {
        _params._sync = !(BitCompare(_flags, Flags::NO_SYNC) || descriptor._bufferParams._updateFrequency == BufferUpdateFrequency::ONCE);
    }

    assert(descriptor._usage != Usage::COUNT);
    assert(descriptor._bufferParams._elementSize * descriptor._bufferParams._elementCount > 0 && "ShaderBuffer::Create error: Invalid buffer size!");
}

void ShaderBuffer::writeData(const bufferPtr data) {
    writeBytes(0, static_cast<ptrdiff_t>(_params._elementCount * _params._elementSize), data);
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

size_t ShaderBuffer::AlignmentRequirement(const Usage usage) noexcept {
    return usage == Usage::CONSTANT_BUFFER ? s_boundAlignmentRequirement : 
                    (usage == Usage::UNBOUND_BUFFER  || usage == Usage::COMMAND_BUFFER)
                        ? s_unboundAlignmentRequirement 
                        : sizeof(U32);
}

bool ShaderBuffer::bind(const U8 bindIndex) {
    return bindByteRange(bindIndex, 0, static_cast<ptrdiff_t>(_params._elementCount * _params._elementSize));
}

bool ShaderBuffer::bind(const ShaderBufferLocation bindIndex) {
    return bind(to_U8(bindIndex));
}

bool ShaderBuffer::bindRange(const U8 bindIndex,
                             const U32 offsetElementCount,
                             const U32 rangeElementCount) {
    assert(rangeElementCount > 0);

    return bindByteRange(bindIndex,
                         static_cast<ptrdiff_t>(offsetElementCount * _params._elementSize),
                         static_cast<ptrdiff_t>(rangeElementCount * _params._elementSize));
}

bool ShaderBuffer::bindRange(const ShaderBufferLocation bindIndex,
                             const U32 offsetElementCount,
                             const U32 rangeElementCount) {
    return bindRange(to_U8(bindIndex), offsetElementCount, rangeElementCount);
}

} //namespace Divide;