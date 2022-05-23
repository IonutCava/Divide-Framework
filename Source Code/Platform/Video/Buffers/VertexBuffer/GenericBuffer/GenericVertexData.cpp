#include "stdafx.h"

#include "Headers/GenericVertexData.h"

namespace Divide {

GenericVertexData::GenericVertexData(GFXDevice& context, const U32 ringBufferLength, const char* name)
  : VertexDataInterface(context, name),
    RingBuffer(ringBufferLength),
    _name(name == nullptr ? "" : name)
{
    assert(handle()._id != 0);
}

}; //namespace Divide