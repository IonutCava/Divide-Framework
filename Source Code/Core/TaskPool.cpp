#include "stdafx.h"

#include "Platform/Threading/Headers/Task.h"
#include "Headers/TaskPool.h"
#include "Core/Headers/StringHelper.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

namespace {
    constexpr I32 g_maxDequeueItems = 5;
    std::atomic_uint g_taskIDCounter = 0u;
    thread_local Task g_taskAllocator[Config::MAX_POOLED_TASKS];
    thread_local U64  g_allocatedTasks = 0u;
}

TaskPool::TaskPool() noexcept
    : GUIDWrapper(),
      _taskCallbacks(Config::MAX_POOLED_TASKS)
{
}

TaskPool::TaskPool(const U32 threadCount, const TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate, const string& workerName)
    : TaskPool()
{
    if (!init(threadCount, poolType, onThreadCreate, workerName)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

TaskPool::~TaskPool()
{
    shutdown();
}

bool TaskPool::init(const U32 threadCount, const TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate, const string& workerName) {
    if (threadCount == 0u || _blockingPool != nullptr || _lockFreePool != nullptr) {
        return false;
    }

    type(poolType);
    _threadNamePrefix = workerName;
    _threadCreateCbk = onThreadCreate;

    switch (type()) {
        case TaskPoolType::TYPE_LOCKFREE: {
            _lockFreePool = MemoryManager_NEW ThreadPool<false>(*this, threadCount);
            return true;
        }
        case TaskPoolType::TYPE_BLOCKING: {
            _blockingPool = MemoryManager_NEW ThreadPool<true>(*this, threadCount);
            return true;
        }
        case TaskPoolType::COUNT: break;
    }

    return false;
}

void TaskPool::shutdown() {
    waitForAllTasks(true);
    waitAndJoin();
    MemoryManager::SAFE_DELETE(_lockFreePool);
    MemoryManager::SAFE_DELETE(_blockingPool);
}

void TaskPool::onThreadCreate(const U32 threadIndex, const std::thread::id& threadID) {
    const string threadName = _threadNamePrefix + Util::to_string(threadIndex);
    if (USE_OPTICK_PROFILER) {
        OPTICK_START_THREAD(threadName.c_str());
    }

    SetThreadName(threadName.c_str());
    if (_threadCreateCbk) {
        _threadCreateCbk(threadID);
    }
}

void TaskPool::onThreadDestroy([[maybe_unused]] const std::thread::id& threadID) {

    if (USE_OPTICK_PROFILER) {
        OPTICK_STOP_THREAD();
    }
}

bool TaskPool::enqueue(Task& task, const TaskPriority priority, const U32 taskIndex, const DELEGATE<void>& onCompletionFunction) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    const bool isRealtime = priority == TaskPriority::REALTIME;
    const bool hasOnCompletionFunction = !isRealtime && onCompletionFunction;

    //Returing false from a PoolTask lambda will just reschedule it for later execution again. 
    //This may leave the task in an infinite loop, always re-queuing!
    const auto poolTask = [this, &task, hasOnCompletionFunction](const bool threadWaitingCall) {
        while (task._unfinishedJobs.load() > 1u) {
            if (threadWaitingCall) {
                threadWaiting();
            } else {
                // Can't be run at this time. It will be executated again later!
                return false;
            }
        }

        if (!threadWaitingCall || task._runWhileIdle) {
            if (task._callback) {
                task._callback(task);
            }

            taskCompleted(task, hasOnCompletionFunction);
            return true;
        }

        return false;
    };

    _runningTaskCount.fetch_add(1u);

    if (!isRealtime) {
        if (onCompletionFunction) {
            _taskCallbacks[taskIndex].push_back(onCompletionFunction);
        }

        return (type() == TaskPoolType::TYPE_BLOCKING)
                    ? _blockingPool->addTask(MOV(poolTask))
                    : _lockFreePool->addTask(MOV(poolTask));
    }

    if (!poolTask(false)) {
        DIVIDE_UNEXPECTED_CALL();
    }
    if (onCompletionFunction) {
        onCompletionFunction();
    }
    
    return true;
}

void TaskPool::waitForTask(const Task& task) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    using namespace std::chrono_literals;
    while (!Finished(task)) {
        threadWaiting();

        UniqueLock<Mutex> lock(_taskFinishedMutex);
        _taskFinishedCV.wait_for(lock, 5ms, [&task]() noexcept { return Finished(task); });
    }
}

size_t TaskPool::flushCallbackQueue() {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    size_t ret = 0u;

    if (_threadedCallbackBuffer.size_approx() > 0u) {
        if (USE_OPTICK_PROFILER) {
            OPTICK_EVENT();
        }

        std::array<U32, g_maxDequeueItems> taskIndex = {};
        size_t count = 0u;
        do {
            count = _threadedCallbackBuffer.try_dequeue_bulk(std::begin(taskIndex), g_maxDequeueItems);
            for (size_t i = 0u; i < count; ++i) {
                auto& cbks = _taskCallbacks[taskIndex[i]];
                for (auto& cbk : cbks) {
                    if (cbk) {
                        cbk();
                    }
                }
                cbks.resize(0);
            }
            ret += count;
        } while (count > 0u);
    }

    return ret;
}

