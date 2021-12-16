#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Box3D.h"

namespace Divide {

template <>
CachedResource_ptr ImplResourceLoader<Box3D>::operator()() {
    const U32 sizeTemp = _descriptor.ID();
    const D64 size = sizeTemp == 0 ? 1.0 : to_D64(sizeTemp);
   
    std::shared_ptr<Box3D> ptr(MemoryManager_NEW Box3D(_context.gfx(),
                                                         _cache,
                                                         _loadingDescriptorHash,
                                                         _descriptor.resourceName(),
                                                         vec3<F32>(to_F32(size))),
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
