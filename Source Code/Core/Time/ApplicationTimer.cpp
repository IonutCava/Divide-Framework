#include "stdafx.h"

#include "Headers/ApplicationTimer.h"

#include "Utility/Headers/Localization.h"

#include "Core/Headers/StringHelper.h"

namespace Divide::Time {

namespace {
    // Time stamp at application initialization
    TimeValue g_startupTicks;
    // Previous frame's time stamp
    TimeValue g_frameDelay;
    std::atomic<U64> g_elapsedTimeUs;

    /// Benchmark reset frequency in milliseconds
    constexpr U64 g_benchmarkFrequencyUS = MillisecondsToMicroseconds<U64>(500);
}

ApplicationTimer::ApplicationTimer() noexcept
{
    reset();
}

void ApplicationTimer::reset() noexcept {
    g_elapsedTimeUs.store(0ULL);
    g_startupTicks = std::chrono::high_resolution_clock::now();
    g_frameDelay = g_startupTicks;
    resetFPSCounter();
}

void ApplicationTimer::update() {
    const TimeValue currentTicks = std::chrono::high_resolution_clock::now();
    g_elapsedTimeUs = to_U64(std::chrono::duration_cast<USec>(currentTicks - g_startupTicks).count());

    const U64 duration = to_U64(std::chrono::duration_cast<USec>(currentTicks - g_frameDelay).count());
    g_frameDelay = currentTicks;

    _speedfactor = Time::MicrosecondsToSeconds<F32>(duration * _targetFrameRate);
    _frameRateHandler.tick(g_elapsedTimeUs);
    
    if (g_elapsedTimeUs - _lastBenchmarkTimeStamp > g_benchmarkFrequencyUS)
    {
        F32 fps = 0.f;
        F32 frameTime = 0.f;
        _frameRateHandler.frameRateAndTime(fps, frameTime);

        _lastBenchmarkTimeStamp = g_elapsedTimeUs;
        _benchmarkReport = Util::StringFormat(Locale::Get(_ID("FRAMERATE_FPS_OUTPUT")),
                                              fps,
                                              _frameRateHandler.averageFrameRate(),
                                              _frameRateHandler.maxFrameRate(),
                                              _frameRateHandler.minFrameRate(),
                                              frameTime);
    }
}

namespace Game {
    /// The following functions return the time updated in the main app loop only!
    U64 ElapsedNanoseconds() noexcept {
        return MicrosecondsToNanoseconds(ElapsedMicroseconds());
    }
    U64 ElapsedMicroseconds() noexcept {
        return g_elapsedTimeUs;
    }
    D64 ElapsedMilliseconds() noexcept {
        return MicrosecondsToMilliseconds<D64, U64>(ElapsedMicroseconds());
    }
    D64 ElapsedSeconds() noexcept {
        return MicrosecondsToSeconds(ElapsedMicroseconds());
    }
}

namespace App {
    /// The following functions force a timer update (a call to query performance timer).
    U64 ElapsedNanoseconds() noexcept {
        return MicrosecondsToNanoseconds(ElapsedMicroseconds());
    }
    U64 ElapsedMicroseconds() noexcept {
        return to_U64(std::chrono::duration_cast<USec>(std::chrono::high_resolution_clock::now() - g_startupTicks).count());
    }
    D64 ElapsedMilliseconds() noexcept {
        return MicrosecondsToMilliseconds<D64, U64>(ElapsedMicroseconds());
    }
    D64 ElapsedSeconds() noexcept {
        return MicrosecondsToSeconds(ElapsedMicroseconds());
    }
}
}  // namespace Divide::Time
