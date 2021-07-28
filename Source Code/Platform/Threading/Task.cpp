#include "stdafx.h"

#include "Headers/Task.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

void Finish(Task& task) {
    if (task._unfinishedJobs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        task._callback = {};
        if (task._parent != nullptr) {
            Finish(*task._parent);
        }
    }
}

void RunLocally(Task& task, TaskPool& pool, TaskPriority priority, const bool hasOnCompletionFunction) {
    ACKNOWLEDGE_UNUSED(priority);

    while (task._unfinishedJobs.load(std::memory_order_acquire) > 1) {
        pool.threadWaiting();
    }
    if (task._callback) {
        task._callback(task);
    }

    Finish(task);
    pool.taskCompleted(task._id, hasOnCompletionFunction);
    pool.flushCallbackQueue();
};

void Start(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction) {
    const bool hasOnCompletionFunction = priority != TaskPriority::REALTIME && onCompletionFunction;
    if (!pool.enqueue(
        [&task, &pool, hasOnCompletionFunction](const bool threadWaitingCall)
        {
            while (task._unfinishedJobs.load(std::memory_order_acquire) > 1) {
                if (threadWaitingCall) {
                    pool.threadWaiting();
                } else {
                    return false;
                }
            }

            if (!threadWaitingCall || task._runWhileIdle) {
                if (task._callback) {
                    task._callback(task);
                }

                Finish(task);
                pool.taskCompleted(task._id, hasOnCompletionFunction);
                return true;
            }

            return false;
        }, 
        priority, 
        task._id,
        onCompletionFunction)) 
    {
        Console::errorfn(Locale::Get(_ID("TASK_SCHEDULE_FAIL")), 1);
        RunLocally(task, pool, priority, hasOnCompletionFunction);
    }
}

void Wait(const Task& task, TaskPool& pool) {
    if (TaskPool::USE_OPTICK_PROFILER) {
        OPTICK_EVENT();
    }

    while (!Finished(task)) {
        pool.threadWaiting();
    }
}

void StartAndWait(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction) {
    Start(task, pool, priority, onCompletionFunction);
    Wait(task, pool);
}

bool Finished(const Task& task) noexcept {
    return task._unfinishedJobs.load(std::memory_order_acquire) == 0;
}
};