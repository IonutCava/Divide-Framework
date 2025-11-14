

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
#include "Platform/Video/RenderBackend/NRI/Headers/NRIPlaceholderObjects.h"
#include "Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h"
#include "Platform/Video/RenderBackend/Vulkan/Shaders/Headers/vkShaderProgram.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"
#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"

namespace Divide
{
    struct ResourcePoolBase
    {
        explicit ResourcePoolBase( RenderAPI api );

        virtual ~ResourcePoolBase();
        virtual void printResources( bool error ) = 0;
        virtual void processDeletionQueue() = 0;

       protected:
        const RenderAPI _api;
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

        explicit ResourcePool( RenderAPI api );

        void resize( size_t size);

        void queueDeletion(Handle<T>& handle);
        void processDeletionQueue() override;

        [[nodiscard]] ResourcePtr<T> get( Handle<T> handle );
        
        Handle<T> retrieveHandleLocked( const size_t descriptorHash );

        void deallocate( Handle<T>& handle );

        [[nodiscard]] Handle<T> allocate( size_t descriptorHash );

        void commitLocked(Handle<T> handle, ResourcePtr<T> ptr);

        void printResources( bool error ) final;

        SharedMutex _lock;
        fixed_vector<std::pair<bool, U8>, ResourcePoolSize, true> _freeList;
        fixed_vector<Entry, ResourcePoolSize, true> _resPool;

        void deallocateInternal( ResourcePtr<T> ptr );
        [[nodiscard]] Handle<T> allocateLocked( size_t descriptorHash );

    private:
        moodycamel::ConcurrentQueue<Handle<T>> _deletionQueue;
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
    ResourcePool<T>& GetPool( const RenderAPI api)
    {
        static ResourcePool<T> s_pool(api);

        return s_pool;
    }

    template<typename T>
    void ResourcePool<T>::queueDeletion( Handle<T>& handle )
    {
        if ( handle != INVALID_HANDLE<T> )
        {
            _deletionQueue.enqueue(handle);
            ResourceCache::s_deletionQueueDirty.store(true, std::memory_order_release);
            handle = INVALID_HANDLE<T>;
        }
    }

    template<typename T>
    void ResourcePool<T>::processDeletionQueue()
    {
        Handle<T> handle{};
        while (_deletionQueue.try_dequeue( handle ))
        {
            deallocate( handle );
        }
    }

    template<typename T>
    void ResourcePool<T>::printResources( const bool error )
    {
        SharedLock<SharedMutex> r_lock( _lock );

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

        SharedLock<SharedMutex> r_lock( _lock );
        DIVIDE_ASSERT( _freeList[handle._index].second == handle._generation );
        
        return _resPool[handle._index]._ptr;
    }

    template<typename T>
    ResourcePool<T>::ResourcePool(const RenderAPI api)
        : ResourcePoolBase(api)
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
        LockGuard<SharedMutex> lock( _lock );
        return allocateLocked( descriptorHash );
    }

    template <typename T>
    void ResourcePool<T>::deallocateInternal( ResourcePtr<T> ptr )
    {
        GetMemPool<T>().deleteElement( ptr );
    }

