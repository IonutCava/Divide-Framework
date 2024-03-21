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
#ifndef DVD_SHARED_MUTEX_BOOST_H_
#define DVD_SHARED_MUTEX_BOOST_H_

#include <shared_mutex>

namespace Divide {

using Mutex = std::mutex;
using TimedMutex = std::timed_mutex;

using SharedMutex = std::shared_mutex;
using SharedTimedMutex = std::shared_timed_mutex;

template<typename mutex>
using SharedLock = std::shared_lock<mutex>;

template<typename mutex>
using UniqueLock = std::unique_lock<mutex>;

template<typename mutex>
using LockGuard = std::lock_guard<mutex>;

template<typename... mutexes>
using ScopedLock = std::scoped_lock<mutexes...>;

};  // namespace Divide

#endif //DVD_SHARED_MUTEX_BOOST_H_
