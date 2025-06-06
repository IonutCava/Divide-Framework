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

struct ParallelForDescriptor
{
    /// For loop iteration count
    U32 _iterCount = 0u;
    /// How many elements should we process per async task
    U32 _partitionSize = 0u;
    /// Each async task will start with the same priority specified here
    TaskPriority _priority = TaskPriority::DONT_CARE;
    /// If this is false, the Parallel_For call won't block the current thread
    bool _waitForFinish = true;
    /// If true, we'll process a for partition on the calling thread
    bool _useCurrentThread = true;
    /// If true, we'll inform the thread pool to execute other tasks while waiting for the all async tasks to finish
    bool _allowPoolIdle = true;
};

using PoolTask = DELEGATE_STD<bool, bool/*threadWaitingCall*/>;

class TaskPool final : public GUIDWrapper {
  public:
     constexpr static bool IsBlocking = true;
     using QueueType = std::conditional_t<IsBlocking, moodycamel::BlockingConcurrentQueue<PoolTask>, moodycamel::ConcurrentQueue<PoolTask>>;
 
  public:
    explicit TaskPool(std::string_view workerName);
    ~TaskPool();

  public:
    /// <summary>
    /// Allocates a new Task and assigns it a delegate function to execute. A parent task can be optionally specified, which will be used to track dependencies between tasks.
    /// </summary>
    /// <param name="parentTask">A pointer to the parent Task, or nullptr if there is no parent.</param>
    /// <param name="func">A delegate representing the function to be executed by the new Task. The delegate takes a reference to the Task as its parameter.</param>
    /// <returns>A pointer to the newly allocated Task.</returns>
    static Task* AllocateTask(Task* parentTask, DELEGATE<void, Task&>&& func ) noexcept;

    /// <summary>
    /// Initializes the thread pool with a specified number of threads (must be >0) and an optional callback for thread creation.
    /// If the pool is already initialized, this function will shutdown existing threads cleanly (finishing off any pending tasks) and recreate them.
    /// </summary>
    /// <param name="threadCount">The number of threads to create and initialize.</param>
    /// <param name="onThreadCreate">An optional delegate that is called when a thread is created. Receives the thread index and the thread's ID as arguments.</param>
    /// <returns>True if initialization succeeds; otherwise, false.</returns>
    bool init(size_t threadCount, DELEGATE<void, size_t, const std::thread::id&>&& onThreadCreate = {});

    /// <summary>
    /// Shuts down the thread pool, waiting for all tasks to finish and cleaning up resources.
    /// </summary>
    void shutdown();


    /// <summary>
    /// Flushes the callback queue, executing all pending callbacks. Must be called from the main thread.
    /// </summary>
    /// <returns>Returns the number of callbacks processed</returns>
    size_t flushCallbackQueue();

    /// <summary>
    /// Called by a task that isn't doing anything (e.g. waiting on child tasks).
    /// Use this to run another task (if any) and return to the previous execution point
    /// </summary>
    /// <returns>Returns true if a task was executed, false otherwise.</returns>
    bool threadWaiting();


    /// <summary>
    /// Wait for all jobs to finish and optionally flush the callback queue.
    /// </summary>
    /// <param name="flushCallbacks">Optional parameter.  If flushCallbacks is true, this function MUST BE CALLED FROM THE MAIN THREAD as it will call flushCallbackQueue() internally</param>
    void waitForAllTasks(bool flushCallbacks = false);


    PROPERTY_R( vector<std::thread>, threads );

  private:
    //ToDo: replace all friend class declarations with attorneys -Ionut;
    friend struct Task;
    friend void Start(Task& task, TaskPool& pool, const TaskPriority priority, DELEGATE<void>&& onCompletionFunction);
    friend void Wait(const Task& task, TaskPool& pool);
    friend void Parallel_For(TaskPool& pool, const ParallelForDescriptor& descriptor);

    void enqueue(Task& task, TaskPriority priority, DELEGATE<void>&& onCompletionFunction);
    void runTask(Task& task, bool hasCompletionCallback);

    /// Join all of the threads and block until all running tasks have completed.
    void join();
    bool deque( bool isIdleCall, PoolTask& taskOut, TaskPriority& priorityOut );
    bool dequeInternal( const TaskPriority& priorityIn, bool isIdleCall, PoolTask& taskOut );
    void waitForTask(const Task& task);
    /// Returns false if there were no available tasks to run
    bool executeOneTask( bool isIdleCall );

    QueueType& getQueue(TaskPriority priority) noexcept;

  private:
     const string _threadNamePrefix;

     struct CallbackEntry
     {
         CallbackEntry() = default;
         CallbackEntry(DELEGATE<void>&& cbk, U32 taskID) noexcept
             : _cbk(MOV(cbk)), _taskID(taskID)
         {
         }

         DELEGATE<void> _cbk;
         U32 _taskID = U32_MAX;
     };

     SharedMutex _taskCallbacksLock;
     eastl::fixed_vector<CallbackEntry, 1 << 9, true> _taskCallbacks;

     moodycamel::ConcurrentQueue<U32> _threadedCallbackBuffer{};

     QueueType _normalQueue;
     QueueType _highPriorityqueue;

     Mutex _taskFinishedMutex;
     std::condition_variable _taskFinishedCV;

     std::atomic_uint _runningTaskCount = 0u;
     std::atomic_size_t _activeThreads{ 0u };

     std::atomic_bool _isRunning;
};

template<class Predicate>
Task* CreateTask(Predicate&& threadedFunction);

template<class Predicate>
Task* CreateTask(Task* parentTask, Predicate&& threadedFunction);

void Parallel_For(TaskPool& pool, const ParallelForDescriptor& descriptor, const DELEGATE<void, const Task*, U32/*start*/, U32/*end*/>& cbk);

} //namespace Divide

#endif //DVD_TASK_POOL_H_

#include "TaskPool.inl"
