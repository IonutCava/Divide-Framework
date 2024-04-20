

#include "Platform/Threading/Headers/Task.h"
#include "Headers/TaskPool.h"
#include "Core/Headers/StringHelper.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide
{

    namespace
    {
        constexpr I32 g_maxDequeueItems = 5;
        std::atomic_uint g_taskIDCounter = 0u;
        NO_DESTROY thread_local Task g_taskAllocator[Config::MAX_POOLED_TASKS];
        thread_local U64  g_allocatedTasks = 0u;

        std::array<U32, g_maxDequeueItems> g_completedTaskIndices{};
    }

    bool TaskPool::init( const U32 threadCount, const DELEGATE<void, const std::thread::id&>& onThreadCreate, const string& workerName )
    {
        if ( threadCount == 0u || _threadPool != nullptr )
        {
            return false;
        }

        _threadNamePrefix = workerName;
        _threadCreateCbk = onThreadCreate;
        _threadPool = MemoryManager_NEW ThreadPool( *this, threadCount );
        return true;
    }

    void TaskPool::shutdown()
    {
        if ( _threadPool != nullptr )
        {
            waitForAllTasks( true );
            waitAndJoin();
            MemoryManager::SAFE_DELETE( _threadPool );
        }
    }

    void TaskPool::onThreadCreate( const U32 threadIndex, const std::thread::id& threadID )
    {
        const string threadName = _threadNamePrefix + Util::to_string( threadIndex );

        Profiler::OnThreadStart( threadName );
        SetThreadName( threadName );
        if ( _threadCreateCbk )
        {
            _threadCreateCbk( threadID );
        }
    }

    void TaskPool::onThreadDestroy( [[maybe_unused]] const std::thread::id& threadID )
    {
        Profiler::OnThreadStop();
    }

    bool TaskPool::enqueue( Task& task, const TaskPriority priority, const U32 taskIndex, const DELEGATE<void>& onCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        const bool isRealtime = priority == TaskPriority::REALTIME;
        const bool hasOnCompletionFunction = !isRealtime && onCompletionFunction;

        //Returning false from a PoolTask lambda will just reschedule it for later execution again. 
        //This may leave the task in an infinite loop, always re-queuing!
        const auto poolTask = [this, &task, hasOnCompletionFunction]( const bool threadWaitingCall )
        {
            while ( task._unfinishedJobs.load() > 1u )
            {
                if ( threadWaitingCall )
                {
                    // Can't be run at this time. It will be executed again later!
                    return false;
                }

                threadWaiting();
            }

            if ( !threadWaitingCall || task._runWhileIdle )
            {
                if ( task._callback ) [[likely]]
                {
                    task._callback( task );
                }

                taskCompleted( task, hasOnCompletionFunction );
                return true;
            }

            return false;
        };

        _runningTaskCount.fetch_add( 1u );

        if ( !isRealtime ) [[likely]]
        {
            if ( onCompletionFunction )
            {
                _taskCallbacks[taskIndex] = onCompletionFunction;
            }

            return _threadPool->addTask( MOV( poolTask ) );
        }

        if ( !poolTask( false ) ) [[unlikely]]
        {
            DIVIDE_UNEXPECTED_CALL();
        }
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

        size_t ret = 0u;
        size_t count = 0u;
        do
        {
            count = _threadedCallbackBuffer.try_dequeue_bulk( std::begin( g_completedTaskIndices ), g_maxDequeueItems );
            for ( size_t i = 0u; i < count; ++i )
            {
                auto& cbk = _taskCallbacks[g_completedTaskIndices[i]];
                if ( cbk ) [[likely]]
                {
                    cbk();
                    cbk = {};
                }
            }
            ret += count;
        }
        while ( count > 0u );

        return ret;
    }

    void TaskPool::waitForAllTasks( const bool flushCallbacks )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );
        DIVIDE_ASSERT(Runtime::isMainThread());
        
        if ( _threadPool != nullptr ) [[likely]]
        {
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
    }

    void TaskPool::taskCompleted( Task& task, const bool hasOnCompletionFunction )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        task._callback = {}; ///<Needed to cleanup any stale resources (e.g. captured by lambdas)
        if ( hasOnCompletionFunction )
        {
            _threadedCallbackBuffer.enqueue( task._id );
        }

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

        _threadPool->executeOneTask( false );
    }

    void TaskPool::waitAndJoin() const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        assert( _threadPool != nullptr );

        _threadPool->wait();
        _threadPool->join();
    }

    void parallel_for( TaskPool& pool, const ParallelForDescriptor& descriptor )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Threading );

        if ( descriptor._iterCount == 0u ) [[unlikely]]
        {
            return;
        }

        const U32 crtPartitionSize = std::min( descriptor._partitionSize, descriptor._iterCount );
        const U32 partitionCount = descriptor._iterCount / crtPartitionSize;
        const U32 remainder = descriptor._iterCount % crtPartitionSize;
        const U32 adjustedCount = descriptor._useCurrentThread ? partitionCount - 1u : partitionCount;

        std::atomic_uint jobCount = adjustedCount + (remainder > 0u ? 1u : 0u);
        const auto& cbk = descriptor._cbk;

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
