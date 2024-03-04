

#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Headers/PlatformContext.h"

#include "Utility/Headers/Localization.h"
#include "Environment/Terrain/Headers/Terrain.h"
#include "Environment/Terrain/Headers/TerrainLoader.h"
#include "Environment/Terrain/Headers/TerrainDescriptor.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Terrain>::operator()() {
    std::shared_ptr<Terrain> ptr(MemoryManager_NEW Terrain(_context.gfx(), _cache, _loadingDescriptorHash, _descriptor.resourceName()),
                                 DeleteResource(_cache));

    Console::printfn(LOCALE_STR("TERRAIN_LOAD_START"), _descriptor.resourceName().c_str());
    const std::shared_ptr<TerrainDescriptor>& terrain = _descriptor.propertyDescriptor<TerrainDescriptor>();
    
    if (ptr) {
        ptr->setState(ResourceState::RES_LOADING);
    }

    if (!ptr || !TerrainLoader::loadTerrain(ptr, terrain, _context, true)) {
        Console::errorfn(LOCALE_STR("ERROR_TERRAIN_LOAD"), _descriptor.resourceName().c_str());
        ptr.reset();
    }

    return ptr;
}

}