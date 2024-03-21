

#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Headers/StringHelper.h"
#include "Environment/Water/Headers/Water.h"
#include "Geometry/Material/Headers/Material.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<WaterPlane>::operator()() {

    std::shared_ptr<WaterPlane> ptr(MemoryManager_NEW WaterPlane(_cache,
                                                                 _loadingDescriptorHash,
                                                                 _descriptor.resourceName()),
                                    DeleteResource(_cache));

    ptr->setState(ResourceState::RES_LOADING);
    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