void TaskPool::waitForAllTasks(const bool flushCallbacks) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    if (type() != TaskPoolType::COUNT) {
        {
            UniqueLock<Mutex> lock(_taskFinishedMutex);
            _taskFinishedCV.wait(lock, [this]() noexcept { return _runningTaskCount.load() == 0u; });
        }

        if (flushCallbacks) {
            flushCallbackQueue();
        }
    }
}

void TaskPool::taskCompleted(Task& task, const bool hasOnCompletionFunction) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    task._callback = {}; //<Needed to cleanup any stale resources (e.g. captured by lamdas)
    if (hasOnCompletionFunction) {
        _threadedCallbackBuffer.enqueue(task._id);
    }

    if (task._parent != nullptr) {
        if_constexpr(Config::Build::IS_DEBUG_BUILD) {
            DIVIDE_ASSERT(task._parent->_unfinishedJobs.fetch_sub(1) >= 1u);
        } else {
            task._parent->_unfinishedJobs.fetch_sub(1);
        }
    }

    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        DIVIDE_ASSERT(task._unfinishedJobs.fetch_sub(1) == 1u);
    } else {
        task._unfinishedJobs.fetch_sub(1);
    }

    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        DIVIDE_ASSERT(_runningTaskCount.fetch_sub(1) >= 1u);
    } else {
        _runningTaskCount.fetch_sub(1);
    }

    ScopedLock<Mutex> lock(_taskFinishedMutex);
    _taskFinishedCV.notify_one();
}

Task* TaskPool::AllocateTask(Task* parentTask, const bool allowedInIdle) noexcept {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    if (parentTask != nullptr) {
        parentTask->_unfinishedJobs.fetch_add(1u);
    }

    Task* task = nullptr;
    do {
        Task& crtTask = g_taskAllocator[g_allocatedTasks++ & Config::MAX_POOLED_TASKS - 1u];

        U16 expected = 0u;
        if (crtTask._unfinishedJobs.compare_exchange_strong(expected, 1u)) {
            task = &crtTask;
        }
    } while (task == nullptr);

    if (task->_id == 0u) {
        task->_id = g_taskIDCounter.fetch_add(1u);
    }
    task->_parent = parentTask;
    task->_runWhileIdle = allowedInIdle;

    return task;
}

void TaskPool::threadWaiting(const bool forceExecute) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    if (!forceExecute && Runtime::isMainThread()) {
        flushCallbackQueue();
    } else {
        if (type() == TaskPoolType::TYPE_BLOCKING) {
            _blockingPool->executeOneTask(false);
        } else {
            _lockFreePool->executeOneTask(false);
        }
    }
}

void TaskPool::waitAndJoin() const {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    if (type() == TaskPoolType::TYPE_BLOCKING) {
        assert(_blockingPool != nullptr);

        _blockingPool->wait();
        _blockingPool->join();
    } else if (type() == TaskPoolType::TYPE_LOCKFREE) {
        assert(_lockFreePool != nullptr);

        _lockFreePool->wait();
        _lockFreePool->join();
    } else {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    if (descriptor._iterCount == 0u) {
        return;
    }

    const U32 crtPartitionSize = std::min(descriptor._partitionSize, descriptor._iterCount);
    const U32 partitionCount = descriptor._iterCount / crtPartitionSize;
    const U32 remainder = descriptor._iterCount % crtPartitionSize;
    const U32 adjustedCount = descriptor._useCurrentThread ? partitionCount - 1u : partitionCount;

    std::atomic_uint jobCount = adjustedCount + (remainder > 0u ? 1u : 0u);
    const auto& cbk = descriptor._cbk;

    for (U32 i = 0u; i < adjustedCount; ++i) {
        const U32 start = i * crtPartitionSize;
        const U32 end = start + crtPartitionSize;
        Task* parallelJob = TaskPool::AllocateTask(nullptr, descriptor._allowRunInIdle);
        parallelJob->_callback = [&cbk, &jobCount, start, end](Task& parentTask) {
                                      cbk(&parentTask, start, end);
                                      jobCount.fetch_sub(1);
                                  };
  
        Start(*parallelJob, pool, descriptor._priority);
    }
    if (remainder > 0u) {
        const U32 count = descriptor._iterCount;
        Task* parallelJob = TaskPool::AllocateTask(nullptr, descriptor._allowRunInIdle);
        parallelJob->_callback = [&cbk, &jobCount, count, remainder](Task& parentTask) {
                                      cbk(&parentTask, count - remainder, count);
                                      jobCount.fetch_sub(1);
                                  };
        Start(*parallelJob, pool, descriptor._priority);
    }

    if (descriptor._useCurrentThread) {
        const U32 start = adjustedCount * crtPartitionSize;
        cbk(nullptr, start, start + crtPartitionSize);
    }

    if (descriptor._waitForFinish) {
        if (descriptor._allowPoolIdle) {
            while (jobCount.load() > 0) {
                pool.threadWaiting();
            }
        } else {
            WAIT_FOR_CONDITION(jobCount.load() == 0u);
        }
    }
}
} //namespace Divide
