

#include "Headers/NavMeshContext.h"

#include "Core/Time/Headers/ApplicationTimer.h"
#include "Utility/Headers/Localization.h"

namespace Divide::AI::Navigation {

void rcContextDivide::doLog(const rcLogCategory category, 
                            const char* msg,
                             [[maybe_unused]] const I32 len)
{
    switch (category) {
        case RC_LOG_PROGRESS:
            Console::printfn(LOCALE_STR("RECAST_CTX_LOG_PROGRESS"), msg);
            break;
        case RC_LOG_WARNING:
            Console::printfn(LOCALE_STR("RECAST_CTX_LOG_WARNING"), msg);
            break;
        case RC_LOG_ERROR:
            Console::errorfn(LOCALE_STR("RECAST_CTX_LOG_ERROR"), msg);
            break;
    }
}

void rcContextDivide::doResetTimers() noexcept {
    for (I32 i = 0; i < RC_MAX_TIMERS; ++i) _accTime[i] = -1;
}

void rcContextDivide::doStartTimer(const rcTimerLabel label) noexcept {
    _startTime[label] = Time::App::ElapsedMilliseconds();
}

void rcContextDivide::doStopTimer(const rcTimerLabel label) {
    const D64 deltaTime = Time::App::ElapsedMilliseconds() - _startTime[label];
    if (_accTime[label] == -1) {
        _accTime[label] = to_I32(deltaTime);
    } else {
        _accTime[label] += to_I32(deltaTime);
    }
}

I32 rcContextDivide::doGetAccumulatedTime(const rcTimerLabel label) const noexcept {
    return _accTime[label];
}

}  // namespace Divide::AI::Navigation
