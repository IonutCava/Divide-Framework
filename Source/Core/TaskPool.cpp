

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
    }

    TaskPool::~TaskPool()
    {
        DIVIDE_ASSERT( _activeThreads.load() == 0u, "Task pool is still active! Threads should be joined before destroying the pool. Call TaskPool::shutdown() first");
    }

    bool TaskPool::init( const size_t threadCount, const DELEGATE<void, const std::thread::id&>& onThreadCreateCbk)
    {
        shutdown();
        if (threadCount == 0u)
        {
            return false;
        }

        _isRunning = true;
        _threadCreateCbk = onThreadCreateCbk;
        _threads.reserve( threadCount );
        _activeThreads.store(threadCount);

        for ( U32 idx = 0u; idx < threadCount; ++idx )
        {
            _threads.emplace_back(
                [&, idx]
                {
                    const auto threadName = Util::StringFormat( "{}_{}", _threadNamePrefix, idx );

                    Profiler::OnThreadStart( threadName );

                    SetThreadName( threadName );

                    if ( _threadCreateCbk )
                    {
                        _threadCreateCbk( std::this_thread::get_id() );
                    }

                    while ( _isRunning )
                    {
                        executeOneTask( false );
                    }

                    Profiler::OnThreadStop();

                    _activeThreads.fetch_sub( 1u );
                } );
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

    bool TaskPool::enqueue( Task& task, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );
        if (priority == TaskPriority::REALTIME)
        {
            return runRealTime( task, onCompletionFunction);
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
                    entry._cbk = onCompletionFunction;
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                _taskCallbacks.emplace_back( onCompletionFunction, task._id );
            }
        }

        //Returning false from a PoolTask lambda will just reschedule it for later execution again. 
        //This may leave the task in an infinite loop, always re-queuing!
        const auto poolTask = [this, &task, hasOnCompletionFunction]( const bool isIdleCall )
        {
            while ( task._unfinishedJobs.load() > 1u )
            {
                if ( isIdleCall )
                {
                    // Can't be run at this time. It will be executed again later!
                    return false;
                }

                // Else, we wait until our child tasks finish running. We also try and do some other work while waiting
                threadWaiting();
            }

            // Can't run this task at the current moment. We're in an idle loop and the task needs express execution (e.g. render pass task)
            if ( !task._runWhileIdle && isIdleCall )
            {
                return false;
            }

            taskStarted( task );

            if ( task._callback ) [[likely]]
            {
                task._callback( task );
            }
            if ( hasOnCompletionFunction )
            {
                _threadedCallbackBuffer.enqueue( task._id );
            }

            taskCompleted( task );

            return true;
        };

        return addTask( MOV( poolTask ) );
    }

    bool TaskPool::runRealTime( Task& task, const DELEGATE<void>& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        while ( task._unfinishedJobs.load() > 1u )
        {
            if ( flushCallbackQueue() == 0u)
            {
                threadWaiting();
            }
        }

        taskStarted( task );

        if ( task._callback ) [[likely]]
        {
            task._callback( task );
        }

        taskCompleted( task );

        if ( onCompletionFunction )
        {
            onCompletionFunction();
        }

        return true;
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
            const size_t callbackCount = _taskCallbacks.size();
            DIVIDE_ASSERT( callbackCount > 0u );

            for ( size_t i = 0u; i < count; ++i )
            {
                const U32 idx = completedTaskIndices[i];
                for ( size_t j = 0u; j < callbackCount; ++j )
                {
                    CallbackEntry& entry = _taskCallbacks[j];
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

    void TaskPool::taskStarted( [[maybe_unused]] Task& task )
    {
        _runningTaskCount.fetch_add( 1 );
    }

    void TaskPool::taskCompleted( Task& task )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        task._callback = {}; ///<Needed to cleanup any stale resources (e.g. captured by lambdas)

        if ( task._parent != nullptr )
        {
            task._parent->_unfinishedJobs.fetch_sub(1);
        }

        task._unfinishedJobs.fetch_sub(1);

        _runningTaskCount.fetch_sub(1);

        LockGuard<Mutex> lock( _taskFinishedMutex );
        _taskFinishedCV.notify_one();
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
                if ( idx == 0u && ++retryCount > s_maxTaskRetry )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
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
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );
        executeOneTask( true );
    }

    void TaskPool::join()
    {
        _isRunning = false;

        for ( std::thread& thread : _threads )
        {
            if (!thread.joinable())
            {
                continue;
            }

            thread.join();
        }

        WAIT_FOR_CONDITION( _activeThreads.load() == 0u );
    }

    void TaskPool::wait() const noexcept
    {
        if ( !_isRunning )
        {
            return;
        }

        while ( _runningTaskCount.load() > 0u )
        {
            // Busy wait
            std::this_thread::yield();
        }
    }

    bool TaskPool::addTask( PoolTask&& job )
    {
        return _queue.enqueue( MOV( job ) );
    }

    void TaskPool::executeOneTask( const bool isIdleCall )
    {
        PoolTask task = {};
        if ( deque( isIdleCall, task ) &&
             !task( isIdleCall ) )
        {
            addTask( MOV( task ) );
        }
    }

    bool TaskPool::deque( const bool isIdleCall, PoolTask& taskOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( isIdleCall )
        {
            return _queue.try_dequeue( taskOut );
        }

        if constexpr ( IsBlocking )
        {
            while( !_queue.wait_dequeue_timed( taskOut, Time::MillisecondsToMicroseconds( 2 ) ))
            {
                if (!_isRunning) [[unlikely]]
                {
                    return false;
                }
                std::this_thread::yield();
            }
        }
        else
        {
            while ( !_queue.try_dequeue( taskOut ) )
            {
                if ( !_isRunning ) [[unlikely]]
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
