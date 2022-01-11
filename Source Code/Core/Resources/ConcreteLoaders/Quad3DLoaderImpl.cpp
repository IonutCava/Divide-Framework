#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Quad3D>::operator()() {
    const vec3<U16> sizeTemp = _descriptor.data();
    vec3<F32> targetSize{ VECTOR3_ZERO};
    
    for (U8 i = 0u; i < 3u; ++i) {
        targetSize[i] = Util::UNPACK_HALF1x16(sizeTemp[i]);
    }
    targetSize *= _descriptor.ID();

    if (targetSize.x == 0.f) {
        targetSize.x = 2.f;
    }
    if (targetSize.y == 0.f) {
        targetSize.y = 2.f;
    }
    //targetSize.z can be zero. That's by desing

    std::shared_ptr<Quad3D> ptr(MemoryManager_NEW Quad3D(_context.gfx(),
                                                          _cache,
                                                          _loadingDescriptorHash,
                                                          _descriptor.resourceName(),
                                                          _descriptor.mask().b[0] == 0,
                                                          targetSize),
                                 DeleteResource(_cache));
    if (!_descriptor.flag()) {
        const ResourceDescriptor matDesc("Material_" + _descriptor.resourceName());
        Material_ptr matTemp = CreateResource<Material>(_cache, matDesc);
        matTemp->properties().shadingMode(ShadingMode::PBR_MR);
        ptr->setMaterialTpl(matTemp);
    }

    if (!Load(ptr)) {
        ptr.reset();
    }

    return ptr;
}

}
