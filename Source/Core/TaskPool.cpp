

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
        thread_local U32 g_allocatedTasks = 0u;
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

                    _activeThreads.fetch_add( 1u ) ;

                    SetThreadName( threadName );

                    if (onThreadCreateCbk)
                    {
                        onThreadCreateCbk( idx, std::this_thread::get_id() );
                    }

                    while ( _isRunning.load() )
                    {
                        if ( !executeOneTask( false ) )
                        {
                            std::this_thread::yield();
                        }
                    }

                    _activeThreads.fetch_sub( 1u );
                    Profiler::OnThreadStop();
                }
            );
        }

        WAIT_FOR_CONDITION(_activeThreads.load() == threadCount);
        
        return true;
    }

    void TaskPool::shutdown()
    {
        wait();
        join();
        waitForAllTasks( true );
        efficient_clear( _threads );
        _taskCallbacks.resize(0);
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

            runTask(task, false);

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
                    entry._taskID = task._globalId;
                    entry._cbk = MOV(onCompletionFunction);
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                _taskCallbacks.emplace_back( MOV(onCompletionFunction), task._globalId );
            }
        }
        // Returning false from a PoolTask lambda will just reschedule it for later execution again. 
        // This may leave the task in an infinite loop, always re-queuing!
        auto poolTask = [this, &task, priority, hasOnCompletionFunction](const bool isIdleCall)
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
                    std::this_thread::yield();
                }
            }

            if (priority == TaskPriority::DONT_CARE_NO_IDLE && isIdleCall)
            {
                return false;
            }

            runTask(task, hasOnCompletionFunction);

            return true;
        };

        DIVIDE_EXPECTED_CALL( getQueue(priority).enqueue(MOV(poolTask)) );
    }

    void TaskPool::runTask( Task& task, const bool hasCompletionCallback)
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

        if (hasCompletionCallback)
        {
            _threadedCallbackBuffer.enqueue(task._globalId);
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

    Task* TaskPool::AllocateTask( Task* parentTask, DELEGATE<void, Task&>&& func ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( parentTask != nullptr )
        {
            parentTask->_unfinishedJobs.fetch_add( 1u );
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
            DIVIDE_EXPECTED_CALL( getQueue(priorityOut).enqueue( task ) );
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

        if ( isIdleCall || priorityIn == TaskPriority::HIGH )
        {
            if ( getQueue(priorityIn).try_dequeue( taskOut ) )
            {
                return true;
            }

            if (priorityIn == TaskPriority::HIGH)
            {
                return false;
            }
        }

        if constexpr (IsBlocking)
        {
            return getQueue(priorityIn).wait_dequeue_timed(taskOut, Time::MillisecondsToMicroseconds(2));
        }
        else
        {
            return getQueue(priorityIn).try_dequeue(taskOut);
        }
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
                }
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
