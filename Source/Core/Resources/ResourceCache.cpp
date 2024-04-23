

#include "Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"
#include "Core/Headers/PlatformContext.h"

#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

namespace
{
    SharedMutex g_hashLock;
    eastl::set<size_t> g_loadingHashes;
}

ResourceLoadLock::ResourceLoadLock(const size_t hash, PlatformContext& context)
    : _loadingHash(hash),
      _threaded(!Runtime::isMainThread())
{
    while (!SetLoading(_loadingHash))
    {
        if (_threaded)
        {
            notifyTaskPool(context);
        }
    }
}

ResourceLoadLock::~ResourceLoadLock()
{
    const bool ret = SetLoadingFinished(_loadingHash);
    DIVIDE_ASSERT(ret, "ResourceLoadLock failed to remove a resource lock!");
}

bool ResourceLoadLock::IsLoading(const size_t hash)
{
    SharedLock<SharedMutex> r_lock(g_hashLock);
    return g_loadingHashes.find(hash) != std::cend(g_loadingHashes);
}

bool ResourceLoadLock::SetLoading(const size_t hash)
{
    if (!IsLoading(hash))
    {
        LockGuard<SharedMutex> w_lock(g_hashLock);
        //Check again
        if (g_loadingHashes.find(hash) == std::cend(g_loadingHashes))
        {
            g_loadingHashes.insert(hash);
            return true;
        }
    }
    return false;
}

bool ResourceLoadLock::SetLoadingFinished(const size_t hash)
{
    LockGuard<SharedMutex> w_lock(g_hashLock);
    const size_t prevSize = g_loadingHashes.size();
    g_loadingHashes.erase(hash);
    return prevSize > g_loadingHashes.size();
}

void ResourceLoadLock::notifyTaskPool(PlatformContext& context)
{
    context.taskPool(TaskPoolType::ASSET_LOADER ).threadWaiting();
}

void DeleteResource::operator()(CachedResource* res) const
{
    WAIT_FOR_CONDITION(res->getState() == ResourceState::RES_LOADED || res->getState() == ResourceState::RES_UNKNOWN, false);
    if (res->getState() != ResourceState::RES_UNKNOWN) {
        _context->remove(res);
    }

    MemoryManager::SAFE_DELETE(res);
}

ResourceCache::ResourceCache(PlatformContext& context)
    : PlatformContextComponent(context)
{
}

ResourceCache::~ResourceCache()
{
    Console::printfn(LOCALE_STR("RESOURCE_CACHE_DELETE"));
    clear();
}

void ResourceCache::printContents() const {
    SharedLock<SharedMutex> r_lock(_creationMutex);
    for (ResourceMap::const_iterator it = std::cbegin(_resDB); it != std::cend(_resDB); ++it) {
        assert(!it->second.expired());

        const CachedResource_ptr res = it->second.lock();
        Console::printfn(LOCALE_STR("RESOURCE_INFO"), res->resourceName().c_str(), res->getGUID());

    }
}

void ResourceCache::clear()
{
    Console::printfn(LOCALE_STR("STOP_RESOURCE_CACHE"));

    LockGuard<SharedMutex> w_lock(_creationMutex);
    for (ResourceMap::iterator it = std::begin(_resDB); it != std::end(_resDB); ++it)
    {
        assert(!it->second.expired());

        const CachedResource_ptr res = it->second.lock();
        Console::warnfn(LOCALE_STR("WARN_RESOURCE_LEAKED"), res->resourceName().c_str(), res->getGUID());
    }

    _resDB.clear();
}

void ResourceCache::add(const CachedResource_wptr& resource, const bool overwriteEntry)
{
    DIVIDE_ASSERT(!resource.expired(), LOCALE_STR("ERROR_RESOURCE_CACHE_LOAD_RES"));

    const CachedResource_ptr res = resource.lock();
    const size_t hash = res->descriptorHash();
    DIVIDE_ASSERT(hash != 0, "ResourceCache add error: Invalid resource hash!");

    Console::printfn(LOCALE_STR("RESOURCE_CACHE_ADD"), res->resourceName().c_str(), res->getResourceTypeName(), res->getGUID(), hash);

    LockGuard<SharedMutex> w_lock(_creationMutex);
    const auto ret = _resDB.emplace(hash, res);
    if (!ret.second && overwriteEntry)
    {
         _resDB[hash] = res;
    }
    DIVIDE_ASSERT(ret.second || overwriteEntry, LOCALE_STR("ERROR_RESOURCE_CACHE_LOAD_RES"));
}

CachedResource_ptr ResourceCache::find(const size_t descriptorHash, bool& entryInMap)
{
    /// Search in our resource cache
    SharedLock<SharedMutex> r_lock(_creationMutex);
    const ResourceMap::const_iterator it = _resDB.find(descriptorHash);
    if (it != std::end(_resDB))
    {
        entryInMap = true;
        return it->second.lock();
    }

    entryInMap = false;
    return nullptr;
}

void ResourceCache::remove(CachedResource* resource)
{
    resource->setState(ResourceState::RES_UNLOADING);

    const size_t resourceHash = resource->descriptorHash();
    const Str<256> name = resource->resourceName();
    const I64 guid = resource->getGUID();

    DIVIDE_ASSERT(resourceHash != 0, LOCALE_STR("ERROR_RESOURCE_CACHE_INVALID_NAME"));

    bool resDBEmpty;
    {
        SharedLock<SharedMutex> r_lock(_creationMutex);
        resDBEmpty = _resDB.empty();
        const auto& it = _resDB.find(resourceHash);
        DIVIDE_ASSERT(!resDBEmpty &&  it != std::end(_resDB), LOCALE_STR("ERROR_RESOURCE_CACHE_UNKNOWN_RESOURCE"));
    }


    Console::printfn(LOCALE_STR("RESOURCE_CACHE_REM_RES"), name, resourceHash);
    if (!resource->unload())
    {
        Console::errorfn(LOCALE_STR("ERROR_RESOURCE_REM"), name, guid);
    }

    if (resDBEmpty)
    {
        Console::errorfn(LOCALE_STR("RESOURCE_CACHE_REMOVE_NO_DB"), name);
    }
    else
    {
        LockGuard<SharedMutex> w_lock(_creationMutex);
        _resDB.erase(_resDB.find(resourceHash));
    }

    resource->setState(ResourceState::RES_UNKNOWN);
}

Mesh_ptr detail::MeshLoadData::build()
{
    if ( _mesh != nullptr )
    {
        Start(*CreateTask([&]( const Task& )
                          {
                              PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );
                          
                              Import::ImportData tempMeshData( _descriptor->assetLocation(), _descriptor->assetName() );
                          
                              if ( MeshImporter::loadMeshDataFromFile( *_context, tempMeshData ) &&
                                   MeshImporter::loadMesh( tempMeshData.loadedFromFile(), _mesh.get(), *_context, _cache, tempMeshData ) &&
                                   _mesh->load() )
                              {
                                  NOP();
                              }
                              else
                              {
                                  Console::errorfn( LOCALE_STR( "ERROR_IMPORTER_MESH" ), _descriptor->assetLocation() / _descriptor->assetName() );
                          
                                  _cache->remove( _mesh.get() );
                                  _mesh.reset();
                              }
                          }),
               _context->taskPool( TaskPoolType::ASSET_LOADER ) );
    }

    return _mesh;
}

} //namespace Divide
