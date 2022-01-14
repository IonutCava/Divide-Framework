#include "stdafx.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Box3D.h"

namespace Divide {

template <>
CachedResource_ptr ImplResourceLoader<Box3D>::operator()() {
    constexpr F32 s_minSideLength = 0.0001f;

    const vec3<F32> targetSize{
        std::max(Util::UINT_TO_FLOAT(_descriptor.data().x), s_minSideLength),
        std::max(Util::UINT_TO_FLOAT(_descriptor.data().y), s_minSideLength),
        std::max(Util::UINT_TO_FLOAT(_descriptor.data().z), s_minSideLength)
    };

    std::shared_ptr<Box3D> ptr(MemoryManager_NEW Box3D(_context.gfx(),
                                                         _cache,
                                                         _loadingDescriptorHash,
                                                         _descriptor.resourceName(),
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