    template<>
    inline void ResourcePool<Texture>::deallocateInternal( ResourcePtr<Texture> ptr )
    {
        switch ( _api )
        {
            case RenderAPI::None:   GetMemPool<noTexture>().deleteElement( static_cast<ResourcePtr<noTexture>>( ptr ) ); break;
            case RenderAPI::OpenGL: GetMemPool<glTexture>().deleteElement( static_cast<ResourcePtr<glTexture>>( ptr ) ); break;
            case RenderAPI::Vulkan: GetMemPool<vkTexture>().deleteElement( static_cast<ResourcePtr<vkTexture>>( ptr ) ); break;
            case RenderAPI::NRI_Vulkan:
            case RenderAPI::NRI_D3D12:
            case RenderAPI::NRI_D3D11:
            case RenderAPI::NRI_None:
                GetMemPool<nriTexture>().deleteElement( static_cast<ResourcePtr<nriTexture>>( ptr ) ); break;

            default:
            case RenderAPI::COUNT:  DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    template<>
    inline void ResourcePool<ShaderProgram>::deallocateInternal( ResourcePtr<ShaderProgram> ptr )
    {
        switch ( _api )
        {
            case RenderAPI::None:   GetMemPool<noShaderProgram>().deleteElement( static_cast<ResourcePtr<noShaderProgram>>( ptr) ); break;
            case RenderAPI::OpenGL: GetMemPool<glShaderProgram>().deleteElement( static_cast<ResourcePtr<glShaderProgram>>( ptr) ); break;
            case RenderAPI::Vulkan: GetMemPool<vkShaderProgram>().deleteElement( static_cast<ResourcePtr<vkShaderProgram>>( ptr) ); break;
            case RenderAPI::NRI_Vulkan:
            case RenderAPI::NRI_D3D12:
            case RenderAPI::NRI_D3D11:
            case RenderAPI::NRI_None:
                GetMemPool<nriShaderProgram>().deleteElement(static_cast<ResourcePtr<nriShaderProgram>>(ptr)); break;

            default:
            case RenderAPI::COUNT:  DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    template <typename T>
    void ResourcePool<T>::deallocate( Handle<T>& handle )
    {
        if ( handle == INVALID_HANDLE<T> )
        {
            Console::errorfn( LOCALE_STR( "ERROR_RESOURCE_CACHE_UNKNOWN_RESOURCE" ) );
            return;
        }

        ResourcePtr<T> ptr = nullptr;
        size_t descriptorHash = 0u;

        {
            LockGuard<SharedMutex> w_lock( _lock );
            if ( _freeList[handle._index].second != handle._generation )
            {
                // Already free
                return;
            }

            Entry& entry = _resPool[handle._index];
            if ( --entry._refCount == 0u)
            {
                ptr = entry._ptr;
                descriptorHash = entry._descriptorHash;

                entry = {};
                ++_freeList[handle._index].second;
                _freeList[handle._index].first = true;
            }
            else
            {
                Console::printfn( LOCALE_STR( "RESOURCE_CACHE_REM_RES_DEC" ), entry._ptr->resourceName().c_str(), entry._refCount );
            }
        }
        handle = INVALID_HANDLE<T>;

        if ( ptr != nullptr )
        {
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_REM_RES" ), ptr->resourceName().c_str(), descriptorHash );

            if ( ptr->getState() == ResourceState::RES_LOADED)
            {
                ptr->setState(ResourceState::RES_UNLOADING);
                if (ptr->unload())
                {
                    ptr->setState(ResourceState::RES_CREATED);
                }
                else
                {
                    ptr->setState(ResourceState::RES_UNKNOWN);
                    Console::errorfn( LOCALE_STR( "ERROR_RESOURCE_REM" ), ptr->resourceName().c_str(), ptr->getGUID() );
                }
            }

            deallocateInternal( ptr );
        }
    }

    template<typename T>
    Handle<T> ResourcePool<T>::retrieveHandleLocked( const size_t descriptorHash )
    {
        Handle<T> ret{};
        for ( const auto&[free, generation] : _freeList )
        {
            if ( !free )
            {
                Entry& entry = _resPool[ret._index];

                if ( entry._descriptorHash == descriptorHash )
                {
                    ret._generation = generation;
                    ++entry._refCount;

                    Console::printfn( LOCALE_STR( "RESOURCE_CACHE_GET_RES_INC" ), entry._ptr->resourceName(), entry._refCount );
                    return ret;
                }
            }
            ++ret._index;
        }

        return INVALID_HANDLE<T>;
    }

    template <typename T>
    void ResourcePool<T>::commitLocked( const Handle<T> handle, ResourcePtr<T> ptr )
    {
        _resPool[handle._index]._ptr = ptr;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    [[nodiscard]] Handle<T> ResourceCache::RetrieveFromCache( const Handle<T> handle )
    {
        if ( handle != INVALID_HANDLE<T>)
        {
            ResourcePool<T>& pool = GetPool<T>( s_renderAPI );
            SharedLock<SharedMutex> r_lock( pool._lock );
            if ( pool._freeList[handle._index].second == handle._generation )
            {
                auto& entry = pool._resPool[handle._index];
                ++entry._refCount;
                Console::printfn( LOCALE_STR( "RESOURCE_CACHE_GET_RES_INC" ), entry._ptr->resourceName(), entry._refCount );
            }
        }

        return handle;
    }

    template<typename T> requires std::is_base_of_v<CachedResource, T>
    Handle<T> ResourceCache::RetrieveOrAllocateHandle( const size_t descriptorHash, bool& wasInCache )
    {
        ResourcePool<T>& pool = GetPool<T>(s_renderAPI);
        {
            SharedLock<SharedMutex> r_lock(pool._lock);
            const Handle<T> ret = pool.retrieveHandleLocked(descriptorHash);
            if ( ret != INVALID_HANDLE<T> )
            {
                wasInCache = true;
                return ret;
            }
        }

        LockGuard<SharedMutex> w_lock( pool._lock );
        // Check again
        const Handle<T> ret = pool.retrieveHandleLocked( descriptorHash );
        if ( ret != INVALID_HANDLE<T> )
        {
            wasInCache = true;
            return ret;
        }

        // Cache miss. Allocate new resource
        return pool.allocateLocked(descriptorHash);
    }


    template <typename T> requires std::is_base_of_v<CachedResource, T>
    T* ResourceCache::Get( const Handle<T> handle )
    {
        if ( handle != INVALID_HANDLE<T> ) [[likely]]
        {
            ResourcePool<T>& pool = GetPool<T>( s_renderAPI );
            SharedLock<SharedMutex> r_lock( pool._lock );
            if ( pool._freeList[handle._index].second == handle._generation )
            {
                return pool._resPool[handle._index]._ptr;
            }
        }

        return nullptr;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    void ResourceCache::Destroy( Handle<T>& handle, const bool immediate )
    {
        if ( handle == INVALID_HANDLE<T> ) [[unlikely]]
        {
            //Perfectly valid operation (e.g. material texture slots). Easier to check here instead of every single DestroyResourceCall.
            NOP();
            return;
        }

        if ( immediate || !s_enabled ) [[unlikely]]
        {
            GetPool<T>( s_renderAPI ).deallocate( handle );
        }
        else
        {
            GetPool<T>( s_renderAPI ).queueDeletion( handle );
        }
        
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    ResourcePtr<T> ResourceCache::AllocateInternal( const ResourceDescriptor<T>& descriptor )
    {
        return GetMemPool<T>().newElement( descriptor );
    }

    template<>
    inline ResourcePtr<ShaderProgram> ResourceCache::AllocateInternal<ShaderProgram>( const ResourceDescriptor<ShaderProgram>& descriptor )
    {
        switch ( s_renderAPI )
        {
            case RenderAPI::None:   return GetMemPool<noShaderProgram>().newElement( *s_context, descriptor );
            case RenderAPI::OpenGL: return GetMemPool<glShaderProgram>().newElement( *s_context, descriptor );
            case RenderAPI::Vulkan: return GetMemPool<vkShaderProgram>().newElement( *s_context, descriptor );
            case RenderAPI::NRI_Vulkan:
            case RenderAPI::NRI_D3D12:
            case RenderAPI::NRI_D3D11:
            case RenderAPI::NRI_None:
                return GetMemPool<nriShaderProgram>().newElement(*s_context, descriptor);

            default:
            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return nullptr;
    }

    template<>
    inline ResourcePtr<Texture> ResourceCache::AllocateInternal<Texture>( const ResourceDescriptor<Texture>& descriptor )
    {
        switch ( s_renderAPI )
        {
            case RenderAPI::None:   return GetMemPool<noTexture>().newElement( *s_context, descriptor );
            case RenderAPI::OpenGL: return GetMemPool<glTexture>().newElement( *s_context, descriptor );
            case RenderAPI::Vulkan: return GetMemPool<vkTexture>().newElement( *s_context, descriptor );
            case RenderAPI::NRI_Vulkan:
            case RenderAPI::NRI_D3D12:
            case RenderAPI::NRI_D3D11:
            case RenderAPI::NRI_None:
                return GetMemPool<nriTexture>().newElement(*s_context, descriptor);

            default:
            case RenderAPI::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return nullptr;
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    ResourcePtr<T> ResourceCache::AllocateAndCommit( const Handle<T> handle, const ResourceDescriptor<T>& descriptor )
    {
        ResourcePool<T>& pool = GetPool<T>( s_renderAPI );

        LockGuard<SharedMutex> lock( pool._lock );
        ResourcePtr<T> ptr = AllocateInternal<T>( descriptor );
        if ( ptr != nullptr )
        {
            pool.commitLocked( handle, ptr );
        }

        return ptr;
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    void ResourceCache::Build( ResourcePtr<T> ptr, const ResourceDescriptor<T>& descriptor )
    {
        Time::ProfileTimer loadTimer{};

        loadTimer.start();
        if ( ptr != nullptr )
        {
            ptr->setState( ResourceState::RES_LOADING );

            if ( ptr->load( *s_context ) ) [[likely]]
            {
                ptr->setState( ResourceState::RES_THREAD_LOADED );
            }
            else
            {
                ptr->setState( ResourceState::RES_THREAD_LOAD_FAILED);
            }
        }
        loadTimer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( loadTimer.get() );

        if ( ptr != nullptr && ptr->getState() == ResourceState::RES_THREAD_LOADED ) [[likely]]
        {
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_BUILD" ),
                              descriptor.resourceName(),
                              ptr->typeName(),
                              ptr->getGUID(),
                              descriptor.getHash(),
                              durationMS );
        }
        else
        {
            Console::errorfn( LOCALE_STR( "RESOURCE_CACHE_BUILD_FAILED" ),
                              descriptor.resourceName(),
                              durationMS );
        }
    }

    template<typename T> requires std::is_base_of_v<Resource, T>
    ResourcePtr<T> ResourceCache::Allocate( const Handle<T> handle, const ResourceDescriptor<T>& descriptor, const size_t descriptorHash )
    {
        Time::ProfileTimer loadTimer{};
        loadTimer.start();
        ResourcePtr<T> ptr = AllocateAndCommit<T>( handle, descriptor );
        loadTimer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( loadTimer.get() );

        if ( ptr != nullptr ) [[likely]]
        {
            Console::printfn( LOCALE_STR( "RESOURCE_CACHE_ALLOCATE" ),
                              descriptor.resourceName(),
                              ptr->typeName(),
                              ptr->getGUID(),
                              descriptorHash,
                              durationMS );
        }
        else
        {
            Console::errorfn( LOCALE_STR("RESOURCE_CACHE_ALLOCATE_FAILED"),
                              descriptor.resourceName(),
                              durationMS );
        }

        return ptr;
    }

    template <typename T> requires std::is_base_of_v<CachedResource, T>
    Handle<T> ResourceCache::LoadResource( const ResourceDescriptor<T>& descriptor, bool& wasInCache, std::atomic_uint& taskCounter )
    {
        DIVIDE_ASSERT(s_enabled);

        taskCounter.fetch_add( 1u );

        // The loading process may change the resource descriptor so always use the user-specified descriptor hash for lookup!
        const size_t loadingHash = descriptor.getHash();

        // If two threads are trying to load the same resource at the same time, by the time one of them adds the resource to the cache, it's too late
        // So check if the hash is currently in the "processing" list, and if it is, just busy-spin until done
        // Once done, lock the hash for ourselves
        ResourceLoadLock res_lock( loadingHash, *s_context );
        /// Check cache first to avoid loading the same resource twice (or if we have stale, expired pointers in there)

        Handle<T> ret = RetrieveOrAllocateHandle<T>( loadingHash, wasInCache );
        if ( wasInCache )
        {
            taskCounter.fetch_sub( 1u );
            return ret;
        }

        Console::printfn( LOCALE_STR( "RESOURCE_CACHE_GET_RES" ), descriptor.resourceName().c_str(), loadingHash );

        ResourcePtr<T> ptr = ResourceCache::Allocate<T>(ret, descriptor, loadingHash);

        if ( ptr != nullptr )
        {
            s_context->taskPool(TaskPoolType::ASSET_LOADER).enqueue(
                *CreateTask([ptr, descriptor]( const Task& )
                            {
                                ResourceCache::Build<T>( ptr, descriptor );
                            }),
                            descriptor.waitForReady() ? TaskPriority::REALTIME : TaskPriority::HIGH,
                            [ptr, ret, &taskCounter, resName = descriptor.resourceName()]()
                            {
                                DIVIDE_ASSERT(ret != INVALID_HANDLE<T>);

                                if ( ptr->getState() == ResourceState::RES_THREAD_LOADED) [[likely]]
                                {
                                    if (ptr->postLoad())
                                    {
                                        ptr->setState( ResourceState::RES_LOADED );
                                    }
                                    else
                                    {
                                        ptr->setState(ResourceState::RES_LOAD_FAILED);
                                    }
                                }

                                if ( ptr->getState() != ResourceState::RES_LOADED)
                                {
                                    Console::printfn( LOCALE_STR( "ERROR_RESOURCE_CACHE_LOAD_RES_NAME" ), resName.c_str() );
                                    Handle<T> retCpy = ret;
                                    GetPool<T>(s_renderAPI).deallocate( retCpy );
                                }

                                taskCounter.fetch_sub( 1u );
                            }
                );
        }

        return ret;
    }

} //namespace Divide

#endif //DVD_RESOURCE_CACHE_INL_
