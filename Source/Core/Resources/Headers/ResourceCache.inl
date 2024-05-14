

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
#ifndef DVD_RESOURCE_CACHE_INL_
#define DVD_RESOURCE_CACHE_INL_

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/None/Headers/NonePlaceholderObjects.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Shaders/Headers/vkShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"

namespace Divide
{
    struct ResourcePoolBase
    {
        ResourcePoolBase();
        virtual ~ResourcePoolBase();
        virtual void printResources( bool error ) = 0;
    };

    template<typename T>
    struct ResourcePool final : public ResourcePoolBase
    {
        struct Entry
        {
            ResourcePtr<T> _ptr{ nullptr };
            size_t _descriptorHash{ 0u };
            U32    _refCount{ 0u };
        };

        constexpr static size_t ResourcePoolSize = 512u;

        ResourcePool();

        void resize( size_t size);

        [[nodiscard]] ResourcePtr<T> get( Handle<T> handle );

        void deallocate( RenderAPI api, Handle<T>& handle );
        void deallocateInternal( RenderAPI api, Entry& entry );

        [[nodiscard]] Handle<T> allocate( size_t descriptorHash );
        [[nodiscard]] Handle<T> allocateLocked( size_t descriptorHash );

        void commit(Handle<T> handle, ResourcePtr<T> ptr);


        void printResources( bool error ) final;

