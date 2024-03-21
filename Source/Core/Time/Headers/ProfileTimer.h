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
#ifndef DVD_CORE_TIME_PROFILE_TIMER_H_
#define DVD_CORE_TIME_PROFILE_TIMER_H_

namespace Divide {
namespace Time {

class ApplicationTimer;
class ProfileTimer {
   public:
    ProfileTimer() = default;

    void start() noexcept;
    void stop() noexcept;
    void reset() noexcept;
    [[nodiscard]] string print(U32 level = 0) const;

    [[nodiscard]] U64 get() const;
    [[nodiscard]] U64 getChildTotal() const;
    [[nodiscard]] const string& name() const noexcept;

    static string printAll();
    static ProfileTimer& getNewTimer(const char* timerName);
    static void removeTimer(ProfileTimer& timer);

    static U64 overhead();

   // time data
   protected:
    string _name = "";
    U64 _timer = 0UL;
    U64 _timerAverage = 0UL;
    U32 _timerCounter = 0;
    U32 _globalIndex = 0;

   // timer <-> timer relationship
   public:
    void addChildTimer(ProfileTimer& child);
    void removeChildTimer(ProfileTimer& child);

    bool hasChildTimer(const ProfileTimer& child) const;

   protected:
     vector<U32> _children;
     U32 _parent = Config::Profile::MAX_PROFILE_TIMERS + 1;
};

class ScopedTimer final : NonCopyable {
public:
    explicit ScopedTimer(ProfileTimer& timer) noexcept;
    ~ScopedTimer();

private:
    ProfileTimer& _timer;
};

ProfileTimer& ADD_TIMER(const char* timerName);
void REMOVE_TIMER(ProfileTimer*& timer);

void START_TIMER(ProfileTimer& timer) noexcept;
void STOP_TIMER(ProfileTimer& timer) noexcept;
U64  QUERY_TIMER(const ProfileTimer& timer) noexcept;

string PRINT_TIMER(ProfileTimer& timer);

}  // namespace Time
}  // namespace Divide

#endif  //DVD_CORE_TIME_PROFILE_TIMER_H_

#include "ProfileTimer.inl"
