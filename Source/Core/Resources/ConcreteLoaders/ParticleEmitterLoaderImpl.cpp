

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<ParticleEmitter>::operator()() {
    std::shared_ptr<ParticleEmitter> ptr(MemoryManager_NEW ParticleEmitter(_context.gfx(), _cache, _loadingDescriptorHash, _descriptor.resourceName()),
                                           DeleteResource(_cache));

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
