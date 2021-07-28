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

bool ProfileTimer::s_enabled = true;

ScopedTimer::ScopedTimer(ProfileTimer& timer) 
    : _timer(timer)
{
    _timer.start();
}

ScopedTimer::~ScopedTimer()
{
    _timer.stop();
}

void ProfileTimer::start() {
    if (timersEnabled()) {
        _timer = App::ElapsedMicroseconds();
    }
}

void ProfileTimer::stop() {
    if (timersEnabled()) {
        _timerAverage += App::ElapsedMicroseconds() - _timer;
        _timerCounter++;
    }
}

void ProfileTimer::reset() noexcept {
    _timerAverage = 0;
    _timerCounter = 0;
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
    const U32 childID = child._globalIndex;

    _children.erase(
        eastl::remove_if(begin(_children),
                         end(_children),
                         [childID](const U32 entry) {
                             return entry == childID;
                         }),
        end(_children));
    child._parent = Config::Profile::MAX_PROFILE_TIMERS + 1;
}

bool ProfileTimer::hasChildTimer(ProfileTimer& child) const
{
    const U32 childID = child._globalIndex;
    return eastl::any_of(cbegin(_children),
                         cend(_children),
                         [childID](const U32 entry) {
                             return entry == childID;
                         });
}

U64 ProfileTimer::getChildTotal() const {
    U64 ret = 0;
    for (const U32 child : _children) {
        if (g_profileTimersState[child]) {
            ret += g_profileTimers[child].get();
        }
    }
    return ret;
}

stringImpl ProfileTimer::print(const U32 level) const {
    stringImpl ret(Util::StringFormat("[ %s ] : [ %5.3f ms]",
                                        _name.c_str(),
                                        MicrosecondsToMilliseconds<F32>(get())));
    for (const U32 child : _children) {
        if (g_profileTimersState[child]) {
            ret.append("\n    " + g_profileTimers[child].print(level + 1));
        }
    }

    for (U32 i = 0; i < level; ++i) {
        ret.insert(0, "    ");
    }

    return ret;
}

U64 ProfileTimer::overhead() {
    constexpr U8 overheadLoopCount = 3;

    U64 overhead = 0;
    ProfileTimer test;
    const bool prevState = timersEnabled();

    if (!prevState) {
        enableTimers();
    }

    for (U8 i = 0; i < overheadLoopCount; ++i) {
        test.start();
        test.stop();
        overhead += test.get();
    }

    if (!prevState) {
        disableTimers();
    }

    return overhead / overheadLoopCount;
}

stringImpl ProfileTimer::printAll() {
    stringImpl ret(Util::StringFormat("Profiler overhead: [%d us]\n", overhead()));

    for (ProfileTimer& entry : g_profileTimers) {
        if (!g_profileTimersState[entry._globalIndex] ||
                entry._parent < Config::Profile::MAX_PROFILE_TIMERS ||
                    entry._timerCounter == 0) {
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

        U32 index = 0;
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

bool ProfileTimer::timersEnabled() noexcept {
    return s_enabled;
}

void ProfileTimer::enableTimers() noexcept {
    s_enabled = true;
}

void ProfileTimer::disableTimers() noexcept {
    s_enabled = false;
}

}  // namespace Divide::Time
