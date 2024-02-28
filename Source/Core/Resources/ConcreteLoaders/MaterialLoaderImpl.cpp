

#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Material>::operator()() {
    Material_ptr ptr(MemoryManager_NEW Material(_context.gfx(), _cache, _loadingDescriptorHash, _descriptor.resourceName()), 
                     DeleteResource(_cache));
    assert(ptr != nullptr);

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
