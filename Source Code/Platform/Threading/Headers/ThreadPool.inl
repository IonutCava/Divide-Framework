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
#ifndef _PLATFORM_TASK_POOL_INL_
#define _PLATFORM_TASK_POOL_INL_

namespace Divide {
    template<bool IsBlocking>
    ThreadPool<IsBlocking>::ThreadPool(TaskPool& parent, const U32 threadCount)
        : _parent(parent)
    {
        _threads.reserve(threadCount);

        for (U32 idx = 0u; idx < threadCount; ++idx) {
            _threads.emplace_back(
            [&, idx]{
                const std::thread::id threadID = std::this_thread::get_id();
                _parent.onThreadCreate(idx, threadID);
                while (_isRunning) {
                    executeOneTask(true);
                }

                _parent.onThreadDestroy(threadID);
            });
        }
    }

    template<bool IsBlocking>
    ThreadPool<IsBlocking>::~ThreadPool()
    {
        join();
    }

    template<bool IsBlocking>
    void ThreadPool<IsBlocking>::join() {
        if (!_isRunning) {
            return;
        }
        _isRunning = false;

        const size_t threadCount = _threads.size();
        for (size_t idx = 0; idx < threadCount; ++idx) {
            addTask([]([[maybe_unused]] const bool wait) noexcept { return true; });
        }

        for (std::thread& thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    template<bool IsBlocking>
    void ThreadPool<IsBlocking>::wait() const noexcept {
        if (_isRunning) {
            // Busy wait
            while (_tasksLeft.load() > 0) {
                std::this_thread::yield();
            }
        }
    }

    template<bool IsBlocking>
    bool ThreadPool<IsBlocking>::addTask(PoolTask&& job) {
        if (_queue.enqueue(MOV(job))) {
            _tasksLeft.fetch_add(1);
            return true;
        }

        return false;
    }

    template<bool IsBlocking>
    void ThreadPool<IsBlocking>::executeOneTask(const bool waitForTask) {
        PoolTask task = {};
        if (dequeTask(waitForTask, task)) {
            if (!task(IsBlocking && !waitForTask)) {
                addTask(MOV(task));
            }
            _tasksLeft.fetch_sub(1);
        }
    }

    template<>
    inline bool ThreadPool<true>::dequeTask(const bool waitForTask, PoolTask& taskOut) {
        if (waitForTask) {
            _queue.wait_dequeue(taskOut);
        } else if (!_queue.try_dequeue(taskOut)) {
            return false;
        }
        
        return true;
    }

    template<>
    inline bool ThreadPool<false>::dequeTask(const bool waitForTask, PoolTask& taskOut) {
        if (waitForTask) {
            while (!_queue.try_dequeue(taskOut)) {
                std::this_thread::yield();
            }
        } else if (!_queue.try_dequeue(taskOut)) {
            return false;
        }
        

        return true;
    }

};//namespace Divide

#endif //_PLATFORM_TASK_POOL_INL_
