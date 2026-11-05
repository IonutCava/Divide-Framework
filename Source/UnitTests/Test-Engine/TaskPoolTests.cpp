// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Headers/ThreadPoolSizing.h"
#include "Platform/Headers/ConditionalWait.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide
{
    TEST_CASE( "Startup warmup keeps just one frame behind the splash", "[startup]" )
    {
        CHECK( Kernel::STARTUP_WARMUP_FRAME_COUNT == 1u );
    }

    TEST_CASE( "Asset-loader sizing respects the worker budget", "[threading][configuration]" )
    {
        using namespace ThreadPoolSizing;
        struct TestCase
        {
            U32 _total;
            U32 _requested;
            U32 _asset;
            U32 _high;
        };
        constexpr std::array cases{
            TestCase{ 0u,  0u, 4u,  4u },
            TestCase{ 16u, 0u, 4u,  4u },
            TestCase{ 17u, 0u, 4u,  4u },
            TestCase{ 18u, 0u, 5u,  4u },
            TestCase{ 20u, 0u, 7u,  4u },
            TestCase{ 21u, 0u, 8u,  4u },
            TestCase{ 32u, 0u, 8u, 15u },
            TestCase{ 32u, 4u, 4u, 19u },
            TestCase{ 32u, 1u, 4u, 19u },
            TestCase{ 32u, 9u, 8u, 15u },
            TestCase{ 18u, 8u, 5u,  4u }
        };

        for ( const TestCase& test : cases )
        {
            const Counts counts = Calculate( test._total, test._requested );
            INFO( "total=" << test._total << ", requested=" << test._requested );
            CHECK( counts._asset == test._asset );
            CHECK( counts._highPriority == test._high );
            CHECK( counts._render == RENDER_WORKERS );
            CHECK( counts._lowPriority == LOW_PRIORITY_WORKERS );
        }
    }

    TEST_CASE( "Asset-loader worker configuration defaults and round trips", "[threading][configuration]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-asset-workers-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove( directory / "missing.xml", error );
            std::filesystem::remove( directory / "saved.xml", error );
            std::filesystem::remove( directory, error );
        };
        const ResourcePath path{ directory.string() };
        {
            std::ofstream missing{ directory / "missing.xml" };
            missing << "<config><runtime/></config>";
            REQUIRE( missing.good() );
        }

        Configuration config{};
        CHECK( config.runtime.assetLoaderThreads == 0 );
        REQUIRE( XML::loadFromXML( config, path, "missing.xml" ) );
        CHECK( config.runtime.assetLoaderThreads == 0 );
        config.runtime.assetLoaderThreads = 4;
        REQUIRE( XML::saveToXML( config, path, "saved.xml" ) );
        Configuration restored{};
        REQUIRE( XML::loadFromXML( restored, path, "saved.xml" ) );
        CHECK( restored.runtime.assetLoaderThreads == 4 );
    }

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

    TEST_CASE( "Render task waits service main-thread completion callbacks", "[threading][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();

        const bool samePool = GENERATE( true, false );
        Application app;
        PlatformContext context{ app };
        TaskPool& renderPool = context.taskPool( TaskPoolType::RENDERER );
        TaskPool& otherPool = context.taskPool( TaskPoolType::ASSET_LOADER );
        REQUIRE( renderPool.init( 2u ) );
        SCOPE_EXIT
        {
            renderPool.shutdown();
        };
        REQUIRE( otherPool.init( 1u ) );
        SCOPE_EXIT
        {
            otherPool.shutdown();
        };
        InitConditionalWait( context );

        std::atomic_bool callbackExecuted{ false };
        std::atomic_bool callbackOnMain{ false };
        std::atomic_bool workerOnMain{ false };
        std::atomic_bool workerTimedOut{ false };
        std::atomic_bool childFinished{ false };
        Task* parent = CreateTask( TASK_NOP );
        Task* child = CreateTask( parent, [&]( Task& )
        {
            workerOnMain = Runtime::isMainThread();
            TaskPool& completionPool = samePool ? renderPool : otherPool;
            completionPool.enqueue( *CreateTask( []( Task& ) {} ), TaskPriority::HIGH, [&]
            {
                callbackOnMain = Runtime::isMainThread();
                callbackExecuted = true;
            } );

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
            while ( !callbackExecuted.load() && std::chrono::steady_clock::now() < deadline )
            {
                std::this_thread::yield();
            }
            workerTimedOut = !callbackExecuted.load();
            childFinished = true;
        } );
        renderPool.enqueue( *child, TaskPriority::DONT_CARE_NO_IDLE );
        renderPool.enqueue( *parent );
        renderPool.wait( *parent );

        CHECK( Finished( *parent ) );
        CHECK( childFinished.load() );
        CHECK_FALSE( workerOnMain.load() );
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
