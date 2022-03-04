#include "stdafx.h"

#include "Headers/GenericVertexData.h"

namespace Divide {

GenericVertexData::GenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
  : VertexDataInterface(context),
    RingBuffer(ringBufferLength),
    _name(name == nullptr ? "" : name)
{
    assert(handle()._id != 0);
}


void GenericVertexData::setIndexBuffer(const IndexBuffer& indices) {
    _idxBuffer = indices;
}

void GenericVertexData::updateIndexBuffer(const IndexBuffer& indices) {
    _idxBuffer = indices;
}
}; //namespace Divide