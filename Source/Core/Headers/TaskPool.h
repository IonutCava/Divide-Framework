/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_TASK_POOL_H_
#define DVD_TASK_POOL_H_

#include "Platform/Threading/Headers/Task.h"

namespace Divide {

struct ParallelForDescriptor {
    DELEGATE<void, const Task*, U32/*start*/, U32/*end*/> _cbk{};
    /// For loop iteration count
    U32 _iterCount = 0u;
    /// How many elements should we process per async task
    U32 _partitionSize = 0u;
    /// Each async task will start with the same priority specified here
    TaskPriority _priority = TaskPriority::DONT_CARE;
    /// If this is false, the parallel_for call won't block the current thread
    bool _waitForFinish = true;
    /// If true, we'll process a for partition on the calling thread
    bool _useCurrentThread = true;
    /// If true, we'll inform the thread pool to execute other tasks while waiting for the all async tasks to finish
    bool _allowPoolIdle = true;
    /// If true, async tasks can be invoked from other task's idle callbacks
    bool _allowRunInIdle = true;
};

using PoolTask = DELEGATE_STD<bool, bool/*threadWaitingCall*/>;

class TaskPool final : public GUIDWrapper {
public:
  public:

    constexpr static bool IsBlocking = true;

    explicit TaskPool(std::string_view workerName);
    ~TaskPool();

    bool init(U32 threadCount, const DELEGATE<void, const std::thread::id&>& onThreadCreate = {});
    void shutdown();

    static Task* AllocateTask(Task* parentTask, DELEGATE<void, Task&>&& func, bool allowedInIdle ) noexcept;

    /// Returns the number of callbacks processed
    size_t flushCallbackQueue();
    void waitForAllTasks(bool flushCallbacks);

    /// Called by a task that isn't doing anything (e.g. waiting on child tasks).
    /// Use this to run another task (if any) and return to the previous execution point
    void threadWaiting();


    // Add a new task to the pool's queue
    bool addTask( PoolTask&& job );

    // Reinitializes the thread pool (joins and closes out all existing threads first)
    void init();
    // Join all of the threads and block until all running tasks have completed.
    void join();

    // Wait for all running jobs to finish
    void wait() const noexcept;

    void executeOneTask( bool waitForTask );

    static void PrintLine( std::string_view );

    PROPERTY_R(U32, workerThreadCount, 0u);
    PROPERTY_R( vector<std::thread>, threads );


  private:
    //ToDo: replace all friend class declarations with attorneys -Ionut;
    friend struct Task;
    friend void Wait(const Task& task, TaskPool& pool);

    friend void Start(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction);
    friend void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor);

    void taskCompleted(Task& task, bool hasOnCompletionFunction);
    
    bool enqueue(Task& task, TaskPriority priority, U32 taskIndex, const DELEGATE<void>& onCompletionFunction);
    bool deque( bool waitForTask, PoolTask& taskOut );
    void waitForTask(const Task& task);

  private:
     static Mutex s_printLock;
     const string _threadNamePrefix;

     using QueueType = std::conditional_t<IsBlocking, moodycamel::BlockingConcurrentQueue<PoolTask>, moodycamel::ConcurrentQueue<PoolTask>>;
     QueueType _queue;

     hashMap<U32, DELEGATE<void>> _taskCallbacks{};

     Mutex _taskFinishedMutex;
     std::condition_variable _taskFinishedCV;
     DELEGATE<void, const std::thread::id&> _threadCreateCbk{};
     moodycamel::ConcurrentQueue<U32> _threadedCallbackBuffer{};

     std::atomic_uint _runningTaskCount = 0u;
     std::atomic_uint _activeThreads{ 0u };
     std::atomic_uint _tasksLeft{ 0 };
     bool _isRunning{ true };
};

template<class Predicate>
Task* CreateTask(Predicate&& threadedFunction, bool allowedInIdle = true);

template<class Predicate>
Task* CreateTask(Task* parentTask, Predicate&& threadedFunction, bool allowedInIdle = true);

void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor);

} //namespace Divide

#endif //DVD_TASK_POOL_H_

#include "TaskPool.inl"
