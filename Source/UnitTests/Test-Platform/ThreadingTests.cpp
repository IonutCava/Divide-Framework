// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/Headers/PlatformRuntime.h"
#include <atomic>
#include <iostream>

namespace Divide
{
    struct TaskUTWrapper
    {
        static size_t flushCallbackQueue(TaskPool& pool)
        {
            return Attorney::MainThreadTaskPool::flushCallbackQueue(pool);
        }
    };

namespace
{
    Mutex s_printLock;
    void PrintLine( const std::string_view line )
    {
        LockGuard<Mutex> lock( s_printLock );
        std::cout << line << std::endl;
    };

    void SleepThread(const D64 milliseconds )
    {
        const D64 start = Time::App::ElapsedMilliseconds();

        while ( true )
        {
            if ( Time::App::ElapsedMilliseconds() - start >= Time::Milliseconds( milliseconds ) )
            {
                break;
            }
            std::this_thread::yield();
        }

    }

    NO_INLINE U32 TouchStackPages( volatile U8* stack, const size_t size ) noexcept
    {
        U32 checksum = 0u;
        for ( size_t i = 0u; i < size; i += 4096u )
        {
            stack[i] = to_U8( i / 4096u + 1u );
            checksum += stack[i];
        }
        return checksum;
    }

    NO_INLINE U32 TouchWorkerStack() noexcept
    {
        volatile U8 stack[64u * 1024u];
        return TouchStackPages( stack, std::size( stack ) );
    }
};

TEST_CASE( "Task Pool Construction Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    Console::ToggleFlag( Console::Flags::ENABLE_ERROR_STREAM, false );

    TaskPool test("CONSTRUCTION_TEST");

    // Not enough workers
    bool init = test.init( 0 );
    CHECK_FALSE( init );

    // Valid
    init = test.init( 1 );
    CHECK_TRUE( init );

    // Double init
    init = test.init( std::thread::hardware_concurrency() );
    CHECK_TRUE( init );

    test.shutdown();
}

TEST_CASE( "Task-pool idle checks never wait on a queue", "[threading_tests][task-pool]" )
{
    using TaskPoolDetail::DequeueMayBlock;

    CHECK_FALSE( DequeueMayBlock( true, TaskPriority::HIGH ) );
    CHECK_FALSE( DequeueMayBlock( true, TaskPriority::DONT_CARE ) );
    CHECK_FALSE( DequeueMayBlock( false, TaskPriority::HIGH ) );
    CHECK( DequeueMayBlock( false, TaskPriority::DONT_CARE ) );
}

TEST_CASE( "Task workers retain stack space and aligned task storage", "[threading_tests][task-pool]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool pool{ "TASK_STACK_TEST" };
    REQUIRE( pool.init( 1u ) );
    SCOPE_EXIT
    {
        pool.shutdown();
    };

    std::atomic_uint checksum{ 0u };
    std::atomic_bool aligned{ false };
    Task* parent = CreateTask( [&]( const Task& )
    {
        Task* child = CreateTask( [&]( const Task& )
        {
            checksum = TouchWorkerStack();
        } );
        aligned = reinterpret_cast<uintptr_t>( child ) % alignof(Task) == 0u;
        pool.enqueue( *child, TaskPriority::REALTIME );
    } );
    pool.enqueue( *parent, TaskPriority::DONT_CARE_NO_IDLE );
    pool.wait( *parent );

    CHECK( aligned.load() );
    CHECK( checksum.load() == 136u );
}

TEST_CASE( "Task-pool waits help eligible work without running worker-only tasks", "[threading_tests][task-pool]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool pool{ "TASK_WAIT_TEST" };
    REQUIRE( pool.init( 1u ) );
    std::atomic_bool workerStarted{ false };
    std::atomic_bool releaseWorker{ false };
    SCOPE_EXIT
    {
        releaseWorker = true;
        pool.shutdown();
    };

    pool.enqueue( *CreateTask( [&]( const Task& )
    {
        workerStarted = true;
        while ( !releaseWorker.load() )
        {
            std::this_thread::yield();
        }
    } ), TaskPriority::HIGH );

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    while ( !workerStarted.load() && std::chrono::steady_clock::now() < deadline )
    {
        std::this_thread::yield();
    }
    REQUIRE( workerStarted.load() );

    constexpr U32 childCount = 32u;
    std::atomic_uint completedChildren{ 0u };
    Task* parent = CreateTask( TASK_NOP );
    for ( U32 i = 0u; i < childCount; ++i )
    {
        pool.enqueue( *CreateTask( parent, [&]( const Task& )
        {
            completedChildren.fetch_add( 1u );
        } ) );
    }
    pool.enqueue( *parent );
    pool.wait( *parent );
    CHECK( Finished( *parent ) );
    CHECK( completedChildren.load() == childCount );

    std::atomic_bool workerOnlyRan{ false };
    Task* workerOnly = CreateTask( [&]( const Task& )
    {
        workerOnlyRan = true;
    } );
    pool.enqueue( *workerOnly, TaskPriority::DONT_CARE_NO_IDLE );
    CHECK_FALSE( pool.threadWaiting() );
    CHECK_FALSE( workerOnlyRan.load() );

    releaseWorker = true;
    pool.wait( *workerOnly );
    CHECK( workerOnlyRan.load() );
}

TEST_CASE( "Realtime task waits service callbacks needed by a child", "[threading_tests][task-pool]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool pool{ "REALTIME_WAIT_TEST" };
    REQUIRE( pool.init( 1u ) );
    SCOPE_EXIT
    {
        pool.shutdown();
    };

    std::atomic_bool callbackExecuted{ false };
    std::atomic_bool callbackOnMain{ false };
    std::atomic_bool childOnMain{ false };
    std::atomic_bool childTimedOut{ false };
    bool parentOnMain = false;
    bool completionOnMain = false;

    Task* parent = CreateTask( [&]( const Task& )
    {
        parentOnMain = Runtime::isMainThread();
    } );
    Task* child = CreateTask( parent, [&]( const Task& )
    {
        childOnMain = Runtime::isMainThread();
        pool.enqueue( *CreateTask( TASK_NOP ), TaskPriority::HIGH, [&]
        {
            callbackOnMain = Runtime::isMainThread();
            callbackExecuted = true;
        } );

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while ( !callbackExecuted.load() && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::yield();
        }
        childTimedOut = !callbackExecuted.load();
    } );
    pool.enqueue( *child, TaskPriority::DONT_CARE_NO_IDLE );
    pool.enqueue( *parent, TaskPriority::REALTIME, [&]
    {
        completionOnMain = Runtime::isMainThread();
    } );

    CHECK_FALSE( childTimedOut.load() );
    CHECK_FALSE( childOnMain.load() );
    CHECK( callbackExecuted.load() );
    CHECK( callbackOnMain.load() );
    CHECK( parentOnMain );
    CHECK( completionOnMain );
    CHECK( Finished( *parent ) );
}


TEST_CASE( "Parallel For Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    Console::ToggleFlag( Console::Flags::ENABLE_ERROR_STREAM, false );

    TaskPool test( "PARALLEL_FOR_TEST" );

    const bool init = test.init( std::thread::hardware_concurrency() );
    CHECK_TRUE( init );

    constexpr U32 partitionSize = 4;
    constexpr U32 loopCount = partitionSize * 4 + 2;

    std::atomic_uint loopCounter = 0;
    std::atomic_uint totalCounter = 0;

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = loopCount;
    descriptor._partitionSize = partitionSize;
    Parallel_For( test, descriptor, [&totalCounter, &loopCounter]( [[maybe_unused]] const Task* parentTask, const U32 start, const U32 end ) noexcept
    {
        ++loopCounter;
        for ( U32 i = start; i < end; ++i )
        {
            ++totalCounter;
        }
    });

    CHECK_EQUAL( loopCounter, 5u );
    CHECK_EQUAL( totalCounter, 18u );

    test.shutdown();
}


TEST_CASE( "Task Callback Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool test( "CALLBACK_TEST" );
    const bool init = test.init( std::thread::hardware_concurrency() );
    CHECK_TRUE( init );

    bool testValue = false;

    Task* job = CreateTask( []( [[maybe_unused]] const Task& parentTask )
                            {
                                Time::ProfileTimer timer;
                                timer.start();
                                PrintLine( "TaskCallbackTest: Thread sleeping for 500ms" );
                                SleepThread(Time::Milliseconds(500) );

                                timer.stop();
                                const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - Time::ProfileTimer::overhead() );
                                PrintLine( "TaskCallbackTest: Thread waking up (" + std::to_string( durationMS ) + "ms )" );
                            } );

