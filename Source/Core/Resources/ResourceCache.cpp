

#include "Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"
#include "Core/Headers/PlatformContext.h"

#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

SharedMutex ResourceLoadLock::s_hashLock;
eastl::set<size_t> ResourceLoadLock::s_loadingHashes;

Mutex ResourceCache::s_poolLock;
vector<ResourcePoolBase*> ResourceCache::s_resourcePools;

ResourceLoadLock::ResourceLoadLock(const size_t hash, PlatformContext& context)
    : _loadingHash(hash)
{
    while (!SetLoading(_loadingHash))
    {
        if ( Runtime::isMainThread() )
        {
            PlatformContextIdleCall();
        }
        else
        {
            context.taskPool( TaskPoolType::ASSET_LOADER ).threadWaiting();
        }
    }
}

ResourceLoadLock::~ResourceLoadLock()
{
    DIVIDE_EXPECTED_CALL_MSG( SetLoadingFinished(_loadingHash),  "ResourceLoadLock failed to remove a resource lock!" );
}

bool ResourceLoadLock::SetLoading(const size_t hash)
{
    {
        SharedLock<SharedMutex> r_lock( s_hashLock );
        if (s_loadingHashes.find( hash ) != std::cend( s_loadingHashes ))
        {
            return false;
        }
    }

    LockGuard<SharedMutex> w_lock(s_hashLock);
    return s_loadingHashes.insert(hash).second;
}

bool ResourceLoadLock::SetLoadingFinished(const size_t hash)
{
    LockGuard<SharedMutex> w_lock(s_hashLock);
    return s_loadingHashes.erase(hash) == 1u;
}

PlatformContext* ResourceCache::s_context = nullptr;
RenderAPI ResourceCache::s_renderAPI = RenderAPI::COUNT;
bool ResourceCache::s_enabled = false;
std::atomic_bool ResourceCache::s_deletionQueueDirty{ false };

ResourcePoolBase::ResourcePoolBase(const RenderAPI api)
   : _api(api)
{
    ResourceCache::RegisterPool( this );
}

ResourcePoolBase::~ResourcePoolBase()
{
}

void ResourceCache::RegisterPool( ResourcePoolBase* pool )
{
    LockGuard<Mutex> w_lock( s_poolLock );
    s_resourcePools.push_back( pool );
}

void ResourceCache::Init( RenderAPI renderAPI, PlatformContext& context)
{
    s_context = &context;
    s_renderAPI = renderAPI;
    s_enabled = true;
}

void ResourceCache::Stop()
{
    s_enabled = false;
    Console::printfn(LOCALE_STR("STOP_RESOURCE_CACHE"));

    for ( ResourcePoolBase* pool : s_resourcePools)
    {
        pool->processDeletionQueue();
    }
}

void ResourceCache::PrintLeakedResources()
{
    for ( ResourcePoolBase* pool : s_resourcePools )
    {
        pool->printResources( true );
    }

    Console::Flush();
}

void ResourceCache::OnFrameStart()
{
    bool expected = true;
    if (s_deletionQueueDirty.compare_exchange_strong(expected, false))
    {
        for ( ResourcePoolBase* pool : s_resourcePools )
        {
            pool->processDeletionQueue();
        }
    }
}

void ResourceCache::OnFrameEnd()
{
    NOP();
}

} //namespace Divide
