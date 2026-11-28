// Copyright (c) 2026 DIVIDE-Studio

#include "Platform/Headers/PlatformRuntime.h"

#include <iostream>

namespace Divide
{
    namespace
    {
        std::atomic_uint g_taskIDCounter = 0u;

        // Lazily allocates a single contiguous flat array per thread that uses AllocateTask,
        // keeping cache locality without reserving 8 MiB of static TLS (.tbss) on every OS thread.
        thread_local std::unique_ptr<Task[]> g_taskAllocator;
        thread_local U32 g_allocatedTasks = 0u;

        template<typename Predicate>
        void WaitForSignal( Mutex& mutex,
                            std::condition_variable& finished,
                            std::atomic_uint& waiterCount,
                            const std::chrono::milliseconds timeout,
                            Predicate&& predicate )
        {
            waiterCount.fetch_add( 1u, std::memory_order_seq_cst );
            SCOPE_EXIT
            {
                waiterCount.fetch_sub( 1u, std::memory_order_seq_cst );
            };

            if ( predicate() )
            {
                return;
            }

            UniqueLock<Mutex> lock( mutex );
            if ( timeout.count() > 0 )
            {
                finished.wait_for( lock, timeout, FWD( predicate ) );
            }
            else
            {
                finished.wait( lock, FWD( predicate ) );
            }
        }

        void WaitForChildren( Mutex& mutex,
                              std::condition_variable& finished,
                              std::atomic_uint& waiterCount,
                              const Task& task )
        {
            const U32 pendingJobs = task._unfinishedJobs.load();
            if ( pendingJobs <= 1u )
            {
                return;
            }

            WaitForSignal( mutex, finished, waiterCount, std::chrono::milliseconds( 2 ), [&task, pendingJobs]() noexcept
            {
                return task._unfinishedJobs.load() != pendingJobs;
            } );
        }
    }

    TaskPool::TaskPool( const std::string_view workerName )
        : _threadNamePrefix( workerName )
    {
        _isRunning.store(false);
    }

    TaskPool::~TaskPool()
    {
        DIVIDE_ASSERT( _activeThreads.load() == 0u, "Task pool is still active! Threads should be joined before destroying the pool. Call TaskPool::shutdown() first");
    }

    bool TaskPool::init( const size_t threadCount, DELEGATE<void, size_t, const std::thread::id&>&& onThreadCreateCbk )
    {
        shutdown();
        if (threadCount == 0u)
        {
            return false;
        }

        _isRunning.store(true);
        _threads.reserve( threadCount );

        for (size_t idx = 0u; idx < threadCount; ++idx )
        {
            _threads.emplace_back
            (
                [&, idx]
                {
                    const auto threadName = Util::StringFormat( "{}_{}", _threadNamePrefix, idx );
                    Profiler::OnThreadStart( threadName );


                    SetThreadName( threadName );

                    if (onThreadCreateCbk)
                    {
                        onThreadCreateCbk( idx, std::this_thread::get_id() );
                    }

                    _activeThreads.fetch_add( 1u ) ;
                    while ( _isRunning.load() )
                    {
                        if ( !executeOneTask( false ) )
                        {
                            std::this_thread::yield();
                        }
                    }

                    Profiler::OnThreadStop();
                    _activeThreads.fetch_sub( 1u );
                }
            );
        }

        WAIT_FOR_CONDITION(_activeThreads.load() == threadCount, false);
        
        return true;
    }

    void TaskPool::shutdown()
    {
        join();
        _threads.clear();

        DELEGATE<void> discarded;
        while ( _threadedCallbackBuffer.try_dequeue( discarded ) )
        {
            discarded = {};
        }
    }

