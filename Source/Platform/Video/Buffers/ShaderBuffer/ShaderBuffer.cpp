// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

size_t ShaderBuffer::AlignmentRequirement(const BufferUsageType usage) noexcept {
    switch ( usage )
    {
        case BufferUsageType::COUNT: DIVIDE_UNEXPECTED_CALL();

        case BufferUsageType::UNBOUND_BUFFER :
        case BufferUsageType::COMMAND_BUFFER : return GFXDevice::GetDeviceInformation()._offsetAlignmentBytesSSBO;
        case BufferUsageType::CONSTANT_BUFFER: return GFXDevice::GetDeviceInformation()._offsetAlignmentBytesUBO;

        default:
        case BufferUsageType::VERTEX_BUFFER:
        case BufferUsageType::INDEX_BUFFER:
        case BufferUsageType::STAGING_BUFFER: break;
    };

    return sizeof(U32);
}

ShaderBuffer::ShaderBuffer(GFXDevice& context, const ShaderBufferDescriptor& descriptor)
      : GUIDWrapper()
      , GraphicsResource(context, Type::SHADER_BUFFER, getGUID(), _ID(descriptor._name.c_str()))
      , RingBuffer(descriptor._ringBufferLength)
      , _alignmentRequirement(AlignmentRequirement(descriptor._usageType))
      , _name(descriptor._name)
      , _params(descriptor)
{
    DIVIDE_ASSERT(descriptor._usageType != BufferUsageType::COUNT);
    DIVIDE_ASSERT(descriptor._elementSize * descriptor._elementCount > 0 && "ShaderBuffer::Create error: Invalid buffer size!");
    _maxSize = descriptor._usageType == BufferUsageType::CONSTANT_BUFFER ? GFXDevice::GetDeviceInformation()._maxSizeBytesUBO : GFXDevice::GetDeviceInformation()._maxSizeBytesSSBO;
}

BufferLock ShaderBuffer::clearData(const ElementRange<> range, const U16 targetQueueIndex) {
    return clearBytes(
               {
                   range._startOffset * _params._elementSize,
                   range._length * _params._elementSize
               },
               targetQueueIndex);
}

BufferLock ShaderBuffer::writeData(const ElementRange<> range, const bufferPtr data) {
    return writeBytes(
               {
                   range._startOffset * _params._elementSize,
                   range._length * _params._elementSize
               },
               data);
}

void ShaderBuffer::readData(const ElementRange<> range, const std::pair<bufferPtr, size_t> outData, const U16 targetQueueIndex) {
    readBytes(
        {
            range._startOffset * _params._elementSize,
            range._length * _params._elementSize
        },
        outData,
        targetQueueIndex);
}

void ShaderBuffer::readBytes(BufferRange<> range, std::pair<bufferPtr, size_t> outData, const U16 targetQueueIndex) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
    const U16 resolvedQueueIndex = targetQueueIndex == U16_MAX ? queueIndex() : targetQueueIndex;

    if ( !DIVIDE_ASSERT( range._length > 0u &&
                         resolvedQueueIndex < queueLength() &&
                         _params._hostVisible &&
                         getUsage() == BufferUsageType::UNBOUND_BUFFER &&
                         range._startOffset < _alignedBufferSize &&
                         range._startOffset == Util::GetAlignmentCorrected(range._startOffset, _alignmentRequirement) &&
                         outData.first != nullptr && outData.second > 0u,
                         "ShaderBuffer::readBytes: invalid read range or output buffer" ) )
    {
        return;
    }

    range._length = std::min( std::min( range._length, outData.second ), _alignedBufferSize - range._startOffset );
    range._startOffset += to_size(resolvedQueueIndex) * _alignedBufferSize;

    readBytesInternal(range, outData);
}

BufferLock ShaderBuffer::clearBytes(const BufferRange<> range, const U16 targetQueueIndex) {
    return writeBytes(range, nullptr, targetQueueIndex);
}

BufferLock ShaderBuffer::writeBytes(BufferRange<> range, const bufferPtr data, const U16 targetQueueIndex) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
    const U16 resolvedQueueIndex = targetQueueIndex == U16_MAX ? queueIndex() : targetQueueIndex;

    if ( !DIVIDE_ASSERT( range._length > 0u &&
                         resolvedQueueIndex < queueLength() &&
                         getUpdateFrequency() != BufferUpdateFrequency::ONCE &&
                         range._startOffset < _alignedBufferSize &&
                         range._length <= _alignedBufferSize - range._startOffset &&
                         range._startOffset == Util::GetAlignmentCorrected(range._startOffset, _alignmentRequirement),
                         "ShaderBuffer::writeBytes: invalid write range or target slot" ) )
    {
        return {};
    }

    range._startOffset += to_size(resolvedQueueIndex) * _alignedBufferSize;

    return writeBytesInternal(range, data);
}

} //namespace Divide
