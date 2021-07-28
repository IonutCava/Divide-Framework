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
#ifndef _PLATFORM_TASK_POOL_H_
#define _PLATFORM_TASK_POOL_H_

namespace Divide {

using PoolTask = DELEGATE_STD<bool, bool/*threadWaitingCall*/>;

class TaskPool;

template<bool IsBlocking>
struct Queue {};

template<>
struct Queue<false> {
    moodycamel::ConcurrentQueue<PoolTask> _container;
};

template<>
struct Queue<true> {
    moodycamel::BlockingConcurrentQueue<PoolTask> _container;
};
// Dead simple ThreadPool class
template<bool IsBlocking>
class ThreadPool
{
public:

    explicit ThreadPool(TaskPool& parent, U32 threadCount);
    ~ThreadPool();

    // Add a new task to the pool's queue
    bool addTask(PoolTask&& job);

    // Join all of the threads and block until all running tasks have completed.
    void join();

    // Wait for all running jobs to finish
    void wait() const noexcept;

    void executeOneTask(bool waitForTask);

    PROPERTY_R(vectorEASTL<std::thread>, threads);

protected:
    bool dequeTask(bool waitForTask, PoolTask& taskOut);
    void onThreadCreate(const std::thread::id& threadID) const;
    void onThreadDestroy(const std::thread::id& threadID) const;

protected:
    TaskPool& _parent;
    Queue<IsBlocking> _queue;
    std::atomic_int _tasksLeft = 0;
    bool _isRunning = false;
};

}; //namespace Divide

#endif //_PLATFORM_TASK_POOL_H_

#include "ThreadPool.inl"