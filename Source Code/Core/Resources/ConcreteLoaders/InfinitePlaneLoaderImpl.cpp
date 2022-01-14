#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Environment/Terrain/Headers/InfinitePlane.h"

namespace Divide {

    template<>
    CachedResource_ptr ImplResourceLoader<InfinitePlane>::operator()() {
        std::shared_ptr<InfinitePlane> ptr(MemoryManager_NEW InfinitePlane(_context.gfx(), _cache, _loadingDescriptorHash, _descriptor.resourceName(), _descriptor.data().xy), DeleteResource(_cache));

        if (!Load(ptr)) {
            ptr.reset();
        }

        return ptr;
    }

}