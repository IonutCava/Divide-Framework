

#include "Headers/GenericVertexData.h"

namespace Divide {

GenericVertexData::GenericVertexData(GFXDevice& context, const U32 ringBufferLength, const bool renderIndirect, const Str<256>& name)
  : VertexDataInterface(context, name)
  , RingBuffer(ringBufferLength)
  , _renderIndirect(renderIndirect)
  , _name(name)
{
    assert(handle()._id != 0);
}

}; //namespace Divide