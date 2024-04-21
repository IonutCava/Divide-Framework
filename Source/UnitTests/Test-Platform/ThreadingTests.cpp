#include "UnitTests/unitTestCommon.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include <atomic>
#include <iostream>

namespace Divide
{

namespace
{
     void StartAndWait( Task& task, TaskPool& pool, const TaskPriority priority = TaskPriority::DONT_CARE, const DELEGATE<void>& onCompletionFunction = {})
    {
        Start( task, pool, priority, onCompletionFunction );
        Wait( task, pool );
    }

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
    init = test.init( HardwareThreadCount() );
    CHECK_TRUE( init );

    test.shutdown();
}

TEST_CASE( "Parallel For Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    Console::ToggleFlag( Console::Flags::ENABLE_ERROR_STREAM, false );

    TaskPool test( "PARALLEL_FOR_TEST" );

    const bool init = test.init( HardwareThreadCount() );
    CHECK_TRUE( init );

    constexpr U32 partitionSize = 4;
    constexpr U32 loopCount = partitionSize * 4 + 2;

    std::atomic_uint loopCounter = 0;
    std::atomic_uint totalCounter = 0;

    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = loopCount;
    descriptor._partitionSize = partitionSize;
    descriptor._cbk = [&totalCounter, &loopCounter]( [[maybe_unused]] const Task* parentTask, const U32 start, const U32 end ) noexcept
    {
        ++loopCounter;
        for ( U32 i = start; i < end; ++i )
        {
            ++totalCounter;
        }
    };

    parallel_for( test, descriptor );

    CHECK_EQUAL( loopCounter, 5u );
    CHECK_EQUAL( totalCounter, 18u );

    test.shutdown();
}


TEST_CASE( "Task Callback Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool test( "CALLBACK_TEST" );
    const bool init = test.init( HardwareThreadCount() );
    CHECK_TRUE( init );

    bool testValue = false;

    Task* job = CreateTask( []( [[maybe_unused]] const Task& parentTask )
                            {
                                Time::ProfileTimer timer;
                                timer.start();
                                TaskPool::PrintLine( "TaskCallbackTest: Thread sleeping for 500ms" );
                                SleepThread(Time::Milliseconds(500) );

                                timer.stop();
                                const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - Time::ProfileTimer::overhead() );
                                TaskPool::PrintLine( "TaskCallbackTest: Thread waking up (" + std::to_string( durationMS ) + "ms )" );
                            } );

    Start( *job, test, TaskPriority::DONT_CARE, [&testValue]()
            {
                TaskPool::PrintLine( "TaskCallbackTest: Callback called!" );
                testValue = true;
                TaskPool::PrintLine( "TaskCallbackTest: Value changed to: [ " + std::string( testValue ? "true" : "false" ) + " ]!" );
            } );

    CHECK_FALSE( testValue );
    TaskPool::PrintLine( "TaskCallbackTest: waiting for task!" );
    Wait( *job, test );

    CHECK_TRUE( Finished( *job ) );
    CHECK_FALSE( testValue );

    TaskPool::PrintLine( "TaskCallbackTest: flushing queue!" );
    const size_t callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL( callbackCount, 1u );
    TaskPool::PrintLine( "TaskCallbackTest: flushing test! Value: " + std::string( testValue ? "true" : "false" ) );
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
            TaskPool::PrintLine( "threadedFunction completed in: " + std::to_string( durationMS ) + " ms." );

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

    const bool init = test.init( to_U8( HardwareThreadCount() ));
    CHECK_TRUE( init );

    Task* job = CreateTask( [&]( const Task& parentTask )
                            {
                                threadedFunction( parentTask );
                            } );

    CHECK_FALSE( getTestValue() );

    Start( *job, test, TaskPriority::DONT_CARE, [&]() noexcept
            {
                setTestValue( false );
            } );

    CHECK_FALSE( getTestValue() );

    Wait( *job, test );

    CHECK_TRUE( getTestValue() );

    const size_t callbackCount = test.flushCallbackQueue();
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
        const bool init = test.init( to_U8( HardwareThreadCount() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;

        timer.start();
        Task* job = CreateTask( TASK_NOP );

        for ( size_t i = 0u; i < loopCountA; ++i )
        {
            Start( *CreateTask( job, TASK_NOP ), test );
        }

        StartAndWait( *job, test );

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        TaskPool::PrintLine( "Threading speed test: " + std::to_string( loopCountA ) + " tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }
    {
        TaskPool test("SPEED_TEST_PARALLEL_FOR");
        const bool init = test.init( to_U8( HardwareThreadCount() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;
        timer.start();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCountB;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = false;
        descriptor._cbk = []( [[maybe_unused]] const Task* parentTask, [[maybe_unused]] const U32 start, [[maybe_unused]] const U32 end )
        {
            NOP();
        };

        parallel_for( test, descriptor );
        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        TaskPool::PrintLine( "Threading speed test (parallel_for): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }
    {
        TaskPool test("SPEED_TEST_PARALLEL_FOR_CURRENT_THREAD");
        const bool init = test.init( to_U8( HardwareThreadCount() ) );
        CHECK_TRUE( init );

        Time::ProfileTimer timer;
        timer.start();

        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = loopCountB;
        descriptor._partitionSize = partitionSize;
        descriptor._useCurrentThread = true;
        descriptor._cbk = []( [[maybe_unused]] const Task* parentTask, [[maybe_unused]] const U32 start, [[maybe_unused]] const U32 end )
        {
            NOP();
        };

        parallel_for( test, descriptor );

        timer.stop();
        const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
        TaskPool::PrintLine( "Threading speed test (parallel_for - use current thread): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );

        test.shutdown();
    }
}

TEST_CASE( "Task Priority Test", "[threading_tests]" )
{
    platformInitRunListener::PlatformInit();

    TaskPool test("PRIORTIY_TEST");
    const bool init = test.init( HardwareThreadCount() );
    CHECK_TRUE( init );

    U32 callbackValue = 0u;

    Task* job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                            {
                                ++callbackValue;
                            } );

    StartAndWait( *job, test, TaskPriority::DONT_CARE, [&callbackValue]()
                    {
                        ++callbackValue;
                    } );

    CHECK_EQUAL( callbackValue, 1u );

    size_t callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL( callbackCount, 1u );
    CHECK_EQUAL( callbackValue, 2u );

    job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                        {
                            ++callbackValue;
                        } );

    StartAndWait( *job, test );
    CHECK_EQUAL( callbackValue, 3u );

    callbackCount = test.flushCallbackQueue();
    CHECK_EQUAL( callbackCount, 0u );
    CHECK_EQUAL( callbackValue, 3u );

    job = CreateTask( [&callbackValue]( [[maybe_unused]] const Task& parentTask )
                        {
                            ++callbackValue;
                        } );
    StartAndWait( *job, test, TaskPriority::REALTIME, [&callbackValue]()
                    {
                        ++callbackValue;
                    } );

    CHECK_EQUAL( callbackValue, 5u );

    test.shutdown();
}

} //namespace Divide
