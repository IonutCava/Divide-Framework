#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Quad3D>::operator()() {
    std::shared_ptr<Quad3D> ptr(MemoryManager_NEW Quad3D(_context.gfx(),
                                                          _cache,
                                                          _loadingDescriptorHash,
                                                          _descriptor.resourceName(),
                                                          _descriptor.mask().b[0] == 0),
                                 DeleteResource(_cache));
    if (!_descriptor.flag()) {
        const ResourceDescriptor matDesc("Material_" + _descriptor.resourceName());
        Material_ptr matTemp = CreateResource<Material>(_cache, matDesc);
        matTemp->shadingMode(ShadingMode::BLINN_PHONG);
        ptr->setMaterialTpl(matTemp);
    }

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
