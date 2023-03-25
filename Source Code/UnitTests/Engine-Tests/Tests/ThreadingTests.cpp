#include "stdafx.h"

#include "Headers/Defines.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include <atomic>

namespace Divide
{

    namespace
    {
        Mutex printLock{};

        void printLine( const char* line )
        {
            LockGuard<Mutex> lock( printLock );
            std::cout << line << std::endl;
        };

        void printLine( const std::string& string )
        {
            printLine( string.c_str() );
        }

        void StartAndWait( Task& task, TaskPool& pool, const TaskPriority priority = TaskPriority::DONT_CARE, const DELEGATE<void>& onCompletionFunction = {})
        {
            Start( task, pool, priority, onCompletionFunction );
            Wait( task, pool );
        }
    };

    TEST( TaskPoolContructionTest )
    {
        Console::ToggleFlag( Console::Flags::ENABLE_ERROR_STREAM, false );

        TaskPool test;

        // Not enough workers
        bool init = test.init( 0, TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_FALSE( init );

        // Valid
        init = test.init( 1, TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_TRUE( init );

        // Double init
        init = test.init( HardwareThreadCount(), TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_FALSE( init );
    }

    TEST( ParallelForTest )
    {
        Console::ToggleFlag( Console::Flags::ENABLE_ERROR_STREAM, false );

        TaskPool test;
        const bool init = test.init( HardwareThreadCount(), TaskPool::TaskPoolType::TYPE_BLOCKING );
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
    }


    TEST( TaskCallbackTest )
    {
        TaskPool test;
        const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_TRUE( init );

        bool testValue = false;

        Task* job = CreateTask( []( [[maybe_unused]] const Task& parentTask )
                                {
                                    Time::ProfileTimer timer;
                                    timer.start();
                                    printLine( "TaskCallbackTest: Thread sleeping for 500ms" );
                                    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                                    timer.stop();
                                    const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - Time::ProfileTimer::overhead() );
                                    printLine( "TaskCallbackTest: Thread waking up (" + std::to_string( durationMS ) + "ms )" );
                                } );

        Start( *job, test, TaskPriority::DONT_CARE, [&testValue]()
               {
                   printLine( "TaskCallbackTest: Callback called!" );
                   testValue = true;
                   printLine( "TaskCallbackTest: Value changed to: [ " + std::string( testValue ? "true" : "false" ) + " ]!" );
               } );

        CHECK_FALSE( testValue );
        printLine( "TaskCallbackTest: waiting for task!" );
        Wait( *job, test );
        CHECK_TRUE( Finished( *job ) );
        CHECK_FALSE( testValue );

        printLine( "TaskCallbackTest: flushing queue!" );
        const size_t callbackCount = test.flushCallbackQueue();
        CHECK_EQUAL( callbackCount, 1u );
        printLine( "TaskCallbackTest: flushing test! Value: " + std::string( testValue ? "true" : "false" ) );
        CHECK_TRUE( testValue );
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
                std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
                timer.stop();
                const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - Time::ProfileTimer::overhead() );
                printLine( "threadedFunction completed in: " + std::to_string( durationMS ) + " ms." );

                setTestValue( true );
            }

            private:
            std::atomic_bool _testValue{ false };
        };
    }

    TEST( TaskClassMemberCallbackTest )
    {
        TaskPool test;
        const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_TRUE( init );

        ThreadedTest testObj;

        Task* job = CreateTask( [&testObj]( const Task& parentTask )
                                {
                                    testObj.threadedFunction( parentTask );
                                } );

        CHECK_FALSE( testObj.getTestValue() );

        Start( *job, test, TaskPriority::DONT_CARE, [&testObj]() noexcept
               {
                   testObj.setTestValue( false );
               } );

        CHECK_FALSE( testObj.getTestValue() );

        Wait( *job, test );

        CHECK_TRUE( testObj.getTestValue() );

        const size_t callbackCount = test.flushCallbackQueue();
        CHECK_EQUAL( callbackCount, 1u );

        const bool finalValue = testObj.getTestValue();

        CHECK_FALSE( finalValue );
    }

    TEST( TaskSpeedTest )
    {
        constexpr size_t loopCountA = 60u * 1000u;
        constexpr U32 partitionSize = 256u;
        constexpr U32 loopCountB = partitionSize * 8192u + 2u;

        const U64 timerOverhead = Time::ProfileTimer::overhead();
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
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
            printLine( "Threading speed test (blocking): " + std::to_string( loopCountA ) + " tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_LOCKFREE );
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
            printLine( "Threading speed test (lockfree): " + std::to_string( loopCountA ) + " tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
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
            printLine( "Threading speed test (parallel_for - blocking): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
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
            printLine( "Threading speed test (parallel_for - blocking - use current thread): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_LOCKFREE );
            CHECK_TRUE( init );

            ParallelForDescriptor descriptor = {};
            descriptor._iterCount = loopCountB;
            descriptor._partitionSize = partitionSize;
            descriptor._useCurrentThread = false;
            descriptor._cbk = []( [[maybe_unused]] const Task* parentTask, [[maybe_unused]] const U32 start, [[maybe_unused]] const U32 end )
            {
                NOP();
            };

            Time::ProfileTimer timer;
            timer.start();
            parallel_for( test, descriptor );
            timer.stop();
            const F32 durationMS = Time::MicrosecondsToMilliseconds<F32>( timer.get() - timerOverhead );
            printLine( "Threading speed test (parallel_for - lockfree): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
        {
            TaskPool test;
            const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_LOCKFREE );
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
            printLine( "Threading speed test (parallel_for - lockfree - use current thread): " + std::to_string( loopCountB / partitionSize ) + " partitions tasks completed in: " + std::to_string( durationMS ) + " ms." );
        }
    }

    TEST( TaskPriorityTest )
    {
        TaskPool test;
        const bool init = test.init( to_U8( HardwareThreadCount() ), TaskPool::TaskPoolType::TYPE_BLOCKING );
        CHECK_TRUE( init );

        U32 callbackValue = 0u;

        Task* job = CreateTask( [&callbackValue]( const Task& /*parentTask*/ )
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

        job = CreateTask( [&callbackValue]( const Task& /*parentTask*/ )
                          {
                              ++callbackValue;
                          } );

        StartAndWait( *job, test );
        CHECK_EQUAL( callbackValue, 3u );

        callbackCount = test.flushCallbackQueue();
        CHECK_EQUAL( callbackCount, 0u );
        CHECK_EQUAL( callbackValue, 3u );

        job = CreateTask( [&callbackValue]( const Task& /*parentTask*/ )
                          {
                              ++callbackValue;
                          } );
        StartAndWait( *job, test, TaskPriority::REALTIME, [&callbackValue]()
                      {
                          ++callbackValue;
                      } );
        CHECK_EQUAL( callbackValue, 5u );
    }

} //namespace Divide
