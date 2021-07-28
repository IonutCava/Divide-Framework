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
#ifndef _TASK_POOL_H_
#define _TASK_POOL_H_

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

class TaskPool final : public GUIDWrapper {
public:
    enum class TaskPoolType : U8 {
        TYPE_LOCKFREE = 0,
        TYPE_BLOCKING,
        COUNT
    };

    static bool USE_OPTICK_PROFILER;

  public:

    explicit TaskPool();
    explicit TaskPool(U32 threadCount, TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate = {}, const stringImpl& workerName = "DVD_WORKER");

    ~TaskPool();
    
    bool init(U32 threadCount, TaskPoolType poolType, const DELEGATE<void, const std::thread::id&>& onThreadCreate = {}, const stringImpl& workerName = "DVD_WORKER");
    void shutdown();

    static Task* AllocateTask(Task* parentTask, bool allowedInIdle);

    /// Returns the number of callbacks processed
    size_t flushCallbackQueue();
    void waitForAllTasks(bool yield, bool flushCallbacks);

    [[nodiscard]] U32 workerThreadCount() const noexcept {
        return _workerThreadCount;
    }

    bool operator==(const TaskPool& other) const noexcept {
        return getGUID() == other.getGUID();
    }

    bool operator!=(const TaskPool& other) const noexcept {
        return getGUID() != other.getGUID();
    }

    // Called by a task that isn't doing anything (e.g. waiting on child tasks).
    // Use this to run another task (if any) and return to the previous execution point
    void threadWaiting();

  private:
    //ToDo: replace all friend class declarations with attorneys -Ionut;
    friend struct Task;
    friend void Wait(const Task& task);

    friend void Start(Task& task, TaskPool& pool, const TaskPriority priority, const DELEGATE<void>& onCompletionFunction);
    friend void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor);
    friend void RunLocally(Task& task, TaskPool& pool, TaskPriority priority, bool hasOnCompletionFunction);

    void taskCompleted(U32 taskIndex, bool hasOnCompletionFunction);
    
    bool enqueue(PoolTask&& task, TaskPriority priority, U32 taskIndex, const DELEGATE<void>& onCompletionFunction);

    template<bool IsBlocking>
    friend class ThreadPool;

    void onThreadCreate(const std::thread::id& threadID);
    void onThreadDestroy(const std::thread::id& threadID);

  private:
      struct PoolHolder {
          template<bool IsBlocking>
          using PoolImpl = ThreadPool<IsBlocking>;
          std::pair<PoolImpl<true>*, PoolImpl<false>*> _poolImpl = {nullptr, nullptr};

          [[nodiscard]] bool addTask(PoolTask&& job) const;
          [[nodiscard]] bool init() const noexcept;
          void waitAndJoin() const;
          void threadWaiting() const;
      };

     hashMap<U32, vectorEASTL<DELEGATE<void>>> _taskCallbacks{};
     DELEGATE<void, const std::thread::id&> _threadCreateCbk{};
     moodycamel::ConcurrentQueue<U32> _threadedCallbackBuffer{};
     PoolHolder _poolImpl = {};
     stringImpl _threadNamePrefix = "";
     std::atomic_uint _runningTaskCount = 0u;
     std::atomic_uint _threadCount = 0u;
     U32 _workerThreadCount = 0u;
};

template<class Predicate>
Task* CreateTask(Predicate&& threadedFunction, bool allowedInIdle = true);

template<class Predicate>
Task* CreateTask(Task* parentTask, Predicate&& threadedFunction, bool allowedInIdle = true);

void parallel_for(TaskPool& pool, const ParallelForDescriptor& descriptor);

void WaitForAllTasks(TaskPool& pool, bool yield, bool flushCallbacks);

} //namespace Divide

#endif //_TASK_POOL_H_

#include "TaskPool.inl"