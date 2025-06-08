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
#ifndef DVD_TASKS_H_
#define DVD_TASKS_H_

namespace Divide {

class TaskPool;

enum class TaskPriority : U8
{
    DONT_CARE = 0,
    DONT_CARE_NO_IDLE, ///< don't run this task while idle or when waiting for other tasks to finish
    HIGH,
    REALTIME, ///< not threaded
    COUNT
};

struct alignas(128) Task
{
    static constexpr U32 INVALID_TASK_ID = Config::MAX_POOLED_TASKS;

    DELEGATE<void, Task&> _callback;
    Task* _parent{ nullptr };
    std::atomic_uint _unfinishedJobs{ 0u };
    U32 _globalId{ INVALID_TASK_ID };
};

constexpr auto TASK_NOP = [](Task&) { NOP(); };

void Start(Task& task, TaskPool& pool, TaskPriority priority = TaskPriority::DONT_CARE, DELEGATE<void>&& onCompletionFunction = {});
void Wait(const Task& task, TaskPool& pool);

[[nodiscard]] bool Finished(const Task& task) noexcept;

}  // namespace Divide

#endif //DVD_TASKS_H_

#include "Task.inl"