    void TaskPool::waitForAllTasks(const bool flushCallbacks)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Threading);

        if (!_isRunning.load())
        {
            return;
        }

        if (flushCallbacks)
        {
            DIVIDE_ASSERT(Runtime::isMainThread());
            using namespace std::chrono_literals;
            while (_runningTaskCount.load() > 0u)
            {
                PlatformContextIdleCall();
                flushCallbackQueue();

                WaitForSignal(_taskFinishedMutex, _taskFinishedCV, _waiterCount, 2ms, [this]() noexcept
                {
                    return _runningTaskCount.load() == 0u;
                });
            }
            flushCallbackQueue();
        }
        else if (_runningTaskCount.load() > 0u)
        {
            WaitForSignal(_taskFinishedMutex, _taskFinishedCV, _waiterCount, std::chrono::milliseconds(0), [this]() noexcept
            {
                return _runningTaskCount.load() == 0u;
            });
        }
    }

    void TaskPool::join()
    {
        waitForAllTasks( true );

        _isRunning.store(false);

        WAIT_FOR_CONDITION(_activeThreads.load() == 0u, false);

        for (std::thread& thread : _threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }

        }

        WAIT_FOR_CONDITION(_activeThreads.load() == 0u, false);
    }

    void TaskPool::enqueue( Task& task, const TaskPriority priority, DELEGATE<void>&& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        DIVIDE_ASSERT(_isRunning.load(), "TaskPool::enqueue error: Task pool is not running!");

        const bool realtime = priority == TaskPriority::REALTIME;
        if (realtime) [[unlikely]]
        {
            while (task._unfinishedJobs.load() > 1u)
            {
                if (Runtime::isMainThread())
                {
                    PlatformContextIdleCall();
                    if (flushCallbackQueue() == 0u && !threadWaiting())
                    {
                        WaitForChildren( _taskFinishedMutex, _taskFinishedCV, _waiterCount, task );
                    }
                }
                else if (!threadWaiting())
                {
                    WaitForChildren( _taskFinishedMutex, _taskFinishedCV, _waiterCount, task );
                }
            }

            runTask(task);
            if (onCompletionFunction)
            {
                onCompletionFunction();
            }
            return;
        }

        // Returning false from a PoolTask lambda will just reschedule it for later execution again. 
        // This may leave the task in an infinite loop, always re-queuing!
        auto poolTask = [this, &task, priority, onCompletion = MOV(onCompletionFunction)](const bool isIdleCall) mutable
        {
            while (task._unfinishedJobs.load() > 1u)
            {
                if (isIdleCall)
                {
                    // Can't be run at this time as we'll just recurse to infinity
                    return false;
                }

                // Else, we wait until our child tasks finish running. We also try and do some other work while waiting
                if ( !threadWaiting() )
                {
                    WaitForChildren( _taskFinishedMutex, _taskFinishedCV, _waiterCount, task );
                }
            }

            if (priority == TaskPriority::DONT_CARE_NO_IDLE && isIdleCall)
            {
                return false;
            }

            runTask(task, MOV(onCompletion));

            return true;
        };

        DIVIDE_EXPECTED_CALL( getQueue(priority).enqueue(MOV(poolTask)) );
    }

    void TaskPool::runTask( Task& task, DELEGATE<void>&& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        _runningTaskCount.fetch_add(1);

        if (task._callback ) [[likely]]
        {
            task._callback( task );
            task._callback = {}; //< Needed to cleanup any stale resources (e.g. captured by lambdas)
        }

        if (task._parent != nullptr)
        {
            task._parent->_unfinishedJobs.fetch_sub(1);
        }

        if (onCompletionFunction)
        {
            DIVIDE_EXPECTED_CALL( _threadedCallbackBuffer.enqueue( MOV(onCompletionFunction) ) );
        }

        task._unfinishedJobs.fetch_sub(1);
        _runningTaskCount.fetch_sub(1);

        if ( _waiterCount.load( std::memory_order_seq_cst ) > 0u )
        {
            LockGuard<Mutex> lock(_taskFinishedMutex);
            _taskFinishedCV.notify_all();
        }
    }

    void TaskPool::wait( const Task& task )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        using namespace std::chrono_literals;
        while ( !Finished( task ) )
        {
            PlatformContextIdleCall();
            if ( threadWaiting() )
            {
                continue;
            }

            WaitForSignal( _taskFinishedMutex, _taskFinishedCV, _waiterCount, 2ms, [&task]() noexcept
            {
                return Finished( task );
            } );
        }
    }

    size_t TaskPool::flushCallbackQueue()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        DIVIDE_ASSERT( Runtime::isMainThread() );

        constexpr size_t maxDequeueItems = 16u;
        std::array<DELEGATE<void>, maxDequeueItems> completedCallbacks{};

        size_t ret = 0u;
        while ( true )
        {
            const size_t count = _threadedCallbackBuffer.try_dequeue_bulk( completedCallbacks.data(), maxDequeueItems );
            if ( count == 0u )
            {
                break;
            }

            for ( size_t i = 0u; i < count; ++i )
            {
                if ( completedCallbacks[i] )
                {
                    completedCallbacks[i]();
                    completedCallbacks[i] = {};
                }
            }

            ret += count;
        }

        return ret;
    }

    Task* TaskPool::AllocateTask( Task* parentTask, DELEGATE<void, Task&>&& func ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( parentTask != nullptr )
        {
            parentTask->_unfinishedJobs.fetch_add( 1u );
        }

        if ( g_taskAllocator == nullptr )
        {
            g_taskAllocator = std::make_unique<Task[]>( Config::MAX_POOLED_TASKS );
        }

        U32 idx = 0u;
        Task* task = nullptr;
        do
        {
            idx = g_allocatedTasks++ & (Config::MAX_POOLED_TASKS - 1u);
            Task& crtTask = g_taskAllocator[idx];

            U32 expected = 0u;
            if ( crtTask._unfinishedJobs.compare_exchange_strong( expected, 1u ) )
            {
                task = &crtTask;
            }
        }
        while ( task == nullptr );

        task->_parent = parentTask;

        if (task->_globalId == Task::INVALID_TASK_ID)
        {
            task->_globalId = g_taskIDCounter.fetch_add(1u);
        }
        task->_callback = MOV( func );
        return task;
    }

    bool TaskPool::threadWaiting()
    {
        return executeOneTask( true );
    }

    bool TaskPool::executeOneTask( const bool isIdleCall )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Threading);

        PoolTask task = {};
        TaskPriority priorityOut = TaskPriority::DONT_CARE;

        if ( !deque( isIdleCall, task, priorityOut))
        {
            return false;
        }

        if ( !task( isIdleCall ) )
        {
            DIVIDE_EXPECTED_CALL( getQueue(priorityOut).enqueue( MOV(task) ) );
            return false;
        }

        return true;
    }

    bool TaskPool::deque( const bool isIdleCall, PoolTask& taskOut, TaskPriority& priorityOut)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Threading);

        priorityOut = TaskPriority::HIGH;
        if ( !dequeInternal(priorityOut, isIdleCall, taskOut) )
        {
            priorityOut = TaskPriority::DONT_CARE;
            return dequeInternal(priorityOut, isIdleCall, taskOut);
        }

        return true;
    }

    bool TaskPool::dequeInternal(const TaskPriority& priorityIn, const bool isIdleCall, PoolTask& taskOut)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if constexpr (IsBlocking)
        {
            if ( TaskPoolDetail::DequeueMayBlock( isIdleCall, priorityIn ) )
            {
                return getQueue(priorityIn).wait_dequeue_timed(taskOut, Time::MillisecondsToMicroseconds(2));
            }
        }
        return getQueue(priorityIn).try_dequeue(taskOut);
    }

    void Parallel_For( TaskPool& pool, const ParallelForDescriptor& descriptor, const DELEGATE<void, const Task*, U32/*start*/, U32/*end*/>& cbk )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( descriptor._iterCount == 0u ) [[unlikely]]
        {
            return;
        }

        // Shortcut for small loops
        if (descriptor._useCurrentThread && descriptor._iterCount < descriptor._partitionSize)
        {
            cbk(nullptr, 0u, descriptor._iterCount);
            return;
        }

        const U32 crtPartitionSize = std::min( descriptor._partitionSize, descriptor._iterCount );
        const U32 partitionCount = descriptor._iterCount / crtPartitionSize;
        const U32 remainder = descriptor._iterCount % crtPartitionSize;
        const U32 adjustedCount = descriptor._useCurrentThread ? partitionCount - 1u : partitionCount;

        std::atomic_uint jobCount = adjustedCount + (remainder > 0u ? 1u : 0u);
        for ( U32 i = 0u; i < adjustedCount; ++i )
        {
            const U32 start = i * crtPartitionSize;
            const U32 end = start + crtPartitionSize;
            Task* parallelJob = TaskPool::AllocateTask
            (
                nullptr,
                [&cbk, &jobCount, start, end]( Task& parentTask )
                {
                    cbk( &parentTask, start, end );
                    jobCount.fetch_sub( 1 );
                }
            );

            pool.enqueue( *parallelJob, descriptor._priority );
        }
        if ( remainder > 0u )
        {
            const U32 count = descriptor._iterCount;
            Task* parallelJob = TaskPool::AllocateTask
            (
                nullptr,
                [&cbk, &jobCount, count, remainder]( Task& parentTask )
                {
                    cbk( &parentTask, count - remainder, count );
                    jobCount.fetch_sub( 1 );
                }
            );

            pool.enqueue( *parallelJob, descriptor._priority );
        }

        if ( descriptor._useCurrentThread )
        {
            const U32 start = adjustedCount * crtPartitionSize;
            cbk( nullptr, start, start + crtPartitionSize );
        }

        if ( descriptor._waitForFinish )
        {
            if ( descriptor._allowPoolIdle )
            {
                while ( jobCount.load() > 0 )
                {
                    PlatformContextIdleCall();
                    if ( !pool.threadWaiting() )
                    {
                        const U32 pendingJobs = jobCount.load();
                        if ( pendingJobs > 0u )
                        {
                            WaitForSignal( pool._taskFinishedMutex, pool._taskFinishedCV, pool._waiterCount,
                                           std::chrono::milliseconds( 2 ), [&jobCount, pendingJobs]() noexcept
                            {
                                return jobCount.load() != pendingJobs;
                            } );
                        }
                    }
                }
            }
            else
            {
                WAIT_FOR_CONDITION( jobCount.load() == 0u );
            }
        }
    }
} //namespace Divide
