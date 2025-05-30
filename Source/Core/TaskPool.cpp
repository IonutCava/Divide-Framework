

#include "Platform/Threading/Headers/Task.h"
#include "Headers/TaskPool.h"
#include "Core/Headers/StringHelper.h"
#include "Platform/Headers/PlatformRuntime.h"

#include <iostream>

namespace Divide
{
    namespace
    {
        std::atomic_uint g_taskIDCounter = 0u;

        NO_DESTROY thread_local Task g_taskAllocator[Config::MAX_POOLED_TASKS];
        thread_local U64  g_allocatedTasks = 0u;
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
        _threadCreateCbk = onThreadCreateCbk;
        _threads.reserve( threadCount );
        _activeThreads.store(threadCount);

        for (size_t idx = 0u; idx < threadCount; ++idx )
        {
            _threads.emplace_back
            (
                [&, idx]
                {
                    const auto threadName = Util::StringFormat( "{}_{}", _threadNamePrefix, idx );

                    Profiler::OnThreadStart( threadName );

                    SetThreadName( threadName );

                    if ( _threadCreateCbk )
                    {
                        _threadCreateCbk( idx, std::this_thread::get_id() );
                        _threadCreateCbk = {};
                    }

                    while ( _isRunning.load() )
                    {
                        executeOneTask( false );
                    }

                    Profiler::OnThreadStop();

                    _activeThreads.fetch_sub( 1u );
                }
            );
        }

        return true;
    }

    void TaskPool::shutdown()
    {
        wait();
        join();
        waitForAllTasks( true );
        efficient_clear( _threads );
        _taskCallbacks.resize(0);
        _threadCreateCbk = {};
    }

    void TaskPool::enqueue( Task& task, const TaskPriority priority, DELEGATE<void>&& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if (priority == TaskPriority::REALTIME ) [[unlikely]]
        {
            while (task._unfinishedJobs.load() > 1u)
            {
                if (flushCallbackQueue() == 0u)
                {
                    threadWaiting();
                }
            }

            runTask(task);

            if (onCompletionFunction)
            {
                onCompletionFunction();
            }

            return;
        }

        bool hasOnCompletionFunction = false;
        if ( onCompletionFunction )
        {
            hasOnCompletionFunction = true;
            LockGuard<SharedMutex> w_lock( _taskCallbacksLock );
            bool found = false;
            for ( CallbackEntry& entry : _taskCallbacks )
            {
                if ( entry._taskID == U32_MAX)
                {
                    entry._taskID = task._id;
                    entry._cbk = MOV(onCompletionFunction);
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                _taskCallbacks.emplace_back( MOV(onCompletionFunction), task._id );
            }
        }

        DIVIDE_EXPECTED_CALL
        (
            getQueue(priority).enqueue(
                // Returning false from a PoolTask lambda will just reschedule it for later execution again. 
                // This may leave the task in an infinite loop, always re-queuing!
                [this, &task, hasOnCompletionFunction](const bool isIdleCall)
                {
                    while (task._unfinishedJobs.load() > 1u)
                    {
                        if (isIdleCall)
                        {
                            // Can't be run at this time as we'll just recurse to infinity
                            return false;
                        }

                        // Else, we wait until our child tasks finish running. We also try and do some other work while waiting
                        threadWaiting();
                    }

                    if (!task._runWhileIdle && isIdleCall)
                    {
                        return false;
                    }

                    runTask(task);

                    if (hasOnCompletionFunction)
                    {
                        _threadedCallbackBuffer.enqueue(task._id);
                    }

                    return true;
                }
            )
        );
    }

    void TaskPool::runTask( Task& task )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        _runningTaskCount.fetch_add(1);

        if ( task._callback ) [[likely]]
        {
            task._callback( task );
            task._callback = {}; //< Needed to cleanup any stale resources (e.g. captured by lambdas)
        }

        if (task._parent != nullptr)
        {
            task._parent->_unfinishedJobs.fetch_sub(1);
        }

        task._unfinishedJobs.fetch_sub(1);
        _runningTaskCount.fetch_sub(1);

        LockGuard<Mutex> lock(_taskFinishedMutex);
        _taskFinishedCV.notify_one();
    }

