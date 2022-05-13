#include "stdafx.h"

#include "Headers/VertexDataInterface.h"

namespace Divide {

VertexDataInterface::VDIPool VertexDataInterface::s_VDIPool;

VertexDataInterface::VertexDataInterface(GFXDevice& context)
  : GraphicsResource(context, Type::BUFFER, getGUID(), 0u)
{
    _handle = s_VDIPool.registerExisting(*this);
}

VertexDataInterface::~VertexDataInterface()
{
    s_VDIPool.unregisterExisting(_handle);
}

}; //namespace Divide
