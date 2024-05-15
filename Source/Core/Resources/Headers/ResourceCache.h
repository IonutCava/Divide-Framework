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
#ifndef DVD_RESOURCE_CACHE_H_
#define DVD_RESOURCE_CACHE_H_

#include "Resource.h"
#include "Utility/Headers/Localization.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/PlatformContextComponent.h"

namespace Divide
{
    class ResourceLoadLock final : NonCopyable, NonMovable
    {
        public:
            explicit ResourceLoadLock( size_t hash, PlatformContext& context );
            ~ResourceLoadLock();

        private:
            [[nodiscard]] static bool SetLoading( size_t hash );
            [[nodiscard]] static bool SetLoadingFinished( size_t hash );

        private:
            const size_t _loadingHash;

            static SharedMutex s_hashLock;
            static eastl::set<size_t> s_loadingHashes;
    };

    struct ResourcePoolBase;
    class ResourceCache final : private NonMovable, private NonCopyable
    {
        public:
            static void Init(RenderAPI renderAPI, PlatformContext& context);
            static void Stop();
            static void OnFrameStart();
            static void OnFrameEnd();
            static void PrintLeakedResources();

            template <typename T> requires std::is_base_of_v<CachedResource, T>
            [[nodiscard]] static T* Get( Handle<T> handle);

            template <typename T> requires std::is_base_of_v<CachedResource, T>
            static void Destroy( Handle<T>& handle, const bool immediate );

            template <typename T> requires std::is_base_of_v<CachedResource, T>
            [[nodiscard]] static Handle<T> LoadResource( const ResourceDescriptor<T>& descriptor, bool& wasInCache, std::atomic_uint& taskCounter );


            template <typename T> requires std::is_base_of_v<CachedResource, T>
            [[nodiscard]] static Handle<T> RetrieveFromCache( Handle<T> handle );

        protected:
            friend struct ResourcePoolBase;
            static void RegisterPool( ResourcePoolBase* pool );

        private:
            template <typename T> requires std::is_base_of_v<CachedResource, T>
            [[nodiscard]] static Handle<T> RetrieveOrAllocateHandle( size_t descriptorHash, bool& wasInCache );

            template<typename T> requires std::is_base_of_v<Resource, T>
            [[nodiscard]] static ResourcePtr<T> AllocateAndCommit( Handle<T> handle, const ResourceDescriptor<T>& descriptor );

            template<typename T> requires std::is_base_of_v<Resource, T>
            static ResourcePtr<T> Allocate( Handle<T> handle, const ResourceDescriptor<T>& descriptor, size_t descriptorHash );

            template<typename T> requires std::is_base_of_v<Resource, T>
            [[nodiscard]] static ResourcePtr<T> AllocateInternal( const ResourceDescriptor<T>& descriptor );

            template<typename T> requires std::is_base_of_v<Resource, T>
            static void Build( ResourcePtr<T> ptr, const ResourceDescriptor<T>& descriptor );

            static Mutex s_poolLock;
            static vector<ResourcePoolBase*> s_resourcePools;

            static PlatformContext* s_context;
            static RenderAPI s_renderAPI;
            static bool s_enabled;
    };

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE Handle<T> CreateResource( const ResourceDescriptor<T>& descriptor, bool& wasInCache, std::atomic_uint& taskCounter )
    {
        return ResourceCache::LoadResource<T>( descriptor, wasInCache, taskCounter );
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE Handle<T> CreateResource( const ResourceDescriptor<T>& descriptor, bool& wasInCache )
    {
        std::atomic_uint taskCounter = 0u;
        return CreateResource( descriptor, wasInCache, taskCounter );
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE Handle<T> CreateResource( const ResourceDescriptor<T>& descriptor, std::atomic_uint& taskCounter )
    {
        bool wasInCache = false;
        return CreateResource( descriptor, wasInCache, taskCounter );
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE Handle<T> CreateResource( const ResourceDescriptor<T>& descriptor )
    {
        bool wasInCache = false;
        std::atomic_uint taskCounter = 0u;
        return CreateResource(descriptor, wasInCache, taskCounter);
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE Handle<T> GetResourceRef( const Handle<T> handle )
    {
        return ResourceCache::RetrieveFromCache<T>( handle );
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    FORCE_INLINE void DestroyResource( Handle<T>& handle, const bool immediate = false)
    {
        ResourceCache::Destroy<T>(handle, immediate);
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] FORCE_INLINE T* Get( const Handle<T> handle )
    {
        return ResourceCache::Get( handle );
    }
}  // namespace Divide

#endif //DVD_RESOURCE_CACHE_H_

#include "ResourceCache.inl"