        RecursiveMutex _lock;
        eastl::fixed_vector<std::pair<bool, U8>, ResourcePoolSize, true> _freeList;
        eastl::fixed_vector<Entry, ResourcePoolSize, true> _resPool;
    };

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    using MemPool = MemoryPool<T, prevPOW2( sizeof( T ) ) * 1u << 5u>;

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    MemPool<T>& GetMemPool()
    {
        static MemPool<T> s_memPool;
        return s_memPool;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    ResourcePool<T>& GetPool()
    {
        static ResourcePool<T> s_pool;
        return s_pool;
    }

    template<typename T>
    void ResourcePool<T>::printResources( const bool error )
    {
        UniqueLock<RecursiveMutex> r_lock( _lock );

        bool first = true;
        const size_t poolSize = _freeList.size();
        for ( size_t i = 0u; i < poolSize; ++i)
        {
            if (!_freeList[i].first)
            {
                DIVIDE_ASSERT(_resPool[i]._ptr != nullptr);

                if ( first )
                {
                    if ( error )
                    {
                        Console::errorfn( LOCALE_STR( "RESOURCE_CACHE_POOL_TYPE" ), _resPool[i]._ptr->typeName() );
                    }
                    else
                    {
                        Console::printfn( LOCALE_STR( "RESOURCE_CACHE_POOL_TYPE" ), _resPool[i]._ptr->typeName() );
                    }
                    first = false;
                }

                if (error)
                {
                    Console::errorfn( LOCALE_STR( "RESOURCE_CACHE_GET_RES_INC" ), _resPool[i]._ptr->resourceName(), _resPool[i]._refCount );
                }
                else
                {
                    Console::printfn( LOCALE_STR("RESOURCE_CACHE_GET_RES_INC"), _resPool[i]._ptr->resourceName(), _resPool[i]._refCount );
                }
            }
        }
    }

    template<typename T>
    ResourcePtr<T> ResourcePool<T>::get( const Handle<T> handle )
    {
        DIVIDE_ASSERT(handle != INVALID_HANDLE<T>);

        UniqueLock<RecursiveMutex> r_lock( _lock );
        DIVIDE_ASSERT( _freeList[handle._index].second == handle._generation );
        
        return _resPool[handle._index]._ptr;
    }

    template<typename T>
    ResourcePool<T>::ResourcePool()
        : ResourcePoolBase()
    {
        
        resize( ResourcePoolSize );
    }

    template<typename T>
    void ResourcePool<T>::resize( const size_t size )
    {
        _freeList.resize( size, std::make_pair( true, 0u ) );
        _resPool.resize( size, {} );
    }

    template<typename T>
    Handle<T> ResourcePool<T>::allocateLocked( const size_t descriptorHash )
    {
        Handle<T> handleOut = {};
        for ( auto& it : _freeList )
        {
            if ( it.first )
            {
                it.first = false;
                handleOut._generation = it.second;
                Entry& entry = _resPool[handleOut._index];
                entry._descriptorHash = descriptorHash;
                entry._refCount = 1u;

                return handleOut;
            }

            ++handleOut._index;
        }

        resize( _freeList.size() + ResourcePoolSize );

        return allocateLocked(descriptorHash);
    }

    template<typename T>
    Handle<T> ResourcePool<T>::allocate( const size_t descriptorHash )
    {
        LockGuard<RecursiveMutex> lock( _lock );
        return allocateLocked( descriptorHash );
    }

    template <typename T>
    void ResourcePool<T>::deallocateInternal( [[maybe_unused]] const RenderAPI api, Entry& entry )
    {
        GetMemPool<T>().deleteElement( entry._ptr );
        entry = {};
    }

    template<>
    inline void ResourcePool<Texture>::deallocateInternal( const RenderAPI api, Entry& entry )
    {
        switch ( api )
        {
            case RenderAPI::None:   GetMemPool<noTexture>().deleteElement( static_cast<ResourcePtr<noTexture>>( entry._ptr ) ); break;
            case RenderAPI::OpenGL: GetMemPool<glTexture>().deleteElement( static_cast<ResourcePtr<glTexture>>( entry._ptr ) ); break;
            case RenderAPI::Vulkan: GetMemPool<vkTexture>().deleteElement( static_cast<ResourcePtr<vkTexture>>( entry._ptr ) ); break;
            case RenderAPI::COUNT:  DIVIDE_UNEXPECTED_CALL(); break;
        }
        entry._ptr = nullptr;
    }

    template<>
    inline void ResourcePool<ShaderProgram>::deallocateInternal( const RenderAPI api, Entry& entry )
    {
        switch ( api )
        {
            case RenderAPI::None:   GetMemPool<noShaderProgram>().deleteElement( static_cast<ResourcePtr<noShaderProgram>>(entry._ptr) ); break;
            case RenderAPI::OpenGL: GetMemPool<glShaderProgram>().deleteElement( static_cast<ResourcePtr<glShaderProgram>>(entry._ptr) ); break;
            case RenderAPI::Vulkan: GetMemPool<vkShaderProgram>().deleteElement( static_cast<ResourcePtr<vkShaderProgram>>(entry._ptr) ); break;
            case RenderAPI::COUNT:  DIVIDE_UNEXPECTED_CALL(); break;
        }
        entry._ptr = nullptr;
    }

    template <typename T>
    void ResourcePool<T>::deallocate( const RenderAPI api, Handle<T>& handle )
    {
        if ( handle == INVALID_HANDLE<T> )
        {
            Console::errorfn( LOCALE_STR( "ERROR_RESOURCE_CACHE_UNKNOWN_RESOURCE" ) );
            return;
        }

        LockGuard<RecursiveMutex> lock( _lock );
        if ( _freeList[handle._index].second != handle._generation )
        {
            // Already free
            return;
        }

        Entry& entry = _resPool[handle._index];

        if ( --entry._refCount == 0u)
        {
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_REM_RES" ), entry._ptr->resourceName().c_str(), entry._descriptorHash );

            entry._descriptorHash = 0u;

            if (entry._ptr->getState() == ResourceState::RES_LOADED)
            {
                entry._ptr->setState(ResourceState::RES_UNLOADING);
                if (!entry._ptr->unload())
                {
                    Console::errorfn( LOCALE_STR( "ERROR_RESOURCE_REM" ), entry._ptr->resourceName().c_str(), entry._ptr->getGUID() );
                    entry._ptr->setState(ResourceState::RES_UNKNOWN);
                }
                else
                {
                    entry._ptr->setState(ResourceState::RES_CREATED);
                }
            }

            DIVIDE_ASSERT(entry._ptr->safeToDelete());
            deallocateInternal( api, entry );

            ++_freeList[handle._index].second;
            _freeList[handle._index].first = true;
        }
        else
        {
            Console::printfn( LOCALE_STR("RESOURCE_CACHE_REM_RES_DEC"), entry._ptr->resourceName().c_str(), entry._refCount );
        }

        handle = INVALID_HANDLE<T>;
    }

    template <typename T>
    void ResourcePool<T>::commit( const Handle<T> handle, ResourcePtr<T> ptr )
    {
        LockGuard<RecursiveMutex> lock( _lock );
        _resPool[handle._index]._ptr = ptr;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    Handle<T> ResourceCache::RetrieveFromCache( const size_t descriptorHash, bool& wasInCache )
    {
        wasInCache = false;

        Handle<T> ret = {};
        ResourcePool<T>& pool = GetPool<T>();
        UniqueLock<RecursiveMutex> r_lock(pool._lock);

        for ( const auto& it : pool._freeList)
        {
            if ( !it.first && pool._resPool[ret._index]._descriptorHash == descriptorHash)
            {
                ret._generation = it.second;
                wasInCache = true;

                const U32 refCount = ++pool._resPool[ret._index]._refCount;

                Console::printfn( LOCALE_STR( "RESOURCE_CACHE_RETRIEVE" ), pool._resPool[ret._index]._ptr->resourceName().c_str(), refCount );
                return ret;
            }
            ++ret._index;
        }

        return pool.allocateLocked(descriptorHash);
    }


    template <typename T> requires std::is_base_of_v<CachedResource, T>
    T* ResourceCache::Get( const Handle<T> handle )
    {
        if ( handle != INVALID_HANDLE<T> )
        {
            ResourcePool<T>& pool = GetPool<T>();
            UniqueLock<RecursiveMutex> r_lock( pool._lock );
            if ( pool._freeList[handle._index].second == handle._generation )
            {
                return pool._resPool[handle._index]._ptr;
            }
        }

        return nullptr;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    void ResourceCache::Destroy( Handle<T>& handle )
    {
        if ( handle != INVALID_HANDLE<T> )
        {
            GetPool<T>().deallocate(s_renderAPI, handle);
        }
        else
        {
            // Not an actual error at this point. It's OK to delete a handle we haven't assigned a resource to yet.
            Console::warnfn(LOCALE_STR( "ERROR_RESOURCE_CACHE_UNKNOWN_RESOURCE" ) );
        }
    }

    template<typename Base, typename Derived> requires std::is_base_of_v<Resource, Base>&& std::is_base_of_v<Base, Derived>
    ResourcePtr<Base> ResourceCache::AllocateInternal( const ResourceDescriptor<Base>& descriptor )
    {
        LockGuard<RecursiveMutex> lock( GetPool<Base>()._lock );
        return GetMemPool<Derived>().newElement( *s_context, descriptor );
    }

    template<>
    inline ResourcePtr<ShaderProgram> ResourceCache::AllocateInternal<ShaderProgram, ShaderProgram>( const ResourceDescriptor<ShaderProgram>& descriptor )
    {
        switch ( s_context->gfx().renderAPI() )
        {
            case RenderAPI::None:   return AllocateInternal<ShaderProgram, noShaderProgram>( descriptor );
            case RenderAPI::OpenGL: return AllocateInternal<ShaderProgram, glShaderProgram>( descriptor );
            case RenderAPI::Vulkan: return AllocateInternal<ShaderProgram, vkShaderProgram>( descriptor );

            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return nullptr;
    }

    template<>
    inline ResourcePtr<Texture> ResourceCache::AllocateInternal<Texture, Texture>( const ResourceDescriptor<Texture>& descriptor )
    {
        switch ( s_context->gfx().renderAPI() )
        {
            case RenderAPI::None:   return AllocateInternal<Texture, noTexture>( descriptor );
            case RenderAPI::OpenGL: return AllocateInternal<Texture, glTexture>( descriptor );
            case RenderAPI::Vulkan: return AllocateInternal<Texture, vkTexture>( descriptor );
            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return nullptr;
    }


    template<typename Base, typename Derived> requires std::is_base_of_v<Resource, Base>&& std::is_base_of_v<Base, Derived>
    void ResourceCache::BuildInternal( ResourcePtr<Base> ptr )
    {
        DIVIDE_ASSERT( ptr != nullptr );
        
        ptr->setState( ResourceState::RES_LOADING );

        if (!ptr->load( *s_context ) )
        {
            ptr->setState( ResourceState::RES_UNKNOWN );
        }
        else
        {
            ptr->setState( ResourceState::RES_THREAD_LOADED );
        }
    }

    template<>
    inline void ResourceCache::BuildInternal<ShaderProgram, ShaderProgram>( ResourcePtr<ShaderProgram> ptr )
    {
        switch ( s_context->gfx().renderAPI() )
        {
            case RenderAPI::None:   BuildInternal<ShaderProgram, noShaderProgram>( ptr ); break;
            case RenderAPI::OpenGL: BuildInternal<ShaderProgram, glShaderProgram>( ptr ); break;
            case RenderAPI::Vulkan: BuildInternal<ShaderProgram, vkShaderProgram>( ptr ); break;

            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    template<>
    inline void  ResourceCache::BuildInternal<Texture, Texture>( ResourcePtr<Texture> ptr )
    {
        switch ( s_context->gfx().renderAPI() )
        {
            case RenderAPI::None:   BuildInternal<Texture, noTexture>( ptr ); break;
            case RenderAPI::OpenGL: BuildInternal<Texture, glTexture>( ptr ); break;
            case RenderAPI::Vulkan: BuildInternal<Texture, vkTexture>( ptr ); break;
            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    void ResourceCache::Build( ResourcePtr<T> ptr, const ResourceDescriptor<T>& descriptor )
    {
        Time::ProfileTimer loadTimer{};

        loadTimer.start();
        BuildInternal<T, T>(ptr);
        loadTimer.stop();

        if ( ptr != nullptr )
        {
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_BUILD" ),
                              ptr->resourceName().c_str(),
                              ptr->typeName(),
                              ptr->getGUID(),
                              descriptor.getHash(),
                              Time::MicrosecondsToMilliseconds<F32>( loadTimer.get() ) );
        }
        else
        {
            Console::errorfn( LOCALE_STR( "RESOURCE_CACHE_BUILD_FAILED" ), descriptor.resourceName() );
        }
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    ResourcePtr<T> ResourceCache::Allocate( Handle<T> handle, const ResourceDescriptor<T>& descriptor, const size_t descriptorHash )
    {
        Time::ProfileTimer loadTimer{};

        loadTimer.start();
        ResourcePtr<T> ptr = AllocateInternal<T, T>(descriptor );

        if ( ptr != nullptr )
        {
            GetPool<T>().commit( handle, ptr );

            loadTimer.stop();
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_ALLOCATE" ),
                              ptr->resourceName().c_str(),
                              ptr->typeName(),
                              ptr->getGUID(),
                              descriptorHash,
                              Time::MicrosecondsToMilliseconds<F32>( loadTimer.get() ) );
        }
        else
        {
            Console::errorfn(LOCALE_STR("RESOURCE_CACHE_ALLOCATE_FAILED"), descriptor.resourceName());
        }

        return ptr;
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    void ResourceCache::Deallocate( Handle<T> handle )
    {
        GetPool<T>().deallocate( s_renderAPI, handle );
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    Handle<T> ResourceCache::LoadResource( const ResourceDescriptor<T>& descriptor, bool& wasInCache, std::atomic_uint& taskCounter )
    {
        taskCounter.fetch_add( 1u );

        // The loading process may change the resource descriptor so always use the user-specified descriptor hash for lookup!
        const size_t loadingHash = descriptor.getHash();

        // If two threads are trying to load the same resource at the same time, by the time one of them adds the resource to the cache, it's too late
        // So check if the hash is currently in the "processing" list, and if it is, just busy-spin until done
        // Once done, lock the hash for ourselves
        ResourceLoadLock res_lock( loadingHash, *s_context );
        /// Check cache first to avoid loading the same resource twice (or if we have stale, expired pointers in there)

        Handle<T> ret = RetrieveFromCache<T>( loadingHash, wasInCache );
        if ( wasInCache )
        {
            taskCounter.fetch_sub( 1u );
            return ret;
        }

        Console::printfn( LOCALE_STR( "RESOURCE_CACHE_GET_RES" ), descriptor.resourceName().c_str(), loadingHash );

        ResourcePtr<T> ptr = ResourceCache::Allocate<T>(ret, descriptor, loadingHash);

        if ( ptr != nullptr )
        {
            Start( *CreateTask( [ptr, descriptor]( const Task& )
                    {
                        ResourceCache::Build<T>( ptr, descriptor );
                    }),
                    s_context->taskPool( TaskPoolType::ASSET_LOADER ), 
                    descriptor.waitForReady() ? TaskPriority::REALTIME : TaskPriority::DONT_CARE,
                    [ptr, ret, &taskCounter, resName = descriptor.resourceName()]()
                    {
                        DIVIDE_ASSERT(ret != INVALID_HANDLE<T>);

                        if ( ptr->getState() == ResourceState::RES_THREAD_LOADED  && ptr->postLoad()) [[likely]]
                        {
                            ptr->setState( ResourceState::RES_LOADED );
                        }
                        else
                        {
                            Console::printfn( LOCALE_STR( "ERROR_RESOURCE_CACHE_LOAD_RES_NAME" ), resName.c_str() );
                            Deallocate( ret );
                        }

                        taskCounter.fetch_sub( 1u );
                    }
                );
        }

        return ret;
    }

} //namespace Divide

#endif //DVD_RESOURCE_CACHE_INL_
