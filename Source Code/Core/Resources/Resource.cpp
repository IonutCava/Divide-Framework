#include "stdafx.h"

#include "Headers/Resource.h"
#include "Headers/ResourceCache.h"

namespace Divide {

//---------------------------- Resource ------------------------------------------//
Resource::Resource(const ResourceType type, const Str256& resourceName)
    : GUIDWrapper(),
      _resourceName(resourceName),
      _resourceType(type),
      _resourceState(ResourceState::RES_CREATED)
{
}

ResourceState Resource::getState() const noexcept {
    return _resourceState.load(std::memory_order_relaxed);
}

void Resource::setState(const ResourceState currentState) noexcept {
    _resourceState.store(currentState, std::memory_order_relaxed);
}

//---------------------------- Cached Resource ------------------------------------//
CachedResource::CachedResource(const ResourceType type,
                               const size_t descriptorHash,
                               const Str256& resourceName)
    : CachedResource(type, descriptorHash, resourceName, {}, {})
{
}

CachedResource::CachedResource(const ResourceType type,
                               const size_t descriptorHash,
                               const Str256& resourceName,
                               const ResourcePath& assetName)
    : CachedResource(type, descriptorHash, resourceName, assetName, {})
{
}

CachedResource::CachedResource(const ResourceType type,
                               const size_t descriptorHash,
                               const Str256& resourceName,
                               ResourcePath assetName,
                               ResourcePath assetLocation)
    : Resource(type, resourceName),
      _assetLocation(MOV(assetLocation)),
      _assetName(MOV(assetName)),
      _descriptorHash(descriptorHash)
{
}

bool CachedResource::load() {
    setState(ResourceState::RES_LOADED);
    flushStateCallbacks();
    return true;
}

bool CachedResource::unload() {
    flushStateCallbacks();

    return true;
}

void CachedResource::addStateCallback(const ResourceState targetState, const DELEGATE<void, CachedResource*>& cbk) {
    {
        ScopedLock<Mutex> w_lock(_callbackLock);
        _loadingCallbacks[to_U32(targetState)].push_back(cbk);
    }
    if (Resource::getState() == ResourceState::RES_LOADED) {
        flushStateCallbacks();
    }
}

void CachedResource::setState(const ResourceState currentState) noexcept {
    Resource::setState(currentState);
    flushStateCallbacks();
}

void CachedResource::flushStateCallbacks() {
    const ResourceState currentState = Resource::getState();
    for (U8 i = 0; i < to_base(currentState) + 1; ++i) {
        const auto tempState = static_cast<ResourceState>(i);
        CachedResource* ptr = nullptr;
        if (tempState != ResourceState::RES_UNKNOWN && tempState != ResourceState::RES_UNLOADING) {
            ptr = this;
        }
        ScopedLock<Mutex> r_lock(_callbackLock);
        CallbackList& cbks = _loadingCallbacks[to_U32(tempState)];
        for (auto& cbk : cbks) {
            cbk(ptr);
        }
        cbks.clear();
    }
}

}  // namespace Divide