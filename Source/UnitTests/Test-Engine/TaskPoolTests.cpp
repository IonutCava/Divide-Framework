// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Platform/Headers/ConditionalWait.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide
{
    TEST_CASE( "Parallel loading waits process main-thread completion callbacks", "[threading][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();

        const bool samePool = GENERATE( true, false );
        const bool allowPoolIdle = GENERATE( true, false );
        Application app;
        PlatformContext context{ app };
        std::atomic_bool workerStarted{ false };
        std::atomic_bool callbackExecuted{ false };
        std::atomic_bool callbackOnMain{ false };
        std::atomic_bool workerTimedOut{ false };
        std::atomic_bool mainTimedOut{ false };

        TaskPool& loader = context.taskPool( TaskPoolType::ASSET_LOADER );
        TaskPool& other = context.taskPool( TaskPoolType::HIGH_PRIORITY );
        REQUIRE( loader.init( 2u ) );
        SCOPE_EXIT
        {
            loader.shutdown();
        };
        REQUIRE( other.init( 1u ) );
        SCOPE_EXIT
        {
            other.shutdown();
        };
        InitConditionalWait( context );

        ParallelForDescriptor descriptor{};
        descriptor._iterCount = 2u;
        descriptor._partitionSize = 1u;
        descriptor._priority = TaskPriority::HIGH;
        descriptor._allowPoolIdle = allowPoolIdle;
        Parallel_For( loader, descriptor, [&]( const Task*, const U32 start, const U32 )
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
            if ( start == 0u )
            {
                TaskPool& completionPool = samePool ? loader : other;
                completionPool.enqueue( *CreateTask( []( const Task& ) {} ), TaskPriority::HIGH, [&]
                {
                    callbackOnMain = Runtime::isMainThread();
                    callbackExecuted = true;
                } );
                workerStarted = true;
                while ( !callbackExecuted.load() && std::chrono::steady_clock::now() < deadline )
                {
                    std::this_thread::yield();
                }
                workerTimedOut = !callbackExecuted.load();
            }
            else
            {
                while ( !workerStarted.load() && std::chrono::steady_clock::now() < deadline )
                {
                    std::this_thread::yield();
                }
                mainTimedOut = !workerStarted.load();
            }
        } );

        CHECK_FALSE( mainTimedOut.load() );
        CHECK_FALSE( workerTimedOut.load() );
        CHECK( callbackExecuted.load() );
        CHECK( callbackOnMain.load() );
    }

    TEST_CASE( "Parallel terrain chunk jobs visit every distinct chunk once", "[threading][terrain][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        TaskPool pool{ "TERRAIN_CHUNK_TEST" };
        REQUIRE( pool.init( 3u ) );
        SCOPE_EXIT
        {
            pool.shutdown();
        };

        constexpr U32 chunkCount = 64u;
        std::array<std::atomic_uint, chunkCount> visits{};
        std::array<U32, chunkCount> chunkIDs{};
        for ( U32 i = 0u; i < chunkCount; ++i )
        {
            chunkIDs[i] = i;
        }

        ParallelForDescriptor descriptor{};
        descriptor._iterCount = chunkCount;
        descriptor._partitionSize = 1u;
        Parallel_For( pool, descriptor, [&]( const Task*, const U32 start, const U32 end )
        {
            for ( U32 i = start; i < end; ++i )
            {
                visits[chunkIDs[i]].fetch_add( 1u );
            }
        } );

        for ( const auto& count : visits )
        {
            CHECK( count.load() == 1u );
        }
    }
}
