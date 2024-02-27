

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"

namespace Divide {

template<>
CachedResource_ptr ImplResourceLoader<Quad3D>::operator()() {
    constexpr F32 s_minSideLength = 0.0001f;

    const vec3<U32> sizeIn = _descriptor.data();

    vec3<F32> targetSize{
        Util::UINT_TO_FLOAT(sizeIn.x),
        Util::UINT_TO_FLOAT(sizeIn.y),
        Util::UINT_TO_FLOAT(sizeIn.z)
    };
    if (sizeIn.x == 0u && sizeIn.y == 0u && sizeIn.z == 0u) {
        targetSize.xy = { s_minSideLength, s_minSideLength };
    } else if (sizeIn.x == 0u && sizeIn.y == 0u ||
               sizeIn.x == 0u && sizeIn.z == 0u) {
        targetSize.x = s_minSideLength;
    } else if (sizeIn.y == 0u && sizeIn.z == 0u) {
        targetSize.y = s_minSideLength;
    }

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
