

#include <thread>
#include "Headers/PlatformRuntime.h"

namespace Divide::Runtime
{

namespace detail
{
    static std::thread::id g_mainThreadID;
};

const std::thread::id& mainThreadID() noexcept
{
    return detail::g_mainThreadID;
}

void mainThreadID(const std::thread::id& threadID) noexcept
{
    detail::g_mainThreadID = threadID;
}

bool resetMainThreadID() noexcept
{
    detail::g_mainThreadID = {};
    return true;
}

}; //namespace Divide::Runtime
