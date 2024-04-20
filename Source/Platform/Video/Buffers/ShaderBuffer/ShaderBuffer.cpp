

#include "Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

size_t ShaderBuffer::AlignmentRequirement(const BufferUsageType usage) noexcept {
    switch ( usage )
    {
        case BufferUsageType::COUNT: DIVIDE_UNEXPECTED_CALL(); break;

        case BufferUsageType::UNBOUND_BUFFER :
        case BufferUsageType::COMMAND_BUFFER : return GFXDevice::GetDeviceInformation()._offsetAlignmentBytesSSBO;
        case BufferUsageType::CONSTANT_BUFFER: return GFXDevice::GetDeviceInformation()._offsetAlignmentBytesUBO;

        case BufferUsageType::VERTEX_BUFFER:
        case BufferUsageType::INDEX_BUFFER:
        case BufferUsageType::STAGING_BUFFER: break;
    };

    return sizeof(U32);
}

ShaderBuffer::ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
      : GUIDWrapper()
      , GraphicsResource(context, Type::SHADER_BUFFER, getGUID(), _ID(descriptor._name.c_str()))
      , RingBufferSeparateWrite(descriptor._ringBufferLength, descriptor._separateReadWrite)
      , _alignmentRequirement(AlignmentRequirement(descriptor._bufferParams._flags._usageType))
      , _name(descriptor._name)
      , _params(descriptor._bufferParams)
{
    assert(descriptor._bufferParams._flags._usageType != BufferUsageType::COUNT);
    assert(descriptor._bufferParams._elementSize * descriptor._bufferParams._elementCount > 0 && "ShaderBuffer::Create error: Invalid buffer size!");
    _maxSize = descriptor._bufferParams._flags._usageType == BufferUsageType::CONSTANT_BUFFER ? GFXDevice::GetDeviceInformation()._maxSizeBytesUBO : GFXDevice::GetDeviceInformation()._maxSizeBytesSSBO;
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
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    DIVIDE_ASSERT(range._length > 0u &&
                  _params._hostVisible &&
                  getUsage() == BufferUsageType::UNBOUND_BUFFER &&
                  range._startOffset == Util::GetAlignmentCorrected(range._startOffset, _alignmentRequirement));

    range._startOffset += getStartOffset(true);
    readBytesInternal(range, outData);
    _lastReadFrame = GFXDevice::FrameCount();
}

BufferLock ShaderBuffer::clearBytes(const BufferRange range) {
    return writeBytes(range, nullptr);
}

BufferLock ShaderBuffer::writeBytes(BufferRange range, const bufferPtr data) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    DIVIDE_ASSERT(range._length > 0 &&
                  getUpdateFrequency() != BufferUpdateFrequency::ONCE &&
                  range._startOffset == Util::GetAlignmentCorrected(range._startOffset, _alignmentRequirement));

    range._startOffset += getStartOffset(false);
    _lastWriteFrameNumber = GFXDevice::FrameCount();

    return writeBytesInternal(range, data);
}

BufferUpdateUsage ShaderBuffer::getUpdateUsage() const noexcept
{
    return _params._flags._updateUsage;
}

BufferUpdateFrequency ShaderBuffer::getUpdateFrequency() const noexcept
{
    return _params._flags._updateFrequency;
}

} //namespace Divide
