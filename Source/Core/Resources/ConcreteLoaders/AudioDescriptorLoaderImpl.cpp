

#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Audio/Headers/AudioDescriptor.h"

namespace Divide {

template <>
CachedResource_ptr ImplResourceLoader<AudioDescriptor>::operator()() {
    AudioDescriptor_ptr ptr(MemoryManager_NEW AudioDescriptor(_loadingDescriptorHash,
                                                              _descriptor.resourceName(),
                                                              _descriptor.assetName(),
                                                              _descriptor.assetLocation()),
                            DeleteResource(_cache));
    if (!Load(ptr)) {
        ptr.reset();
    } else {
        ptr->isLooping(_descriptor.flag());
    }

    return ptr;
}

}
