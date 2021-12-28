#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Box3D.h"

namespace Divide {

template <>
CachedResource_ptr ImplResourceLoader<Box3D>::operator()() {
    const vec3<U16> sizeTemp = _descriptor.data();
    vec3<F32> targetSize{ VECTOR3_UNIT };
    for (U8 i = 0u; i < 3u; ++i) {
        if (sizeTemp[i] == 0u) {
            targetSize[i] = 1.f;
        } else {
            targetSize[i] = Util::UNPACK_HALF1x16(sizeTemp[i]) * _descriptor.ID();;
        }
    }

   
    std::shared_ptr<Box3D> ptr(MemoryManager_NEW Box3D(_context.gfx(),
                                                         _cache,
                                                         _loadingDescriptorHash,
                                                         _descriptor.resourceName(),
                                                         targetSize),
                                  DeleteResource(_cache));

    if (!_descriptor.flag()) {
        const ResourceDescriptor matDesc("Material_" + _descriptor.resourceName());
        Material_ptr matTemp = CreateResource<Material>(_cache, matDesc);
        matTemp->shadingMode(ShadingMode::COOK_TORRANCE);
        ptr->setMaterialTpl(matTemp);
    }

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
