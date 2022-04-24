/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef RESOURCE_MANAGER_H_
#define RESOURCE_MANAGER_H_

#include "ResourceLoader.h"

#include "Utility/Headers/Localization.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

class ResourceLoadLock final : NonCopyable, NonMovable {
public:
    explicit ResourceLoadLock(size_t hash, PlatformContext& context);
    ~ResourceLoadLock();

    static void notifyTaskPool(PlatformContext& context);

private:
    static bool IsLoading(size_t hash);
    static bool SetLoading(size_t hash);
    static bool SetLoadingFinished(size_t hash);

private:
    const size_t _loadingHash;
    const bool _threaded;
};
/// Resource Cache responsibilities:
/// - keep track of already loaded resources
/// - load new resources using appropriate resource loader and store them in cache
/// - remove resources by name / pointer or remove all
class ResourceCache final : public PlatformContextComponent, NonMovable {
public:
    explicit ResourceCache(PlatformContext& context);
    ~ResourceCache();

    /// Each resource entity should have a 'resource name'Loader implementation.
    template <typename T, bool UseAtomicCounter>
    typename std::enable_if<std::is_base_of<CachedResource, T>::value, std::shared_ptr<T>>::type
    loadResource(const ResourceDescriptor& descriptor, bool& wasInCache, std::atomic_uint& taskCounter)
    {
        F32 timeInMS = 0.f;
        std::shared_ptr<T> ptr;
        {
            if_constexpr(UseAtomicCounter) {
                taskCounter.fetch_add(1u);
            }

            // The loading process may change the resource descriptor so always use the user-specified descriptor hash for lookup!
            const size_t loadingHash = descriptor.getHash();

            // If two threads are trying to load the same resource at the same time, by the time one of them adds the resource to the cache, it's too late
            // So check if the hash is currently in the "processing" list, and if it is, just busy-spin until done
            // Once done, lock the hash for ourselves
            ResourceLoadLock res_lock(loadingHash, _context);
            /// Check cache first to avoid loading the same resource twice (or if we have stale, expired pointers in there)
            bool cacheHit = false;
            ptr = std::static_pointer_cast<T>(find(loadingHash, cacheHit));
            /// If the cache did not contain our resource ...
            wasInCache = ptr != nullptr;
            if (!wasInCache) {
                Console::printfn(Locale::Get(_ID("RESOURCE_CACHE_GET_RES")), descriptor.resourceName().c_str(), loadingHash);

                Time::ProfileTimer loadTimer = {};
                loadTimer.start();
                /// ...acquire the resource's loader and get our resource as the loader creates it
                ptr = std::static_pointer_cast<T>(ImplResourceLoader<T>(this, _context, descriptor, loadingHash)());
                assert(ptr != nullptr);
                add(ptr, cacheHit);
                loadTimer.stop();
                timeInMS = Time::MicrosecondsToMilliseconds<F32>(loadTimer.get());
            }

            if_constexpr(UseAtomicCounter) {
                ptr->addStateCallback(ResourceState::RES_LOADED, [&taskCounter](auto) noexcept {
                    taskCounter.fetch_sub(1u);
                });
            }
        }
        if (descriptor.waitForReady()) {
            WAIT_FOR_CONDITION_CALLBACK(ptr->getState() == ResourceState::RES_LOADED, ResourceLoadLock::notifyTaskPool, _context);
        }

        // Print load times
        if (wasInCache) {
            Console::printfn(Locale::Get(_ID("RESOURCE_CACHE_RETRIEVE")), descriptor.resourceName().c_str());
        } else {
            Console::printfn(Locale::Get(_ID("RESOURCE_CACHE_LOAD")), descriptor.resourceName().c_str(), timeInMS);
        }

        return ptr;
    }

    CachedResource_ptr find(size_t descriptorHash, bool& entryInMap);
    void add(const CachedResource_wptr& resource, bool overwriteEntry);
    /// Empty the entire cache of resources
    void clear();

    void printContents() const;

    /// unload a single resource and pend deletion
    void remove(CachedResource* resource);

protected:
    /// multithreaded resource creation
    //using ResourceMap = ska::bytell_hash_map<size_t, CachedResource_wptr>;
    using ResourceMap = hashMap<size_t, CachedResource_wptr>;

    ResourceMap _resDB{}; 
    mutable SharedMutex _creationMutex{};
};

template <typename T>
typename std::enable_if<std::is_base_of<CachedResource, T>::value, std::shared_ptr<T>>::type
CreateResource(ResourceCache* cache, const ResourceDescriptor& descriptor, bool& wasInCache) {
    std::atomic_uint taskCounter = 0u;
    return cache->loadResource<T, false>(descriptor, wasInCache, taskCounter);
}

template <typename T>
typename std::enable_if<std::is_base_of<CachedResource, T>::value, std::shared_ptr<T>>::type
CreateResource(ResourceCache* cache, const ResourceDescriptor& descriptor, bool& wasInCache, std::atomic_uint& taskCounter) {
    return cache->loadResource<T, true>(descriptor, wasInCache, taskCounter);
}

template <typename T>
typename std::enable_if<std::is_base_of<CachedResource, T>::value, std::shared_ptr<T>>::type
CreateResource(ResourceCache* cache, const ResourceDescriptor& descriptor) {
    bool wasInCache = false;
    return CreateResource<T>(cache, descriptor, wasInCache);
}

template <typename T>
typename std::enable_if<std::is_base_of<CachedResource, T>::value, std::shared_ptr<T>>::type
CreateResource(ResourceCache* cache, const ResourceDescriptor& descriptor, std::atomic_uint& taskCounter) {
    bool wasInCache = false;
    return CreateResource<T>(cache, descriptor, wasInCache, taskCounter);
}


}  // namespace Divide

#endif
