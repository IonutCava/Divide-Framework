#include "stdafx.h"

#include "config.h"

#include "Headers/ProfileTimer.h"
#include "Headers/ApplicationTimer.h"

#include "Core/Headers/StringHelper.h"

namespace Divide::Time {

namespace {
    std::array<ProfileTimer, Config::Profile::MAX_PROFILE_TIMERS> g_profileTimers;
    std::array<bool, Config::Profile::MAX_PROFILE_TIMERS> g_profileTimersState;

    bool g_timersInit = false;
}

ScopedTimer::ScopedTimer(ProfileTimer& timer) noexcept
    : _timer(timer)
{
    _timer.start();
}

ScopedTimer::~ScopedTimer()
{
    _timer.stop();
}

void ProfileTimer::start() noexcept {
    _timer = App::ElapsedMicroseconds();
}

void ProfileTimer::stop() noexcept {
    _timerAverage += App::ElapsedMicroseconds() - _timer;
    _timerCounter++;
}

void ProfileTimer::reset() noexcept {
    _timerAverage = 0u;
    _timerCounter = 0u;
}

void ProfileTimer::addChildTimer(ProfileTimer& child) {
    // must not have a parent
    assert(child._parent > Config::Profile::MAX_PROFILE_TIMERS);
    // must be unique child
    assert(!hasChildTimer(child));

    _children.push_back(child._globalIndex);
    child._parent = _globalIndex;
}

void ProfileTimer::removeChildTimer(ProfileTimer& child) {
    erase_if(_children, [childID = child._globalIndex](const U32 entry) { return entry == childID; });
    child._parent = Config::Profile::MAX_PROFILE_TIMERS + 1u;
}

bool ProfileTimer::hasChildTimer(const ProfileTimer& child) const {
    return eastl::any_of(cbegin(_children),
                         cend(_children),
                         [childID = child._globalIndex](const U32 entry) {
                             return entry == childID;
                         });
}

U64 ProfileTimer::getChildTotal() const {
    U64 ret = 0u;
    for (const U32 child : _children) {
        if (g_profileTimersState[child]) {
            ret += g_profileTimers[child].get();
        }
    }
    return ret;
}

string ProfileTimer::print(const U32 level) const {
    string ret(Util::StringFormat("[ %s ] : [ %5.3f ms]",
                                        _name.c_str(),
                                        MicrosecondsToMilliseconds<F32>(get())));
    for (const U32 child : _children) {
        if (g_profileTimersState[child]) {
            ret.append("\n    " + g_profileTimers[child].print(level + 1));
        }
    }

    for (U32 i = 0u; i < level; ++i) {
        ret.insert(0, "    ");
    }

    return ret;
}

U64 ProfileTimer::overhead() {
    constexpr U8 overheadLoopCount = 3u;

    U64 overhead = 0u;
    ProfileTimer test;

    for (U8 i = 0u; i < overheadLoopCount; ++i) {
        test.start();
        test.stop();
        overhead += test.get();
    }

    return overhead / overheadLoopCount;
}

string ProfileTimer::printAll() {
    string ret(Util::StringFormat("Profiler overhead: [%d us]\n", overhead()));

    for (ProfileTimer& entry : g_profileTimers) {
        if (!g_profileTimersState[entry._globalIndex] ||
            entry._parent < Config::Profile::MAX_PROFILE_TIMERS ||
            entry._timerCounter == 0u)
        {
            continue;
        }

        ret.append(entry.print());
        ret.append("\n");
        entry.reset();
    }

    return ret;
}

ProfileTimer& ProfileTimer::getNewTimer(const char* timerName) {
    if (!g_timersInit) {
        g_profileTimersState.fill(false);

        U32 index = 0u;
        for (ProfileTimer& entry : g_profileTimers) {
            entry._globalIndex = index++;
        }
        g_timersInit = true;
    }

    for (ProfileTimer& entry : g_profileTimers) {
        if (!g_profileTimersState[entry._globalIndex]) {
            entry.reset();
            entry._name = timerName;
            g_profileTimersState[entry._globalIndex] = true;
            return entry;
        }
    }

    DIVIDE_UNEXPECTED_CALL_MSG("Reached max profile timer count!");
    return g_profileTimers[0];
}

void ProfileTimer::removeTimer(ProfileTimer& timer) {
    g_profileTimersState[timer._globalIndex] = false;
    if (timer._parent < Config::Profile::MAX_PROFILE_TIMERS) {
        g_profileTimers[timer._parent].removeChildTimer(timer);
    }
}

}  // namespace Divide::Time
