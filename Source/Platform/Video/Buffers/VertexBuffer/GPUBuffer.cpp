

#include "Headers/GPUBuffer.h"

namespace Divide {

GPUVertexBuffer::GVBPool GPUVertexBuffer::s_GVBPool;

GPUBuffer::GPUBuffer(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
    : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID_VIEW(name.data(), name.size()))
    , RingBuffer(ringBufferLength)
    , _name(name.data(), name.size())
{
}

GPUVertexBuffer::GPUVertexBuffer(GFXDevice& context, const std::string_view name)
  : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID_VIEW(name.data(), name.size()))
  , _handle(s_GVBPool.registerExisting(*this))
{
}

GPUVertexBuffer::~GPUVertexBuffer()
{
    s_GVBPool.unregisterExisting(_handle);
}


void GPUVertexBuffer::incQueue()
{
    if (_vertexBuffer) _vertexBuffer->incQueue();
    if (_indexBuffer)  _indexBuffer->incQueue();
}


}; //namespace Divide