    test.enqueue( *job, TaskPriority::DONT_CARE, [&testValue]()
            {
                PrintLine( "TaskCallbackTest: Callback called!" );
                testValue = true;
                PrintLine( "TaskCallbackTest: Value changed to: [ " + std::string( testValue ? "true" : "false" ) + " ]!" );
            } );

    CHECK_FALSE( testValue );
    PrintLine( "TaskCallbackTest: waiting for task!" );
    test.wait( *job );

    CHECK_TRUE( Finished( *job ) );
    CHECK_FALSE( testValue );

    PrintLine( "TaskCallbackTest: flushing queue!" );
    const size_t callbackCount = TaskUTWrapper::flushCallbackQueue(test);
    CHECK_EQUAL( callbackCount, 1u );
    PrintLine( "TaskCallbackTest: flushing test! Value: " + std::string( testValue ? "true" : "false" ) );
    CHECK_TRUE( testValue );

    test.shutdown();
}


namespace
{
    struct ThreadedTest
    {
        void setTestValue( const bool state ) noexcept
        {
            _testValue.store( state, std::memory_order_release );
        }

        [[nodiscard]] bool getTestValue() const noexcept
        {
            return _testValue.load( std::memory_order_acquire );
        }

        void threadedFunction( [[maybe_unused]] const Task& parentTask )
        {
            Time::ProfileTimer timer;
            timer.start();
            SleepThread( Time::Milliseconds( 300 ) );
            timer.stop();
            const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - Time::ProfileTimer::overhead() );
            PrintLine( "threadedFunction completed in: " + std::to_string( durationMS ) + " ms." );

