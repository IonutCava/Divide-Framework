

#include "Headers/VertexDataInterface.h"

namespace Divide {

VertexDataInterface::VDIPool VertexDataInterface::s_VDIPool;
SharedMutex VertexDataInterface::s_VDIPoolLock;

VertexDataInterface::VertexDataInterface(GFXDevice& context, const Str<256>& name)
  : GraphicsResource(context, Type::BUFFER, getGUID(), name.empty() ? 0 : _ID(name.c_str()))
{
    LockGuard<SharedMutex> w_lock( s_VDIPoolLock );
    _handle = s_VDIPool.registerExisting(*this);
}

VertexDataInterface::~VertexDataInterface()
{
    LockGuard<SharedMutex> w_lock(s_VDIPoolLock);
    s_VDIPool.unregisterExisting(_handle);
}

}; //namespace Divide
