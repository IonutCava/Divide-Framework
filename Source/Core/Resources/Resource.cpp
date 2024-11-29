

#include "Headers/Resource.h"
#include "Headers/ResourceCache.h"

namespace Divide {

//---------------------------- Resource ------------------------------------------//
Resource::Resource( const std::string_view resourceName, const std::string_view typeName )
    : GUIDWrapper()
    , _typeName( typeName )
    , _resourceName(resourceName)
    , _resourceState(ResourceState::RES_CREATED)
{
}

ResourceState Resource::getState() const noexcept
{
    return _resourceState.load(std::memory_order_relaxed);
}

void Resource::setState(const ResourceState currentState)
{
    _resourceState.store(currentState, std::memory_order_relaxed);
}

bool WaitForReady( Resource* res )
{
    if ( res != nullptr )
    {
        ResourceState state = res->getState();

        while( state != ResourceState::RES_LOADED && state != ResourceState::RES_UNKNOWN )
        {
            DIVIDE_ASSERT(state != ResourceState::RES_UNLOADING);

            if ( state == ResourceState::RES_THREAD_LOAD_FAILED ||
                 state == ResourceState::RES_LOAD_FAILED )
            {
                // We failed the load!
                return false;
            }

            PlatformContextIdleCall();
            std::this_thread::yield();
            state = res->getState();
        }

        return true;
    }

    return false;
}

bool SafeToDelete( Resource* res )
{
    if ( res != nullptr )
    {
        const ResourceState state = res->getState();
        return state == ResourceState::RES_CREATED || state == ResourceState::RES_LOADED;
    }

    return true;
}

//---------------------------- Cached Resource ------------------------------------//
CachedResource::CachedResource( const ResourceDescriptorBase& descriptor, const std::string_view typeName )
    : Resource( descriptor.resourceName(), typeName)
    , _assetLocation( descriptor.assetLocation() )
    , _assetName( descriptor.assetName() )
    , _descriptorHash( descriptor.getHash() )
{
}

bool CachedResource::load( [[maybe_unused]] PlatformContext& context )
{
    return true;
}

bool CachedResource::postLoad()
{
    return true;
}

bool CachedResource::unload()
{
    return true;
}

void CachedResource::setState(const ResourceState currentState)
{
    Resource::setState(currentState);
}

ResourceDescriptorBase::ResourceDescriptorBase( const std::string_view resourceName )
    : Hashable()
    , _resourceName( resourceName.data(), resourceName.size() )
{
}

size_t ResourceDescriptorBase::getHash() const
{
    _hash = Hashable::getHash();

    Util::Hash_combine( _hash,
                        _resourceName,
                        _assetLocation,
                        _assetName,
                        _flag,
                        _ID,
                        _mask.i,
                        _enumValue,
                        _data.x,
                        _data.y,
                        _data.z );

    return _hash;
}

}  // namespace Divide
