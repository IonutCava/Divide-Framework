

#include "Headers/VertexDataInterface.h"

namespace Divide {

VertexDataInterface::VDIPool VertexDataInterface::s_VDIPool;

VertexDataInterface::VertexDataInterface(GFXDevice& context, const std::string_view name)
  : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID_VIEW(name.data(), name.size()))
{
    _handle = s_VDIPool.registerExisting(*this);
}

VertexDataInterface::~VertexDataInterface()
{
    s_VDIPool.unregisterExisting(_handle);
}

}; //namespace Divide
