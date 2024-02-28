

#include "Headers/GFXRTPool.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Headers/GFXDevice.h"

#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"

namespace Divide {

GFXRTPool::GFXRTPool(GFXDevice& parent)
    : _parent(parent)
{
}

RenderTargetHandle GFXRTPool::allocateRT(const RenderTargetDescriptor& descriptor) {
    LockGuard<SharedMutex> w_lock(_renderTargetLock);

    RenderTargetID uid = 0u;
    for (auto& target : _renderTargets) {
        if (target == nullptr) {
            target = MOV(Attorney::GFXDeviceGFXRTPool::newRT(_parent, descriptor));
            return { target.get(), uid };
        }
        ++uid;
    }

    RenderTarget* rt = _renderTargets.emplace_back(MOV(Attorney::GFXDeviceGFXRTPool::newRT(_parent, descriptor))).get();
    return { rt, _renderTargetIndex++ };
}

bool GFXRTPool::deallocateRT(RenderTargetHandle& handle) {
    if (handle._rt == nullptr) {
        return true;
    }

    const RenderTargetID id = handle._targetID;
    if (id != INVALID_RENDER_TARGET_ID) {
        LockGuard<SharedMutex> w_lock(_renderTargetLock);
        handle._rt = nullptr;
        handle._targetID = INVALID_RENDER_TARGET_ID;
        _renderTargets[id].reset();
        return true;
    }

    return false;
}

RenderTarget* GFXRTPool::getRenderTarget(const RenderTargetID target) const {
    DIVIDE_ASSERT(target != INVALID_RENDER_TARGET_ID && _renderTargets.size() > to_base(target));
    SharedLock<SharedMutex> w_lock(_renderTargetLock);
    return _renderTargets[target].get();
}

}; //namespace Divide