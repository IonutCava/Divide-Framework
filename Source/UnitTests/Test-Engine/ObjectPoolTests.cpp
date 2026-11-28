// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/ObjectPool.h"

#include <future>

namespace Divide
{
    namespace
    {
        struct PoolProbe
        {
            explicit PoolProbe( const U32 value = 0u, std::function<void()> onDestroy = {} )
                : _value( value )
                , _onDestroy( MOV( onDestroy ) )
            {
                s_alive.fetch_add( 1 );
            }

            ~PoolProbe()
            {
                s_alive.fetch_sub( 1 );
                if ( _onDestroy )
                {
                    _onDestroy();
                }
            }

            U32 _value;
            std::function<void()> _onDestroy;
            static inline std::atomic_int s_alive{ 0 };
        };

        using ProbePool = ObjectPool<PoolProbe, 2u, true>;
    }

    TEST_CASE( "Object pool handles go stale when their slot is freed and reused", "[object_pool]" )
    {
        ProbePool pool;
        const PoolHandle first = pool.allocate( 11u );
        const PoolHandle second = pool.allocate( 22u );
        REQUIRE( pool.find( first ) != nullptr );
        CHECK( pool.find( first )->_value == 11u );
        CHECK( pool.find( second )->_value == 22u );

        pool.deallocate( first );
        CHECK( pool.find( first ) == nullptr );
        CHECK( pool.find( second )->_value == 22u );

        const PoolHandle reused = pool.allocate( 33u );
        CHECK( reused._id == first._id );
        CHECK( reused._generation != first._generation );
        CHECK( pool.find( first ) == nullptr );
        REQUIRE( pool.find( reused ) != nullptr );
        CHECK( pool.find( reused )->_value == 33u );

        pool.deallocate( reused );
        pool.deallocate( second );
        CHECK( PoolProbe::s_alive.load() == 0 );
    }

    TEST_CASE( "Object pool grows past its initial capacity", "[object_pool]" )
    {
        ProbePool pool;
        constexpr U32 count = 40u;
        vector<PoolHandle> handles;
        for ( U32 i = 0u; i < count; ++i )
        {
            handles.push_back( pool.allocate( i ) );
        }
        for ( U32 i = 0u; i < count; ++i )
        {
            REQUIRE( pool.find( handles[i] ) != nullptr );
            CHECK( pool.find( handles[i] )->_value == i );
        }
        for ( const PoolHandle handle : handles )
        {
            pool.deallocate( handle );
        }
        CHECK( PoolProbe::s_alive.load() == 0 );
    }

    TEST_CASE( "Registered objects stay owned by the caller", "[object_pool]" )
    {
        ProbePool pool;
        PoolProbe external{ 5u };
        const PoolHandle handle = pool.registerExisting( external );
        CHECK( pool.find( handle ) == &external );

        pool.unregisterExisting( handle );
        CHECK( pool.find( handle ) == nullptr );
        CHECK( external._value == 5u );
        CHECK( PoolProbe::s_alive.load() == 1 );
    }

    TEST_CASE( "Object pool destructors run outside the pool lock", "[object_pool][threading]" )
    {
        ProbePool pool;
        const PoolHandle survivor = pool.allocate( 7u );

        std::promise<bool> reachedPool;
        std::future<bool> reachedPoolResult = reachedPool.get_future();
        std::thread helper;
        const PoolHandle doomed = pool.allocate( 1u, [&]
        {
            // A destructor that needs the pool must not be blocked by the deallocation that is running it.
            helper = std::thread( [&] { reachedPool.set_value( pool.find( survivor ) != nullptr ); } );
            auto ret = reachedPoolResult.wait_for( std::chrono::seconds( 5 ) );
            DIVIDE_UNUSED( ret );
        } );

        pool.deallocate( doomed );
        helper.join();
        REQUIRE( reachedPoolResult.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready );
        CHECK( reachedPoolResult.get() );
        CHECK( pool.find( doomed ) == nullptr );

        pool.deallocate( survivor );
        CHECK( PoolProbe::s_alive.load() == 0 );
    }

    TEST_CASE( "Scoped lockers batch unlocked pool operations", "[object_pool][threading]" )
    {
        ProbePool pool;
        PoolHandle first;
        PoolHandle second;
        {
            ProbePool::ScopedLocker lock( pool );
            first = pool.allocateUnlocked( 100u );
            second = pool.allocateUnlocked( 200u );
            REQUIRE( pool.findUnlocked( first ) != nullptr );
            CHECK( pool.findUnlocked( first )->_value == 100u );
            CHECK( pool.findUnlocked( second )->_value == 200u );
        }
        {
            ProbePool::ScopedSharedLocker lock( pool );
            CHECK( pool.findUnlocked( first )->_value == 100u );
            CHECK( pool.findUnlocked( second )->_value == 200u );
        }
        {
            ProbePool::ScopedLocker lock( pool );
            PoolProbe* detached = pool.unregisterExistingUnlocked( first );
            REQUIRE( detached != nullptr );
            CHECK( detached->_value == 100u );
            CHECK( pool.findUnlocked( first ) == nullptr );
            delete detached;
            pool.deallocateUnlocked( second );
            CHECK( pool.findUnlocked( second ) == nullptr );
        }
        CHECK( PoolProbe::s_alive.load() == 0 );

        // A held exclusive lock keeps readers out until the scope ends.
        std::atomic_bool readerFinished{ false };
        PoolHandle guarded;
        std::thread reader;
        {
            ProbePool::ScopedLocker lock( pool );
            guarded = pool.allocateUnlocked( 9u );
            reader = std::thread( [&]
            {
                auto ret = pool.find( guarded );
                DIVIDE_UNUSED( ret );
                readerFinished.store( true, std::memory_order_release );
            } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            CHECK_FALSE( readerFinished.load( std::memory_order_acquire ) );
        }
        reader.join();
        CHECK( readerFinished.load() );
        pool.deallocate( guarded );
    }

    TEST_CASE( "Object pool survives concurrent allocation and deallocation", "[object_pool][threading]" )
    {
        ProbePool pool;
        constexpr U32 workerCount = 4u;
        constexpr U32 iterations = 500u;
        std::atomic_uint mismatches{ 0u };
        std::array<std::thread, workerCount> workers;
        for ( U32 worker = 0u; worker < workerCount; ++worker )
        {
            workers[worker] = std::thread( [&, worker]
            {
                for ( U32 i = 0u; i < iterations; ++i )
                {
                    const U32 value = worker * iterations + i;
                    const PoolHandle handle = pool.allocate( value );
                    const PoolProbe* found = pool.find( handle );
                    if ( found == nullptr || found->_value != value )
                    {
                        mismatches.fetch_add( 1u );
                    }
                    pool.deallocate( handle );
                    if ( pool.find( handle ) != nullptr )
                    {
                        mismatches.fetch_add( 1u );
                    }
                }
            } );
        }
        for ( std::thread& worker : workers )
        {
            worker.join();
        }

        CHECK( mismatches.load() == 0u );
        CHECK( PoolProbe::s_alive.load() == 0 );
    }
}
