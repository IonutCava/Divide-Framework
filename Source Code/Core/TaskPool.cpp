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

TaskPool::TaskPool()
    : GUIDWrapper(),
      _taskCallbacks(Config::MAX_POOLED_TASKS)
{
}

TaskPool::TaskPool(const U32 threadCount, const TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate, const stringImpl& workerName)
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

bool TaskPool::init(const U32 threadCount, const TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate, const stringImpl& workerName) {
    if (threadCount == 0 || _poolImpl.init()) {
        return false;
    }

    _threadNamePrefix = workerName;
    _threadCreateCbk = onThreadCreate;
    _workerThreadCount = threadCount;

    switch (poolType) {
        case TaskPoolType::TYPE_LOCKFREE: {
            std::get<1>(_poolImpl._poolImpl) = MemoryManager_NEW ThreadPool<false>(*this, _workerThreadCount);
            return true;
        }
        case TaskPoolType::TYPE_BLOCKING: {
            std::get<0>(_poolImpl._poolImpl) = MemoryManager_NEW ThreadPool<true>(*this, _workerThreadCount);
            return true;
        }
        case TaskPoolType::COUNT: break;
    }

    return false;
}

void TaskPool::shutdown() {
    waitForAllTasks(true);
    if (_poolImpl.init()) {
        MemoryManager::SAFE_DELETE(std::get<0>(_poolImpl._poolImpl));
        MemoryManager::SAFE_DELETE(std::get<1>(_poolImpl._poolImpl));
    }
}

void TaskPool::onThreadCreate(const std::thread::id& threadID) {
    const stringImpl threadName = _threadNamePrefix + Util::to_string(_threadCount.fetch_add(1));
    if (USE_OPTICK_PROFILER) {
        OPTICK_START_THREAD(threadName.c_str());
    }

    SetThreadName(threadName.c_str());
    if (_threadCreateCbk) {
        _threadCreateCbk(threadID);
    }
}

void TaskPool::onThreadDestroy(const std::thread::id& threadID) {
    ACKNOWLEDGE_UNUSED(threadID);

    if (USE_OPTICK_PROFILER) {
        OPTICK_STOP_THREAD();
    }
}

bool TaskPool::enqueue(Task& task, const TaskPriority priority, const U32 taskIndex, const DELEGATE<void>& onCompletionFunction) {
    const bool hasOnCompletionFunction = priority != TaskPriority::REALTIME && onCompletionFunction;

    //Returing false from a PoolTask lambda will just reschedule it for later execution again. 
    //This may leave the task in an infinite loop, always re-queuing!
    auto poolTask = [this, &task, hasOnCompletionFunction](const bool threadWaitingCall) {
        while (task._unfinishedJobs.load() > 1) {
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

    _runningTaskCount.fetch_add(1);

    if (priority == TaskPriority::REALTIME) {
        if (!poolTask(false)) {
            DIVIDE_UNEXPECTED_CALL();
        }
        if (onCompletionFunction) {
            onCompletionFunction();
        }
        return true;
    }

    if (onCompletionFunction) {
        _taskCallbacks[taskIndex].push_back(onCompletionFunction);
    }

    return _poolImpl.addTask(MOV(poolTask));
}

void TaskPool::waitForTask(const Task& task) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    UniqueLock<Mutex> lock(_taskFinishedMutex);
    _taskFinishedCV.wait(lock, [&task]() noexcept { return Finished(task); });
}

size_t TaskPool::flushCallbackQueue() {
    if (USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    size_t ret = 0u, count = 0u;
    std::array<U32, g_maxDequeueItems> taskIndex = {};
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

    return ret;
}

void TaskPool::waitForAllTasks(const bool flushCallbacks) {
    if (!_poolImpl.init()) {
        return;
    }

    if (_workerThreadCount > 0u) {
        UniqueLock<Mutex> lock(_taskFinishedMutex);
        _taskFinishedCV.wait(lock, [this]() { return _runningTaskCount.load() == 0u; });
    }

    if (flushCallbacks) {
        flushCallbackQueue();
    }

    _poolImpl.waitAndJoin();
}

void TaskPool::taskCompleted(Task& task, const bool hasOnCompletionFunction) {
    if (hasOnCompletionFunction) {
        _threadedCallbackBuffer.enqueue(task._id);
    }

    const U32 jobCount = task._unfinishedJobs.fetch_sub(1);
    assert(jobCount == 1u);

    if (task._parent != nullptr) {
        const U32 parentJobCount = task._parent->_unfinishedJobs.fetch_sub(1);
        assert(parentJobCount >= 1u);
    }

    const U32 test = _runningTaskCount.fetch_sub(1);
    assert(test >= 1u);

    ScopedLock<Mutex> lock(_taskFinishedMutex);
    _taskFinishedCV.notify_one();
}

Task* TaskPool::AllocateTask(Task* parentTask, const bool allowedInIdle) {
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

    if (task->_id == 0) {
        task->_id = g_taskIDCounter.fetch_add(1u);
    }
    task->_parent = parentTask;
    task->_runWhileIdle = allowedInIdle;

    return task;
}

void TaskPool::threadWaiting(const bool forceExecute) {
    if (!forceExecute && Runtime::isMainThread()) {
        flushCallbackQueue();
        return;
    }

    _poolImpl.threadWaiting();
}

void WaitForAllTasks(TaskPool& pool, const bool flushCallbacks) {
    pool.waitForAllTasks(flushCallbacks);
}

bool TaskPool::PoolHolder::init() const noexcept {
    return _poolImpl.first != nullptr || 
           _poolImpl.second != nullptr;
}

void TaskPool::PoolHolder::waitAndJoin() const {
    if (_poolImpl.first != nullptr) {
        _poolImpl.first->wait();
        _poolImpl.first->join();
        return;
    }

    _poolImpl.second->wait();
    _poolImpl.second->join();
}

void TaskPool::PoolHolder::threadWaiting() const {
    if (_poolImpl.first != nullptr) {
        _poolImpl.first->executeOneTask(false);
    } else {
        _poolImpl.second->executeOneTask(false);
    }
}

bool TaskPool::PoolHolder::addTask(PoolTask&& job) const {
    if (_poolImpl.first != nullptr) {
        return _poolImpl.first->addTask(MOV(job));
    }

    return _poolImpl.second->addTask(MOV(job));
}

void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor) {
    if (descriptor._iterCount == 0) {
        return;
    }

    const U32 crtPartitionSize = std::min(descriptor._partitionSize, descriptor._iterCount);
    const U32 partitionCount = descriptor._iterCount / crtPartitionSize;
    const U32 remainder = descriptor._iterCount % crtPartitionSize;
    const U32 adjustedCount = descriptor._useCurrentThread ? partitionCount - 1 : partitionCount;

    std::atomic_uint jobCount = adjustedCount + (remainder > 0 ? 1 : 0);
    const auto& cbk = descriptor._cbk;

    for (U32 i = 0; i < adjustedCount; ++i) {
        const U32 start = i * crtPartitionSize;
        const U32 end = start + crtPartitionSize;
        Task* parallelJob = TaskPool::AllocateTask(nullptr, descriptor._allowRunInIdle);
        parallelJob->_callback = [&cbk, &jobCount, start, end](Task& parentTask) {
                                      cbk(&parentTask, start, end);
                                      jobCount.fetch_sub(1);
                                  };
  
        Start(*parallelJob, pool, descriptor._priority);
    }
    if (remainder > 0) {
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
