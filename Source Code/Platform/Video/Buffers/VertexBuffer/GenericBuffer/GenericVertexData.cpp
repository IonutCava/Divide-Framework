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

GenericVertexData::~GenericVertexData()
{
    _attributeMapDraw.clear();
}

AttributeDescriptor& GenericVertexData::attribDescriptor(const U32 attribIndex) {
    AttributeDescriptor& desc = _attributeMapDraw[attribIndex];
    desc.attribIndex(attribIndex);
    return desc;
}

void GenericVertexData::setIndexBuffer(const IndexBuffer& indices, [[maybe_unused]] BufferUpdateFrequency updateFrequency) {
    _idxBuffer = indices;
}

void GenericVertexData::updateIndexBuffer(const IndexBuffer& indices) {
    _idxBuffer = indices;
}
}; //namespace Divide