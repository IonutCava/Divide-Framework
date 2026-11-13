// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Core/Headers/ThreadPoolSizing.h"
#include "Managers/Headers/RenderStageSchedule.h"
#include "Managers/Headers/RenderViewSchedule.h"
#include "Platform/Headers/ConditionalWait.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide
{
    TEST_CASE( "A single recording lane stays on its caller without dispatching tasks", "[threading][render_stage][render_smoke]" )
    {
        TaskPool pool{ "Inline recording" };
        RenderViewSchedule schedule;
        schedule.add( 3u );
        schedule.add( 7u );
        schedule.add( 3u );
        const auto caller = std::this_thread::get_id();
        size_t visits = 0u;
        schedule.record( pool, [&]( const size_t lane, const size_t view )
        {
            CHECK( std::this_thread::get_id() == caller );
            CHECK( lane == 3u % RenderViewSchedule::LANE_COUNT );
            CHECK( view == visits++ );
        } );
        CHECK( visits == 3u );
    }

    TEST_CASE( "View recording lanes retain package affinity and deterministic reuse", "[threading][render_stage][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{ app };
        TaskPool& pool = context.taskPool( TaskPoolType::RENDERER );
        REQUIRE( pool.init( 4u ) );
        SCOPE_EXIT { pool.shutdown(); };
        InitConditionalWait( context );

        RenderViewSchedule schedule;
        bool called = false;
        schedule.record( pool, [&]( size_t, size_t ) { called = true; } );
        CHECK_FALSE( called );

        constexpr std::array<U16, 12u> keys{{ 3u, 0u, 7u, 1u, 2u, 3u, 1u, 6u, 0u, 7u, 2u, 3u }};
        schedule.reserve( keys.size() );
        for ( U32 frame = 0u; frame < 32u; ++frame )
        {
            schedule.reset();
            for ( const U16 key : keys )
            {
                schedule.add( key );
            }
            std::array<size_t, keys.size()> recordedLanes{};
            std::array<U32, keys.size()> visits{};
            std::array<size_t, RenderViewSchedule::LANE_COUNT> previous{};
            std::array<bool, RenderViewSchedule::LANE_COUNT> ordered{};
            ordered.fill( true );
            schedule.record( pool, [&]( const size_t lane, const size_t view )
            {
                ordered[lane] = ordered[lane] && (previous[lane] == 0u || previous[lane] < view + 1u);
                previous[lane] = view + 1u;
                recordedLanes[view] = lane;
                ++visits[view];
                TaskGroup nested{ pool };
                nested.enqueue( []( const Task& ) {} );
            } );
            for ( size_t view = 0u; view < keys.size(); ++view )
            {
                CHECK( visits[view] == 1u );
                CHECK( recordedLanes[view] == keys[view] % RenderViewSchedule::LANE_COUNT );
            }
            for ( const bool value : ordered )
            {
                CHECK( value );
            }
            size_t serialCount = 0u;
            schedule.record( pool, [&]( const size_t lane, const size_t view )
            {
                CHECK( view == serialCount++ );
                CHECK( lane == recordedLanes[view] );
            }, false );
            CHECK( serialCount == keys.size() );
        }
        schedule.reset();
        for ( size_t lane = 0u; lane < RenderViewSchedule::LANE_COUNT; ++lane )
        {
            CHECK_FALSE( schedule.usesLane( lane ) );
        }
    }

    TEST_CASE( "Empty render task groups never reserve pooled parent slots", "[threading][render_stage][render_smoke]" )
    {
        TaskPool pool{ "Empty groups" };
        bool allEmpty = true;
        for ( U32 i = 0u; i < Config::MAX_POOLED_TASKS + 1u; ++i )
        {
            TaskGroup group{ pool };
            allEmpty = allEmpty && group.empty();
            group.wait();
            allEmpty = allEmpty && group.empty();
        }
        CHECK( allEmpty );
    }

    TEST_CASE( "Render task groups join children and can be reused after waiting", "[threading][render_stage][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{ app };
        TaskPool& pool = context.taskPool( TaskPoolType::RENDERER );
        REQUIRE( pool.init( 2u ) );
        SCOPE_EXIT { pool.shutdown(); };
        InitConditionalWait( context );

        std::atomic_uint completed{ 0u };
        TaskGroup group{ pool };
        for ( U32 iteration = 0u; iteration < 128u; ++iteration )
        {
            CHECK( group.empty() );
            group.enqueue( [&]( const Task& ) { completed.fetch_add( 1u ); } );
            group.enqueue( [&]( const Task& )
            {
                TaskGroup nested{ pool };
                nested.enqueue( [&]( const Task& ) { completed.fetch_add( 1u ); } );
            } );
            CHECK_FALSE( group.empty() );
            group.wait();
            CHECK( completed.load() == 2u * (iteration + 1u) );
            CHECK( group.empty() );
            group.wait();
        }
        {
            TaskGroup joinedOnExit{ pool };
            joinedOnExit.enqueue( [&]( const Task& ) { completed.fetch_add( 1u ); } );
        }
        CHECK( completed.load() == 257u );
    }

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
            std::filesystem::remove( directory / "saved.xml.bak", error );
            std::filesystem::remove( directory / "restore.xml", error );
            std::filesystem::remove( directory / "restore.xml.bak", error );
            std::filesystem::remove( directory / "blocker", error );
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

        const std::filesystem::path blocker = directory / "blocker";
        {
            std::ofstream output{ blocker };
            REQUIRE( output.good() );
        }
        CHECK_FALSE( XML::saveToXML( config, ResourcePath{ blocker.string() }, "saved.xml" ) );

        REQUIRE( std::filesystem::create_directory( directory / "saved.xml.bak" ) );
        config.runtime.assetLoaderThreads = 5;
        REQUIRE( XML::saveToXML( config, path, "saved.xml" ) );
        REQUIRE( XML::loadFromXML( restored, path, "saved.xml" ) );
        CHECK( restored.runtime.assetLoaderThreads == 5 );

        REQUIRE( std::filesystem::create_directory( directory / "restore.xml" ) );
        {
            std::ofstream backup{ directory / "restore.xml.bak" };
            backup << "<config><runtime/></config>";
            REQUIRE( backup.good() );
        }
        CHECK_FALSE( XML::loadFromXML( restored, path, "restore.xml" ) );
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

    TEST_CASE( "Render stages enqueue only after all distinct dependencies finish", "[threading][render_stage]" )
    {
        constexpr U8 shadow = to_base( RenderStage::SHADOW );
        constexpr U8 reflection = to_base( RenderStage::REFLECTION );
        constexpr U8 refraction = to_base( RenderStage::REFRACTION );
        constexpr U8 display = to_base( RenderStage::DISPLAY );
        constexpr U8 preview = to_base( RenderStage::NODE_PREVIEW );
        RenderStageSchedule schedule{};

        REQUIRE( schedule.addDependency( reflection, shadow ) );
        REQUIRE( schedule.addDependency( reflection, shadow ) );
        REQUIRE( schedule.addDependency( refraction, shadow ) );
        REQUIRE( schedule.addDependency( display, reflection ) );
        REQUIRE( schedule.addDependency( display, refraction ) );
        REQUIRE( schedule.addDependency( preview, reflection ) );
        REQUIRE( schedule.addDependency( preview, refraction ) );
        REQUIRE( schedule.isAcyclic() );
        schedule.prepare();

        std::array<U32, RenderStageSchedule::STAGE_COUNT> enqueued{};
        const auto enqueue = [&]( const U8 stage )
        {
            ++enqueued[stage];
        };
        schedule.enqueueRoots( enqueue );
        CHECK( enqueued[shadow] == 1u );
        CHECK( enqueued[reflection] == 0u );
        CHECK( enqueued[refraction] == 0u );
        CHECK( enqueued[display] == 0u );
        CHECK( enqueued[preview] == 0u );

        schedule.complete( shadow, enqueue );
        CHECK( enqueued[reflection] == 1u );
        CHECK( enqueued[refraction] == 1u );
        CHECK( enqueued[display] == 0u );
        CHECK( enqueued[preview] == 0u );
        schedule.complete( reflection, enqueue );
        CHECK( enqueued[display] == 0u );
        CHECK( enqueued[preview] == 0u );
        schedule.complete( refraction, enqueue );
        CHECK( enqueued[display] == 1u );
        CHECK( enqueued[preview] == 1u );
    }

    TEST_CASE( "Render-stage roots do not enqueue stages released concurrently", "[threading][render_stage]" )
    {
        RenderStageSchedule schedule{};
        REQUIRE( schedule.addDependency( 1u, 0u ) );
        REQUIRE( schedule.isAcyclic() );
        schedule.prepare();

        std::array<U32, RenderStageSchedule::STAGE_COUNT> enqueued{};
        schedule.enqueueRoots( [&]( const U8 stage )
        {
            ++enqueued[stage];
            if ( stage == 0u )
            {
                schedule.complete( stage, [&]( const U8 ready )
                {
                    ++enqueued[ready];
                } );
            }
        } );
        for ( const U32 count : enqueued )
        {
            CHECK( count == 1u );
        }
    }

    TEST_CASE( "Render-stage submission order respects dependencies regardless of stage index", "[threading][render_stage]" )
    {
        constexpr U8 shadow = to_base( RenderStage::SHADOW );
        constexpr U8 reflection = to_base( RenderStage::REFLECTION );
        constexpr U8 refraction = to_base( RenderStage::REFRACTION );
        constexpr U8 display = to_base( RenderStage::DISPLAY );
        constexpr U8 preview = to_base( RenderStage::NODE_PREVIEW );

        RenderStageSchedule schedule{};
        // Invert the natural index order so the result cannot simply be 0..N-1.
        REQUIRE( schedule.addDependency( shadow, display ) );
        REQUIRE( schedule.addDependency( display, reflection ) );
        REQUIRE( schedule.addDependency( display, refraction ) );
        REQUIRE( schedule.addDependency( reflection, preview ) );

        std::array<U8, RenderStageSchedule::STAGE_COUNT> order{};
        REQUIRE( schedule.topologicalOrder( order ) );

        std::array<size_t, RenderStageSchedule::STAGE_COUNT> position{};
        std::array<bool, RenderStageSchedule::STAGE_COUNT> seen{};
        for ( size_t i = 0u; i < order.size(); ++i )
        {
            REQUIRE( order[i] < RenderStageSchedule::STAGE_COUNT );
            CHECK_FALSE( seen[order[i]] );
            seen[order[i]] = true;
            position[order[i]] = i;
        }

        CHECK( position[preview] < position[reflection] );
        CHECK( position[reflection] < position[display] );
        CHECK( position[refraction] < position[display] );
        CHECK( position[display] < position[shadow] );

        schedule.reset();
        REQUIRE( schedule.addDependency( 0u, 1u ) );
        REQUIRE( schedule.addDependency( 1u, 0u ) );
        CHECK_FALSE( schedule.topologicalOrder( order ) );
    }

    TEST_CASE( "Render-stage dependencies reject invalid indices and cycles", "[threading][render_stage]" )
    {
        RenderStageSchedule schedule{};
        constexpr U8 invalid = to_base( RenderStage::COUNT );
        CHECK_FALSE( schedule.addDependency( invalid, 0u ) );
        CHECK_FALSE( schedule.addDependency( 0u, invalid ) );
        REQUIRE( schedule.addDependency( 0u, 1u ) );
        REQUIRE( schedule.addDependency( 1u, 0u ) );
        CHECK_FALSE( schedule.isAcyclic() );
        schedule.reset();
        REQUIRE( schedule.addDependency( 0u, 0u ) );
        CHECK_FALSE( schedule.isAcyclic() );
        schedule.reset();
        CHECK( schedule.isAcyclic() );
    }

    TEST_CASE( "Render-stage dependency completions publish and enqueue exactly once", "[threading][render_stage]" )
    {
        constexpr U8 shadow = to_base( RenderStage::SHADOW );
        constexpr U8 reflection = to_base( RenderStage::REFLECTION );
        constexpr U8 refraction = to_base( RenderStage::REFRACTION );
        constexpr U8 display = to_base( RenderStage::DISPLAY );
        constexpr U8 preview = to_base( RenderStage::NODE_PREVIEW );

        for ( U32 iteration = 0u; iteration < 64u; ++iteration )
        {
            RenderStageSchedule schedule{};
            REQUIRE( schedule.addDependency( reflection, shadow ) );
            REQUIRE( schedule.addDependency( refraction, shadow ) );
            REQUIRE( schedule.addDependency( display, reflection ) );
            REQUIRE( schedule.addDependency( display, refraction ) );
            REQUIRE( schedule.addDependency( preview, reflection ) );
            REQUIRE( schedule.addDependency( preview, refraction ) );
            schedule.prepare();

            std::array<std::atomic_uint, RenderStageSchedule::STAGE_COUNT> enqueued{};
            const auto enqueue = [&]( const U8 stage )
            {
                enqueued[stage].fetch_add( 1u );
            };
            schedule.enqueueRoots( enqueue );
            schedule.complete( shadow, enqueue );

            std::thread first( [&] { schedule.complete( reflection, enqueue ); } );
            std::thread second( [&] { schedule.complete( refraction, enqueue ); } );
            first.join();
            second.join();
            for ( const auto& count : enqueued )
            {
                CHECK( count.load() == 1u );
            }
        }
    }

    TEST_CASE( "Ready render-stage tasks retain worker and main-thread callback progress", "[threading][render_stage][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{ app };
        TaskPool& pool = context.taskPool( TaskPoolType::RENDERER );
        REQUIRE( pool.init( 4u ) );
        InitConditionalWait( context );

        constexpr U8 shadow = to_base( RenderStage::SHADOW );
        constexpr U8 reflection = to_base( RenderStage::REFLECTION );
        constexpr U8 refraction = to_base( RenderStage::REFRACTION );
        constexpr U8 display = to_base( RenderStage::DISPLAY );
        constexpr U8 preview = to_base( RenderStage::NODE_PREVIEW );
        RenderStageSchedule schedule{};
        const bool valid = schedule.addDependency( reflection, shadow ) &&
                           schedule.addDependency( refraction, shadow ) &&
                           schedule.addDependency( display, reflection ) &&
                           schedule.addDependency( display, refraction ) &&
                           schedule.addDependency( preview, reflection ) &&
                           schedule.addDependency( preview, refraction ) &&
                           schedule.isAcyclic();

        std::array<Task*, RenderStageSchedule::STAGE_COUNT> tasks{};
        std::array<std::atomic_uint, RenderStageSchedule::STAGE_COUNT> visits{};
        std::atomic_uint completedMask{ 0u };
        std::atomic_uint completedCount{ 0u };
        std::atomic_uint violations{ 0u };
        std::atomic_bool callbackExecuted{ false };
        std::atomic_bool callbackOnMain{ false };
        std::atomic_bool callbackTimedOut{ false };
        const auto enqueue = [&]( const U8 stage )
        {
            pool.enqueue( *tasks[stage], TaskPriority::DONT_CARE_NO_IDLE );
        };

        if ( valid )
        {
            schedule.prepare();
            for ( U8 stage = 0u; stage < to_base( RenderStage::COUNT ); ++stage )
            {
                tasks[stage] = CreateTask( [&, stage]( const Task& )
                {
                    const U32 required = stage == shadow ? 0u :
                                         (stage == reflection || stage == refraction) ? 1u << shadow :
                                         (1u << reflection) | (1u << refraction);
                    if ( Runtime::isMainThread() || (completedMask.load() & required) != required )
                    {
                        violations.fetch_add( 1u );
                    }
                    if ( stage == shadow )
                    {
                        pool.enqueue( *CreateTask( []( const Task& ) {} ), TaskPriority::HIGH, [&]
                        {
                            callbackOnMain = Runtime::isMainThread();
                            callbackExecuted = true;
                        } );
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
                        while ( !callbackExecuted.load() && std::chrono::steady_clock::now() < deadline )
                        {
                            std::this_thread::yield();
                        }
                        callbackTimedOut = !callbackExecuted.load();
                    }
                    visits[stage].fetch_add( 1u );
                    completedMask.fetch_or( 1u << stage );
                    schedule.complete( stage, enqueue );
                    completedCount.fetch_add( 1u );
                } );
            }
            schedule.enqueueRoots( enqueue );
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while ( completedCount.load() < RenderStageSchedule::STAGE_COUNT && std::chrono::steady_clock::now() < deadline )
        {
            context.idle();
            std::this_thread::yield();
        }
        pool.shutdown();

        REQUIRE( valid );
        CHECK( completedCount.load() == RenderStageSchedule::STAGE_COUNT );
        CHECK( violations.load() == 0u );
        CHECK( callbackExecuted.load() );
        CHECK( callbackOnMain.load() );
        CHECK_FALSE( callbackTimedOut.load() );
        for ( const auto& count : visits )
        {
            CHECK( count.load() == 1u );
        }
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
