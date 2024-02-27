

#include <thread>

namespace Divide {
namespace Runtime {

namespace detail {
    bool g_idSet = false;
    std::thread::id g_mainThreadID;
};

const std::thread::id&  mainThreadID() noexcept {
    return detail::g_mainThreadID;
}

void mainThreadID(const std::thread::id& threadID) noexcept {
    assert(!detail::g_idSet);

    detail::g_mainThreadID = threadID;
    detail::g_idSet = true;
}

bool resetMainThreadID() noexcept {
    detail::g_idSet = false;
    return true;
}

}; //namespace Runtime
}; //namespace Divide