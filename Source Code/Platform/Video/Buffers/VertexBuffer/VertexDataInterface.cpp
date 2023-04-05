#include "stdafx.h"

#include "Headers/VertexDataInterface.h"

namespace Divide {

VertexDataInterface::VDIPool VertexDataInterface::s_VDIPool;

VertexDataInterface::VertexDataInterface(GFXDevice& context, const Str256& name)
  : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID(name.c_str()))
{
    _handle = s_VDIPool.registerExisting(*this);
}

VertexDataInterface::~VertexDataInterface()
{
    s_VDIPool.unregisterExisting(_handle);
}

}; //namespace Divide
