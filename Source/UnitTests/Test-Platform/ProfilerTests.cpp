// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Profiler.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Time/Headers/ProfileTimer.h"

TEST_CASE("Profile timers remain valid across application timer resets", "[profiler_tests][timing]")
{
    using namespace Divide;

    Time::ApplicationTimer applicationTimer;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});

    Time::ProfileTimer timer;
    const auto before = std::chrono::steady_clock::now();
    timer.start();
    applicationTimer.reset();
    timer.stop();
    const auto after = std::chrono::steady_clock::now();
    // Separately truncated timestamps can differ by one microsecond.
    const U64 upperBound = to_U64(std::chrono::duration_cast<std::chrono::microseconds>(after - before).count()) + 1u;
    CHECK(timer.get() <= upperBound);

    timer.reset();
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    timer.stop();
    CHECK(timer.get() >= 1000u);
}

#if !USE_OPTICK
TEST_CASE("Disabled profiling does not evaluate arguments", "[profiler_tests]")
{
    int evaluated = 0;
    PROFILE_SCOPE(++evaluated, ++evaluated);
    PROFILE_SCOPE_AUTO(++evaluated);
    PROFILE_TAG(++evaluated, ++evaluated);
    PROFILE_FRAME(++evaluated);
    PROFILE_VK_INIT(++evaluated, ++evaluated, ++evaluated, ++evaluated, ++evaluated, ++evaluated);
    PROFILE_VK_PRESENT(++evaluated);
    PROFILE_VK_EVENT_AUTO();
    PROFILE_VK_EVENT(++evaluated);
    PROFILE_VK_EVENT_AND_CONTEX(++evaluated, ++evaluated);
    PROFILE_VK_EVENT_AUTO_AND_CONTEXT(++evaluated);
    CHECK(evaluated == 0);
}
#endif
