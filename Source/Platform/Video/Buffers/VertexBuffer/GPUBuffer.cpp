

#include "Headers/GPUBuffer.h"

namespace Divide {

GPUBuffer::GPUBufferPool GPUBuffer::s_BufferPool;

GPUBuffer::GPUBuffer(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
    : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID_VIEW(name.data(), name.size()))
    , RingBuffer(ringBufferLength)
    , _name(name.data(), name.size())
    , _handle(s_BufferPool.registerExisting(*this))
{
}

GPUBuffer::~GPUBuffer()
{
    s_BufferPool.unregisterExisting(_handle);
}

BufferLock GPUBuffer::setBuffer(const SetBufferParams& params)
{
    DIVIDE_ASSERT(params._usageType != BufferUsageType::COUNT);

    BufferLock lock{};

    _bindConfig._bindIdx = params._bindIdx;
    _bindConfig._elementStride = params._elementStride;

    return lock;
}


}; //namespace Divide