    void TaskPool::waitForTask( const Task& task )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        using namespace std::chrono_literals;
        while ( !Finished( task ) )
        {
            threadWaiting();

            UniqueLock<Mutex> lock( _taskFinishedMutex );
            _taskFinishedCV.wait_for( lock, 2ms, [&task]() noexcept
                                      {
                                          return Finished( task );
                                      } );
        }
    }

    size_t TaskPool::flushCallbackQueue()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        DIVIDE_ASSERT( Runtime::isMainThread() );

        constexpr I32 maxDequeueItems = 1 << 3;
        U32 completedTaskIndices[maxDequeueItems];

        size_t ret = 0u;
        while ( true )
        {
            const size_t count = _threadedCallbackBuffer.try_dequeue_bulk( completedTaskIndices, maxDequeueItems );
            if ( count == 0u )
            {
                break;
            }

            LockGuard<SharedMutex> w_lock( _taskCallbacksLock );
            for ( size_t i = 0u; i < count; ++i )
            {
                const U32 idx = completedTaskIndices[i];
                for (CallbackEntry& entry : _taskCallbacks)
                {
                    if ( entry._taskID == idx)
                    {
                        if ( entry._cbk )
                        {
                            entry._cbk();
                        }
                        entry = {};
                        break;
                    }
                }
            }

            ret += count;
        }

        return ret;
    }

    void TaskPool::waitForAllTasks( const bool flushCallbacks )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );
        DIVIDE_ASSERT(Runtime::isMainThread());
        
        if ( _activeThreads.load() > 0u )
        {
            UniqueLock<Mutex> lock( _taskFinishedMutex );
            _taskFinishedCV.wait( lock, [this]() noexcept
                                    {
                                        return _runningTaskCount.load() == 0u;
                                    } );
        }

        if ( flushCallbacks )
        {
            flushCallbackQueue();
        }
    }

    Task* TaskPool::AllocateTask( Task* parentTask, DELEGATE<void, Task&>&& func, const bool allowedInIdle ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( parentTask != nullptr )
        {
            parentTask->_unfinishedJobs.fetch_add( 1u );
        }

        constexpr U8 s_maxTaskRetry = 10u;

        Task* task = nullptr;
        U8 retryCount = 0u;
        do
        {
            U16 expected = 0u;
            if constexpr ( false )
            {
                const auto idx = g_allocatedTasks++ & Config::MAX_POOLED_TASKS - 1u;
                Task& crtTask = g_taskAllocator[idx];
                DIVIDE_EXPECTED_CALL( idx != 0u || ++retryCount <= s_maxTaskRetry );

                if ( crtTask._unfinishedJobs.compare_exchange_strong( expected, 1u ) )
                {
                    task = &crtTask;
                }
            }
            else
            {
                Task& crtTask = g_taskAllocator[g_allocatedTasks++ & Config::MAX_POOLED_TASKS - 1u];
                if ( crtTask._unfinishedJobs.compare_exchange_strong( expected, 1u ) )
                {
                    task = &crtTask;
                }
            }
        }
        while ( task == nullptr );

        if ( task->_id == 0u )
        {
            task->_id = g_taskIDCounter.fetch_add( 1u );
        }
        task->_parent = parentTask;
        task->_runWhileIdle = allowedInIdle;
        task->_callback = MOV( func );

        return task;
    }

    void TaskPool::threadWaiting()
    {
        executeOneTask( true );
    }

    void TaskPool::join()
    {
        _isRunning.store(false);

        for ( std::thread& thread : _threads )
        {
            if (thread.joinable())
            {
                thread.join();
            }

        }

        WAIT_FOR_CONDITION( _activeThreads.load() == 0u );
    }

    void TaskPool::wait() const noexcept
    {
        if ( _isRunning.load() )
        {
            while ( _runningTaskCount.load() > 0u )
            {
                // Busy wait
                std::this_thread::yield();
            }
        }
    }

    void TaskPool::executeOneTask( const bool isIdleCall )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Threading);

        PoolTask task = {};
        TaskPriority priorityOut = TaskPriority::DONT_CARE;

        if ( deque( isIdleCall, task, priorityOut) && 
             !task( isIdleCall ) )
        {
            DIVIDE_EXPECTED_CALL( getQueue(priorityOut).enqueue( task ) );
        }
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

    bool TaskPool::dequeInternal(const TaskPriority& priorityIn, bool isIdleCall, PoolTask& taskOut)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( isIdleCall || priorityIn == TaskPriority::HIGH )
        {
            return getQueue(priorityIn).try_dequeue( taskOut );
        }

        if constexpr ( IsBlocking )
        {
            while( !getQueue(priorityIn).wait_dequeue_timed( taskOut, Time::MillisecondsToMicroseconds( 2 ) ))
            {
                if (!_isRunning.load()) [[unlikely]]
                {
                    return false;
                }
                std::this_thread::yield();
            }
        }
        else
        {
            while ( !getQueue(priorityIn).try_dequeue( taskOut ) )
            {
                if ( !_isRunning.load() ) [[unlikely]]
                {
                    return false;
                }
                std::this_thread::yield();
            }
        }

        return true;
    }

    void Parallel_For( TaskPool& pool, const ParallelForDescriptor& descriptor, const DELEGATE<void, const Task*, U32/*start*/, U32/*end*/>& cbk )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( descriptor._iterCount == 0u ) [[unlikely]]
        {
            return;
        }

        // Shortcut for small looops
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
                },
                descriptor._allowRunInIdle
            );

            Start( *parallelJob, pool, descriptor._priority );
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
                },
                descriptor._allowRunInIdle
            );

            Start( *parallelJob, pool, descriptor._priority );
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
                    pool.threadWaiting();
                }
            }
            else
            {
                WAIT_FOR_CONDITION( jobCount.load() == 0u );
            }
        }
    }
} //namespace Divide
