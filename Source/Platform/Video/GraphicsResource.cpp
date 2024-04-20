

#include "Headers/GraphicsResource.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

GraphicsResource::GraphicsResource(GFXDevice& context, const Type type, const I64 GUID, const U64 nameHash)
    : _context(context), _guid(GUID), _nameHash(nameHash), _type(type)
{
    Attorney::GFXDeviceGraphicsResource::onResourceCreate(_context, _type, _guid, _nameHash);
}

GraphicsResource::~GraphicsResource()
{
    Attorney::GFXDeviceGraphicsResource::onResourceDestroy(_context, _type, _guid, _nameHash);
}

} //namespace Divide
