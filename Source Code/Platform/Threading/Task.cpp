#include "stdafx.h"

#include "Headers/Task.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

void Start(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction) {
    OPTICK_EVENT();

    if (!pool.enqueue(task, priority, task._id, onCompletionFunction)) {
        Console::errorfn(Locale::Get(_ID("TASK_SCHEDULE_FAIL")), 1);
        Start(task, pool, TaskPriority::REALTIME, onCompletionFunction);
    }
}

void Wait(const Task& task, TaskPool& pool) {
    OPTICK_EVENT();

    pool.waitForTask(task);
}

void StartAndWait(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction) {
    Start(task, pool, priority, onCompletionFunction);
    Wait(task, pool);
}

bool Finished(const Task& task) noexcept {
    return task._unfinishedJobs.load() == 0u;
}

}; //namespace Divide
