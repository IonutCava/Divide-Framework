#include "stdafx.h"

#include "Headers/VertexDataInterface.h"

namespace Divide {

VertexDataInterface::VDIPool VertexDataInterface::s_VDIPool;

VertexDataInterface::VertexDataInterface(GFXDevice& context, const char* name)
  : GraphicsResource(context, Type::BUFFER, getGUID(), name != nullptr ? _ID(name) : 0u)
{
    _handle = s_VDIPool.registerExisting(*this);
}

VertexDataInterface::~VertexDataInterface()
{
    s_VDIPool.unregisterExisting(_handle);
}

}; //namespace Divide
