#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Profiler.h"

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
