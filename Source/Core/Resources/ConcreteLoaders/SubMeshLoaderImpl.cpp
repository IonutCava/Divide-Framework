

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Headers/SubMesh.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<SubMesh>::operator()() {
    SubMesh_ptr ptr(MemoryManager_NEW SubMesh(_context.gfx(),
                                              _cache,
                                              _loadingDescriptorHash,
                                              _descriptor.resourceName()),
                    DeleteResource(_cache));
    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
