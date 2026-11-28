// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ApplicationTimer.h"

#include "Utility/Headers/Localization.h"

namespace Divide::Time
{

namespace
{
    // Time stamp at application initialization
    std::atomic<TimeValue::duration::rep> g_startupTicks{};
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

void ApplicationTimer::reset() noexcept
{
    g_elapsedTimeUs.store(0ULL);
    const TimeValue startupTicks = std::chrono::high_resolution_clock::now();
    g_startupTicks.store(startupTicks.time_since_epoch().count(), std::memory_order_relaxed);
    g_frameDelay = startupTicks;
    resetFPSCounter();
}

void ApplicationTimer::update()
{
    const TimeValue startupTicks{TimeValue::duration{g_startupTicks.load(std::memory_order_relaxed)}};
    const TimeValue currentTicks = std::chrono::high_resolution_clock::now();
    g_elapsedTimeUs = to_U64(std::chrono::duration_cast<USec>(currentTicks - startupTicks).count());

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
         Util::StringFormatTo( _benchmarkReport,
                               LOCALE_STR("FRAMERATE_FPS_OUTPUT"),
                               fps,
                               _frameRateHandler.averageFrameRate(),
                               _frameRateHandler.maxFrameRate(),
                               _frameRateHandler.minFrameRate(),
                               frameTime);
    }
}

namespace App
{

D64 ElapsedMicroseconds() noexcept
{
    const TimeValue startupTicks{TimeValue::duration{g_startupTicks.load(std::memory_order_relaxed)}};
    return to_D64( std::chrono::duration_cast<USec>(std::chrono::high_resolution_clock::now() - startupTicks).count() );
}

} //namespace App

}  // namespace Divide::Time
