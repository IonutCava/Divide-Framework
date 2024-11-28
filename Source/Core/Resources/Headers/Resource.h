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
#ifndef DVD_RESOURCE_H_
#define DVD_RESOURCE_H_

#include "Core/Headers/Hashable.h"

namespace Divide
{

/// When "CreateResource" is called, the resource is in "RES_UNKNOWN" state.
/// Once it has been instantiated it will move to the "RES_CREATED" state.
/// Calling "load" on a non-created resource will fail.
/// After "load" is called, the resource is move to the "RES_LOADING" state
/// Nothing can be done to the resource when it's in "RES_LOADING" state!
/// Once loading is complete, preferably in another thread,
/// the resource state will become "RES_THREAD_LOADED"
/// In the main thread, postLoad() will be called setting the state to RES_LOADED 
/// and the resource will be ready to be used (e.g. added to the SceneGraph)
/// Calling "unload" is only available for "RES_LOADED" state resources.
/// Calling this method will set the state to "RES_LOADING"
/// Once unloading is complete, the resource will become "RES_CREATED".
/// It will still exist, but won't contain any data.
/// RES_UNKNOWN and RES_CREATED are safe to delete

    enum class ResourceState : U8
    {
        RES_UNKNOWN = 0,            ///< The resource exists, but it's state is undefined
        RES_CREATED = 1,            ///< The pointer has been created and instantiated, but no data has been loaded
        RES_LOADING = 2,            ///< The resource is loading, creating data, parsing scripts, etc
        RES_THREAD_LOADED = 3,      ///< The resource is loaded but not yet available
        RES_THREAD_LOAD_FAILED = 4, ///< The resource was created but failed to load internally
        RES_LOADED = 5,             ///< The resource is available for usage
        RES_LOAD_FAILED = 6,        ///< The resource loaded fine, but failed to execute post load operations
        RES_UNLOADING = 7,          ///< The resource is unloading, deleting data, etc
        COUNT
    };

    class Resource : public GUIDWrapper
    {
      public:
        explicit Resource( std::string_view resourceName, std::string_view typeName );

        [[nodiscard]] ResourceState getState() const noexcept;

        PROPERTY_R( Str<32>, typeName );
        PROPERTY_R( Str<256>, resourceName );

      protected:
        virtual void setState( ResourceState currentState );

      protected:
        std::atomic<ResourceState> _resourceState;
    };

    [[nodiscard]] bool WaitForReady( Resource* res );
    [[nodiscard]] bool SafeToDelete( Resource* res );

    struct ResourceDescriptorBase;

    class CachedResource : public Resource
    {
        friend class ResourceCache;
        friend struct ResourceLoader;

       public:
        explicit CachedResource( const ResourceDescriptorBase& descriptor, std::string_view typeName);

         /// Loading and unloading interface
        virtual bool load( PlatformContext& context );
        virtual bool postLoad();
        virtual bool unload();

        void setState( ResourceState currentState ) final;

      protected:
        mutable Mutex _callbackLock{};
        PROPERTY_RW( ResourcePath, assetLocation );
        PROPERTY_RW( Str<256>, assetName );
        PROPERTY_R( size_t, descriptorHash );
    };

    template<typename T> requires std::is_base_of_v<CachedResource, T>
    using ResourcePtr = T*;

    template<typename T>
    struct PropertyDescriptor
    {
    };

    template<typename T>
    bool operator==( const PropertyDescriptor<T>& lhs, const PropertyDescriptor<T>& rhs ) noexcept;
    template<typename T>
    bool operator!=( const PropertyDescriptor<T>& lhs, const PropertyDescriptor<T>& rhs ) noexcept;


    template<typename T>
    [[nodiscard]] size_t GetHash( const PropertyDescriptor<T>& descriptor ) noexcept;

    struct ResourceDescriptorBase : public Hashable
    {
        explicit ResourceDescriptorBase( const std::string_view resourceName );

        [[nodiscard]] size_t getHash() const override;

        PROPERTY_RW( ResourcePath, assetLocation ); ///< Can't be fixed size due to the need to handle array textures, cube maps, etc
        PROPERTY_RW( Str<256>, assetName );     ///< Resource instance name (for lookup)
        PROPERTY_RW( Str<256>, resourceName );
        PROPERTY_RW( uint3, data, VECTOR3_ZERO ); ///< general data
        PROPERTY_RW( U32, enumValue, 0u );
        PROPERTY_RW( U32, ID, 0u );
        PROPERTY_RW( P32, mask ); ///< 4 bool values representing  ... anything ...
        PROPERTY_RW( bool, flag, false );
        PROPERTY_RW( bool, waitForReady, true );
    };

    [[nodiscard]] size_t GetHash(const ResourceDescriptorBase& descriptor) noexcept;

    template <typename T>
    struct ResourceDescriptor final : public ResourceDescriptorBase
    {
        explicit ResourceDescriptor( std::string_view resourceName );
        explicit ResourceDescriptor( std::string_view resourceName, const PropertyDescriptor<T>& descriptor );

        PropertyDescriptor<T> _propertyDescriptor;

        [[nodiscard]] size_t getHash() const final;
    };

    template<typename T>
    bool operator==( const ResourceDescriptor<T>& lhs, const ResourceDescriptor<T>& rhs ) noexcept;
    template<typename T>
    bool operator!=( const ResourceDescriptor<T>& lhs, const ResourceDescriptor<T>& rhs ) noexcept;

}  // namespace Divide

#endif //DVD_RESOURCE_H_

#include "Resource.inl"