            setTestValue( true );
        }

        private:
        std::atomic_bool _testValue{ false };
    };
}

TEST_CASE_METHOD( ThreadedTest, "Task Class Member Callback Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool test("MEMBER_CALLBACK_TEST");

    const bool init = test.init( to_U8( std::thread::hardware_concurrency() ));
    CHECK_TRUE( init );

    Task* job = CreateTask( [&]( const Task& parentTask )
                            {
                                threadedFunction( parentTask );
                            } );

    CHECK_FALSE( getTestValue() );

    test.enqueue( *job, TaskPriority::DONT_CARE, [&]() noexcept
            {
                setTestValue( false );
            } );

    CHECK_FALSE( getTestValue() );

    test.wait( *job );

    CHECK_TRUE( getTestValue() );

    const size_t callbackCount = TaskUTWrapper::flushCallbackQueue(test);
    CHECK_EQUAL( callbackCount, 1u );

    const bool finalValue = getTestValue();

    CHECK_FALSE( finalValue );

    test.shutdown();
}

TEST_CASE( "Task Speed Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    constexpr size_t loopCountA = 60u * 1000u;
    constexpr U32 partitionSize = 256u;
    constexpr U32 loopCountB = partitionSize * 8192u + 2u;

    const U64 timerOverhead = Time::ProfileTimer::overhead();
    {
        TaskPool test("SPEED_TEST_LOOP");
        const bool init = test.init( to_U8( std::thread::hardware_concurrency() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;

        timer.start();
        Task* job = CreateTask( TASK_NOP );

        for ( size_t i = 0u; i < loopCountA; ++i )
        {
            test.enqueue( *CreateTask( job, TASK_NOP ) );
        }

        test.enqueue( *job, TaskPriority::DONT_CARE);
        test.wait( *job );
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        PrintLine( "Threading speed test: " + std::to_string( loopCountA ) + " tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = loopCountB;
    descriptor._partitionSize = partitionSize;

    {
        TaskPool test("SPEED_TEST_PARALLEL_FOR");
        const bool init = test.init( to_U8( std::thread::hardware_concurrency() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;
        timer.start();

        descriptor._useCurrentThread = false;
        Parallel_For( test, descriptor, []( [[maybe_unused]] const Task* parentTask, [[maybe_unused]] const U32 start, [[maybe_unused]] const U32 end )
        {
            NOP();
        });
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        PrintLine( "Threading speed test (Parallel_For): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }
    {
        TaskPool test("SPEED_TEST_PARALLEL_FOR_CURRENT_THREAD");
        const bool init = test.init( to_U8( std::thread::hardware_concurrency() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;
        timer.start();

        descriptor._useCurrentThread = true;
        Parallel_For( test, descriptor, []( [[maybe_unused]] const Task* parentTask, [[maybe_unused]] const U32 start, [[maybe_unused]] const U32 end )
        {
            NOP();
        });

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        PrintLine( "Threading speed test (Parallel_For - use current thread): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }
}

TEST_CASE( "Task Priority Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool test("PRIORTIY_TEST");
    const bool init = test.init( std::thread::hardware_concurrency() );
    CHECK_TRUE( init );

    U32 callbackValue = 0u;

    Task* job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                            {
                                ++callbackValue;
                            } );

    test.enqueue( *job, TaskPriority::DONT_CARE, [&callbackValue]()
                    {
                        ++callbackValue;
                    });
    test.wait( *job );
    CHECK_EQUAL( callbackValue, 1u );

    size_t callbackCount = TaskUTWrapper::flushCallbackQueue(test);
    CHECK_EQUAL( callbackCount, 1u );
    CHECK_EQUAL( callbackValue, 2u );

    job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                        {
                            ++callbackValue;
                        } );

    test.enqueue( *job, TaskPriority::DONT_CARE);
    test.wait( *job );
    CHECK_EQUAL( callbackValue, 3u );

    job = CreateTask([&callbackValue]([[maybe_unused]] const Task& parentTask)
                                     {
                                         ++callbackValue;
                                     });

    test.enqueue( *job, TaskPriority::HIGH);
    test.wait( *job );
    CHECK_EQUAL(callbackValue, 4u);

    callbackCount = TaskUTWrapper::flushCallbackQueue(test);
    CHECK_EQUAL( callbackCount, 0u );
    CHECK_EQUAL( callbackValue, 4u );

    job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                        {
                            ++callbackValue;
                        } );
    test.enqueue( *job, TaskPriority::REALTIME, [&callbackValue]()
                    {
                        ++callbackValue;
                    } );
    test.wait( *job );
    CHECK_EQUAL( callbackValue, 6u );

    test.shutdown();
}

} //namespace Divide
