

#include "Headers/GenericVertexData.h"

namespace Divide {

GenericVertexData::GenericVertexData(GFXDevice& context, const U16 ringBufferLength, const std::string_view name)
  : VertexDataInterface(context, name)
  , RingBuffer(ringBufferLength)
  , _name(name)
{
    assert(handle()._id != 0);
}

} //namespace Divide
