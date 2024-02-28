

#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Dynamics/Entities/Triggers/Headers/Trigger.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Trigger>::operator()() {
    std::shared_ptr<Trigger> ptr(MemoryManager_NEW Trigger(_cache, _loadingDescriptorHash, _descriptor.resourceName()),
                                   DeleteResource(_cache));

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}