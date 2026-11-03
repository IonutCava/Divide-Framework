# Copyright (c) 2026 DIVIDE-Studio
# Copyright (c) 2009-2026 Ionut Cava
# SPDX-License-Identifier: MIT

"""Regression tests for swapchain frame submission and cancellation."""

from pathlib import Path
import unittest

if __package__:
    from .ExposureBufferTests import run_cpp
else:
    from ExposureBufferTests import run_cpp


ROOT = Path(__file__).resolve().parents[2]
VIDEO = ROOT / "Source" / "Platform" / "Video"


class FrameLifecycleTests(unittest.TestCase):
    def test_vulkan_queue_locks_follow_shared_handles_and_idle_locks_all(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "vkDevice.cpp").read_text(encoding="utf-8")
        start = source.index("    void VKDevice::submitToQueue(")
        end = source.index("}; //namespace Divide", start)
        functions = source[start:end]
        run_cpp(self, f"""
#include <array>
#include <cassert>
#include <cstdint>
#include <mutex>
#define PROFILE_SCOPE_AUTO(...)
#define VK_CHECK(call) assert((call) == VK_SUCCESS)
using U8 = std::uint8_t;
using VkResult = int;
using VkFence = int;
using VkQueue = int;
using VkDevice = int;
constexpr VkResult VK_SUCCESS = 0;
struct VkSubmitInfo {{}};
struct VkPresentInfoKHR {{}};
struct Mutex {{
    bool locked = false;
    void lock() {{ assert(!locked); locked = true; }}
    void unlock() {{ assert(locked); locked = false; }}
    bool try_lock() {{ if (locked) return false; lock(); return true; }}
}};
template<typename T> using LockGuard = std::lock_guard<T>;
template<typename... T> using ScopedLock = std::scoped_lock<T...>;
enum class QueueType : U8 {{ GRAPHICS, COMPUTE, TRANSFER, COUNT }};
U8 to_base(QueueType type) {{ return static_cast<U8>(type); }}
struct Queue {{ VkQueue _queue = 0; }};
struct VKDevice {{
    std::array<Queue, 3> _queues;
    mutable std::array<Mutex, 3> _queueLocks;
    VkDevice getVKDevice() const {{ return 1; }}
    void submitToQueue(QueueType, const VkSubmitInfo&, VkFence&) const;
    VkResult queuePresent(QueueType, const VkPresentInfoKHR&) const;
    Mutex& queueLock(QueueType) const noexcept;
    VkResult waitIdle() const;
}};
const VKDevice* active = nullptr;
unsigned submissions = 0, presentations = 0, idleCalls = 0;
VkResult vkQueueSubmit(VkQueue queue, int, const VkSubmitInfo*, VkFence) {{
    assert(queue == 42 && active->_queueLocks[0].locked);
    ++submissions; return VK_SUCCESS;
}}
VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR*) {{
    assert(queue == 42 && active->_queueLocks[0].locked);
    ++presentations; return VK_SUCCESS;
}}
VkResult vkDeviceWaitIdle(VkDevice) {{
    for (const Mutex& lock : active->_queueLocks) assert(lock.locked);
    ++idleCalls; return VK_SUCCESS;
}}
{functions}
int main() {{
    VKDevice device;
    active = &device;
    device._queues[0]._queue = 42;
    device._queues[1]._queue = 7;
    device._queues[2]._queue = 42;
    assert(&device.queueLock(QueueType::GRAPHICS) == &device.queueLock(QueueType::TRANSFER));
    assert(&device.queueLock(QueueType::GRAPHICS) != &device.queueLock(QueueType::COMPUTE));
    VkFence fence = 1;
    device.submitToQueue(QueueType::TRANSFER, {{}}, fence);
    assert(device.queuePresent(QueueType::GRAPHICS, {{}}) == VK_SUCCESS);
    assert(device.waitIdle() == VK_SUCCESS);
    assert(submissions == 1 && presentations == 1 && idleCalls == 1);
    for (const Mutex& lock : device._queueLocks) assert(!lock.locked);
}}
""")

    def test_swapchain_destroys_views_and_frees_command_buffers(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "vkSwapChain.cpp").read_text(encoding="utf-8")
        start = source.index("    void VKSwapChain::destroy()")
        end = source.index("    ErrorCode VKSwapChain::create(", start)
        function = source[start:end]
        failed_create = source[source.index("        if ( !vkbSwapchain )", end):
                               source.index("        _swapChain = vkbSwapchain.value();", end)]
        self.assertIn("destroy();", failed_create)
        run_cpp(self, f"""
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
using VkDevice = int;
using VkCommandPool = int;
using VkCommandBuffer = int;
using VkSemaphore = int;
using VkFence = int;
constexpr int VK_NULL_HANDLE = 0;
enum class QueueType {{ GRAPHICS }};
std::vector<std::string> events;
namespace vkb {{
    struct Swapchain {{
        int swapchain = 1;
        void destroy_image_views(const std::vector<int>& views) {{
            if (!views.empty()) events.push_back("views");
        }}
    }};
    void destroy_swapchain(Swapchain&) {{ events.push_back("swapchain"); }}
}}
struct Device {{
    VkDevice getVKDevice() const {{ return 1; }}
    struct Queue {{ VkCommandPool _pool = 2; }};
    Queue getQueue(QueueType) const {{ return {{}}; }}
}};
void vkFreeCommandBuffers(VkDevice, VkCommandPool, int count, const VkCommandBuffer* buffer) {{
    assert(count == 1 && *buffer != VK_NULL_HANDLE); events.push_back("command");
}}
void vkDestroySemaphore(VkDevice, VkSemaphore, void*) {{ events.push_back("semaphore"); }}
void vkDestroyFence(VkDevice, VkFence, void*) {{ events.push_back("fence"); }}
struct FrameData {{
    VkCommandBuffer _commandBuffer = 1;
    VkSemaphore _presentSemaphore = 2;
    struct {{ VkFence _handle = 3; }} _renderFence;
}};
struct VKSwapChain {{
    Device _device;
    vkb::Swapchain _swapChain;
    std::vector<FrameData> _frames{{2}};
    FrameData* _activeFrame = &_frames[0];
    std::vector<int> _swapchainImages{{1, 2}}, _swapchainImageViews{{3, 4}};
    std::vector<int> _swapchainImagePresented{{0, 0}};
    std::vector<VkSemaphore> _renderSemaphores{{5, 6}};
    void destroy();
}};
{function}
int main() {{
    VKSwapChain chain;
    chain.destroy();
    assert(chain._activeFrame == nullptr && chain._swapChain.swapchain == VK_NULL_HANDLE);
    assert(chain._frames.empty() && chain._swapchainImages.empty() &&
           chain._swapchainImageViews.empty() && chain._swapchainImagePresented.empty() &&
           chain._renderSemaphores.empty());
    assert(std::count(events.begin(), events.end(), "command") == 2);
    assert(std::count(events.begin(), events.end(), "fence") == 2);
    assert(std::count(events.begin(), events.end(), "semaphore") == 4);
    assert(std::find(events.begin(), events.end(), "views") <
           std::find(events.begin(), events.end(), "swapchain"));
    const auto count = events.size();
    chain.destroy();
    assert(events.size() == count);
}}
""")

    def test_recreation_waits_for_each_window_and_handles_idle_failure(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "VKWrapper.cpp").read_text(encoding="utf-8")
        start = source.index("    bool VK_API::recreateSwapChain(")
        end = source.index("    void VK_API::initStatePerWindow(", start)
        function = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <string>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
#define PROFILE_SCOPE(...)
#define DIVIDE_GPU_ASSERT(condition, ...) ((void)(condition))
#define LOCALE_STR(value) value
using VkResult = int;
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_DEVICE_LOST = -1;
enum class ErrorCode {{ NO_ERR, FAILED }};
enum class WindowFlags {{ VSYNC = 1 }};
template<typename T> int to_base(T value) {{ return static_cast<int>(value); }}
std::vector<std::string> events;
struct DisplayWindow {{
    int id;
    bool isMinimized = false;
    bool minimized() const {{ return isMinimized; }}
    int getGUID() const {{ return id; }}
    int flags() const {{ return 0; }}
}};
struct Device {{
    VkResult result = VK_SUCCESS;
    VkResult waitIdle() {{ events.push_back("wait"); return result; }}
}};
struct SwapChain {{
    ErrorCode result = ErrorCode::NO_ERR;
    ErrorCode create(int, bool, int) {{ events.push_back("create"); return result; }}
    bool isSRGB() const {{ return true; }}
    struct Data {{ int image_format = 1; }};
    Data getSwapChain() const {{ return {{}}; }}
}};
struct DeviceInformation {{ bool _screenSRGB = true; }};
struct Context {{
    DisplayWindow main{{1}};
    struct Config {{ struct Runtime {{ bool adaptiveSync = false; }} runtime; }} settings;
    Context& context() {{ return *this; }}
    DisplayWindow& mainWindow() {{ return main; }}
    Config& config() {{ return settings; }}
}};
struct GFXDevice {{
    static DeviceInformation GetDeviceInformation() {{ return {{}}; }}
    static void OverrideDeviceInformation(DeviceInformation) {{}}
}};
struct VKPerWindowState {{ DisplayWindow* _window; SwapChain* _swapChain; int _surface = 1; }};
struct SDLEventManager {{ static void pollEvents() {{ events.push_back("poll"); }} }};
struct Console {{
    template<typename... T> static void errorfn(T&&...) {{ events.push_back("error"); }}
    template<typename... T> static void warnfn(T&&...) {{}}
}};
std::string VKErrorString(VkResult) {{ return "device lost"; }}
struct VK_API {{
    Device* _device;
    Context _context;
    void idle(bool) {{ events.push_back("idle"); }}
    bool recreateSwapChain(VKPerWindowState&);
}};
{function}
int main() {{
    Device device;
    VK_API api{{&device}};
    DisplayWindow secondary{{2}};
    SwapChain chain;
    VKPerWindowState state{{&secondary, &chain}};
    assert(api.recreateSwapChain(state));
    assert((events == std::vector<std::string>{{"wait", "create"}}));
    events.clear(); device.result = VK_ERROR_DEVICE_LOST;
    assert(!api.recreateSwapChain(state));
    assert((events == std::vector<std::string>{{"wait", "error"}}));
    events.clear(); secondary.isMinimized = true;
    assert(!api.recreateSwapChain(state));
    assert((events == std::vector<std::string>{{"idle", "poll"}}));
    events.clear(); secondary.isMinimized = false; device.result = VK_SUCCESS;
    state._window = &api._context.main;
    assert(api.recreateSwapChain(state));
    assert((events == std::vector<std::string>{{"wait", "create"}}));
}}
""")

    def test_vulkan_fence_remains_signaled_until_a_frame_is_submitted(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "vkSwapChain.cpp").read_text(encoding="utf-8")
        start = source.index("    VkResult VKSwapChain::beginFrame()")
        end = source.index("    vkb::Swapchain& VKSwapChain::getSwapChain()", start)
        functions = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
#define PROFILE_SCOPE(...)
#define PROFILE_VK_PRESENT(...)
#define VK_CHECK(call) assert((call) == VK_SUCCESS)
using U32 = std::uint32_t;
constexpr std::uint64_t U64_MAX = std::numeric_limits<std::uint64_t>::max();
using VkResult = int;
using VkDevice = int;
using VkFence = int;
using VkSemaphore = int;
using VkCommandBuffer = int;
using VkPipelineStageFlags = unsigned;
constexpr int VK_NULL_HANDLE = 0;
constexpr int VK_SUCCESS = 0, VK_ERROR_UNKNOWN = -1, VK_ERROR_OUT_OF_DATE_KHR = -2, VK_SUBOPTIMAL_KHR = 1;
constexpr unsigned VK_TRUE = 1, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 1;
constexpr unsigned VK_PIPELINE_STAGE_TRANSFER_BIT = 1, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT = 2;
constexpr unsigned VK_PIPELINE_STAGE_ALL_COMMANDS_BIT = 4, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1;
struct VkCommandBufferBeginInfo {{}};
struct VkSubmitInfo {{
    const VkPipelineStageFlags* pWaitDstStageMask = nullptr;
    U32 waitSemaphoreCount = 0;
    const VkSemaphore* pWaitSemaphores = nullptr;
    U32 signalSemaphoreCount = 0;
    const VkSemaphore* pSignalSemaphores = nullptr;
    U32 commandBufferCount = 0;
    const VkCommandBuffer* pCommandBuffers = nullptr;
}};
struct VkPresentInfoKHR {{
    unsigned sType = 0;
    const int* pSwapchains = nullptr;
    U32 swapchainCount = 0;
    const VkSemaphore* pWaitSemaphores = nullptr;
    U32 waitSemaphoreCount = 0;
    const U32* pImageIndices = nullptr;
}};
namespace vk {{
    VkCommandBufferBeginInfo commandBufferBeginInfo(unsigned) {{ return {{}}; }}
    VkSubmitInfo submitInfo() {{ return {{}}; }}
}}
template<typename T, int N, bool Overflow> struct fixed_vector : std::vector<T> {{
    void reset_lose_memory() {{ this->clear(); }}
}};
struct VKSubmitSempahore {{ using Container = fixed_vector<VkSemaphore, 8, true>; }};
U32 to_U32(std::size_t n) {{ return static_cast<U32>(n); }}
bool fences[3] = {{false, true, true}};
int waits = 0, resets = 0, submissions = 0, begun = 0;
VkResult nextAcquire = VK_SUCCESS;
VkResult vkWaitForFences(VkDevice, U32, const VkFence* fence, unsigned, std::uint64_t) {{
    ++waits; assert(fences[*fence]); return VK_SUCCESS;
}}
VkResult vkResetFences(VkDevice, U32, const VkFence* fence) {{
    assert(fences[*fence]); fences[*fence] = false; ++resets; return VK_SUCCESS;
}}
VkResult vkAcquireNextImageKHR(VkDevice, int, std::uint64_t, VkSemaphore, void*, U32* index) {{
    *index = 0; return nextAcquire;
}}
VkResult vkBeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*) {{
    ++begun; return VK_SUCCESS;
}}
VkResult vkEndCommandBuffer(VkCommandBuffer) {{ return VK_SUCCESS; }}
enum class QueueType {{ GRAPHICS }};
struct Device {{
    VkDevice getVKDevice() const {{ return 1; }}
    void submitToQueue(QueueType, const VkSubmitInfo& submit, VkFence& fence) const {{
        assert(!fences[fence] && submit.commandBufferCount == 1); ++submissions;
    }}
    VkResult queuePresent(QueueType, const VkPresentInfoKHR&) const {{ return VK_SUCCESS; }}
}};
struct GFXDevice {{ static U32 FrameCount() {{ return 0; }} }};
struct FrameData {{
    VkSemaphore _presentSemaphore = 1;
    VkCommandBuffer _commandBuffer = 1;
    struct {{ VkFence _handle = 1; }} _renderFence;
}};
struct VKSwapChain {{
    Device _device;
    std::vector<FrameData> _frames{{1}};
    FrameData* _activeFrame = nullptr;
    struct {{ int swapchain = 1; }} _swapChain;
    std::vector<VkSemaphore> _renderSemaphores{{2}};
    U32 _swapchainImageIndex = 0;
    bool _currentImageRenderedThisFrame = false;
    bool presented = false;
    void markCurrentImagePresented() {{ presented = true; }}
    VkResult beginFrame();
    VkResult endFrame(const VKSubmitSempahore::Container&);
}};
{functions}
int main() {{
    VKSwapChain first, second, empty;
    second._frames[0]._renderFence._handle = 2;
    empty._frames.clear();
    assert(empty.beginFrame() == VK_ERROR_OUT_OF_DATE_KHR && waits == 0);
    nextAcquire = VK_ERROR_OUT_OF_DATE_KHR;
    assert(first.beginFrame() == VK_ERROR_OUT_OF_DATE_KHR);
    assert(first.beginFrame() == VK_ERROR_OUT_OF_DATE_KHR);
    assert(fences[1] && resets == 0 && begun == 0);
    nextAcquire = VK_SUBOPTIMAL_KHR;
    assert(first.beginFrame() == VK_SUBOPTIMAL_KHR);
    assert(fences[1] && resets == 0 && begun == 1);
    assert(first.endFrame({{}}) == VK_SUCCESS);
    assert(!fences[1] && resets == 1 && submissions == 1 && first.presented);
    fences[1] = true;
    nextAcquire = VK_SUCCESS;
    assert(second.beginFrame() == VK_SUCCESS && fences[2]);
    assert(second.endFrame({{}}) == VK_SUCCESS);
    assert(!fences[2] && resets == 2 && submissions == 2 && second.presented);
    assert(first.beginFrame() == VK_SUCCESS && fences[1]);
    assert(first.endFrame({{}}) == VK_SUCCESS);
    assert(!fences[1] && resets == 3 && submissions == 3);
    fences[1] = true;  // simulated completion of the first window's submission
    assert(first.beginFrame() == VK_SUCCESS && fences[1]);
    assert(waits == 6);
}}
""")

    def test_failed_window_discards_queued_commands_without_replaying(self):
        source = (VIDEO / "GFXDevice.cpp").read_text(encoding="utf-8")
        start = source.index("    void GFXDevice::flushWindow( DisplayWindow& window )")
        end = source.index("    bool GFXDevice::framePreRender(", start)
        function = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
using U16 = std::uint16_t;
using I32 = std::int32_t;
using Mutex = std::mutex;
template<typename T> using LockGuard = std::lock_guard<T>;
template<typename T> struct vec2 {{ T width, height; }};
template<typename T> struct Rect {{ T x, y, width, height; }};
namespace GFX {{
    struct CommandBufferQueue {{
        struct Entry {{ int _buffer; }};
        std::vector<Entry> _commandBuffers;
        int resets = 0;
    }};
    void ResetCommandBufferQueue(CommandBufferQueue& queue) {{
        queue._commandBuffers.clear(); ++queue.resets;
    }}
}}
struct DisplayWindow {{
    bool canFlush;
    GFX::CommandBufferQueue queue;
    vec2<U16> getDrawableSize() const {{ return {{800, 600}}; }}
    GFX::CommandBufferQueue& getCurrentCommandBufferQueue() {{ return queue; }}
}};
struct Backend {{
    int prepared = 0, flushed = 0;
    bool prepareFlushWindow(DisplayWindow& window) {{ ++prepared; return window.canFlush; }}
    void flushWindow(DisplayWindow&) {{ ++flushed; }}
}};
struct GFXDevice {{
    Backend* _api;
    Mutex _queuedCommandbufferLock;
    int viewports = 0, scissors = 0;
    std::vector<int> replayed;
    void setViewport(Rect<I32>) {{ ++viewports; }}
    void setScissor(Rect<I32>) {{ ++scissors; }}
    void flushCommandBufferInternal(int value) {{ replayed.push_back(value); }}
    void flushWindow(DisplayWindow& window);
}};
{function}
int main() {{
    Backend backend;
    GFXDevice device{{&backend}};
    DisplayWindow skipped{{false}}, rendered{{true}};
    skipped.queue._commandBuffers = {{{{1}}, {{2}}}};
    rendered.queue._commandBuffers = {{{{3}}, {{4}}}};
    device.flushWindow(skipped);
    assert(device.replayed.empty() && skipped.queue._commandBuffers.empty());
    assert(skipped.queue.resets == 1 && device.viewports == 0 && device.scissors == 0);
    device.flushWindow(rendered);
    assert((device.replayed == std::vector<int>{{3, 4}}));
    assert(rendered.queue.resets == 1 && device.viewports == 1 && device.scissors == 1);
    assert(backend.prepared == 2 && backend.flushed == 2);
}}
""")

    def test_vulkan_frame_waits_for_claimed_transfer_semaphores(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "VKWrapper.cpp").read_text(encoding="utf-8")
        start = source.index("    void VK_API::flushWindow( DisplayWindow& window )")
        end = source.index("    bool VK_API::frameStarted()", start)
        function = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
#define SCOPE_EXIT if (false)
#define LOCALE_STR(x) x
#define DIVIDE_UNEXPECTED_CALL() assert(false)
#define DIVIDE_ASSERT(condition, ...) (condition)
using VkDevice = int;
using VkSemaphore = int;
using VkResult = int;
using Mutex = std::mutex;
template<typename T> using LockGuard = std::lock_guard<T>;
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_OUT_OF_DATE_KHR = -1, VK_SUBOPTIMAL_KHR = 1;
struct SemaphoreList : std::vector<VkSemaphore> {{
    using std::vector<VkSemaphore>::vector;
    using std::vector<VkSemaphore>::operator=;
    void reset_lose_memory() {{ clear(); }}
}};
struct VKSubmitSempahore {{ using Container = SemaphoreList; }};
struct DisplayWindow {{
    int id;
    int getGUID() const {{ return id; }}
}};
struct SwapChain {{
    std::vector<SemaphoreList> submissions;
    std::function<void()> duringSubmit;
    VkResult endFrame(const SemaphoreList& waits) {{
        submissions.push_back(waits);
        if (duringSubmit) duringSubmit();
        return VK_SUCCESS;
    }}
}};
struct VKPerWindowState {{
    DisplayWindow* _window = nullptr;
    SwapChain* _swapChain = nullptr;
    bool _skipEndFrame = false;
    bool _frameInProgress = false;
    bool _recreateAfterPresent = false;
    bool _needsRecreate = false;
    struct ActiveState {{ int unused = 0; }} _activeState;
}};
struct StateTracker {{ VKPerWindowState* _activeWindow = nullptr; }};
struct SubmitSemaphores {{ Mutex _lock; SemaphoreList _pendingSubmitSemaphores; }};
struct Console {{
    template<typename... Args> static void errorfn(Args...) {{ assert(false); }}
}};
std::string VKErrorString(VkResult) {{ return "error"; }}
struct VK_API {{
    static SubmitSemaphores s_submitSemaphores;
    static StateTracker tracker;
    static std::vector<VkSemaphore> retired;
    static int transfers;
    std::map<int, VKPerWindowState> _perWindowState;
    static StateTracker& GetStateTracker() {{ return tracker; }}
    static int GetCurrentCommandBuffer() {{ return 1; }}
    static void FlushBufferTransferRequests(int) {{ ++transfers; }}
    static void RegisterCustomAPIDelete(std::function<void(VkDevice)> callback, bool transient) {{
        assert(transient); callback(1);
    }}
    bool recreateSwapChain(VKPerWindowState&) {{ assert(false); return false; }}
    void flushWindow(DisplayWindow&);
}};
SubmitSemaphores VK_API::s_submitSemaphores;
StateTracker VK_API::tracker;
std::vector<VkSemaphore> VK_API::retired;
int VK_API::transfers = 0;
void vkDestroySemaphore(VkDevice, VkSemaphore semaphore, void*) {{ VK_API::retired.push_back(semaphore); }}
{function}
int main() {{
    VK_API api;
    DisplayWindow skipped{{1}}, first{{2}}, second{{3}};
    SwapChain firstChain, secondChain;
    api._perWindowState[1] = {{&skipped, &firstChain, true, false}};
    api._perWindowState[2] = {{&first, &firstChain, false, true}};
    api._perWindowState[3] = {{&second, &secondChain, false, true}};
    auto& pending = VK_API::s_submitSemaphores._pendingSubmitSemaphores;
    pending = {{11, 12}};

    api.flushWindow(skipped);
    assert(!api._perWindowState[1]._skipEndFrame);
    assert((pending == SemaphoreList{{11, 12}}));
    assert(VK_API::transfers == 0 && VK_API::retired.empty());

    firstChain.duringSubmit = [&pending] {{ pending.push_back(13); }};
    api.flushWindow(first);
    assert((firstChain.submissions[0] == SemaphoreList{{11, 12}}));
    assert((pending == SemaphoreList{{13}}));
    assert((VK_API::retired == std::vector<VkSemaphore>{{11, 12}}));

    api.flushWindow(second);
    assert((secondChain.submissions[0] == SemaphoreList{{13}}));
    assert(pending.empty());
    assert((VK_API::retired == std::vector<VkSemaphore>{{11, 12, 13}}));
    assert(VK_API::transfers == 2);
}}
""")

    def test_nested_windows_resume_the_existing_or_skipped_frame(self):
        source = (VIDEO / "RenderBackend" / "Vulkan" / "VKWrapper.cpp").read_text(encoding="utf-8")
        start = source.index("    bool VK_API::drawToWindow( DisplayWindow& window )")
        end = source.index("    bool VK_API::frameStarted()", start)
        functions = source[start:end]
        run_cpp(self, f"""
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define PROFILE_SCOPE_AUTO(...)
#define LOCALE_STR(x) x
#define DIVIDE_UNEXPECTED_CALL() assert(false)
#define DIVIDE_ASSERT(condition, ...) (condition)
struct ScopeExit {{
    std::function<void()> callback;
    ~ScopeExit() {{ callback(); }}
}};
struct ScopeExitMaker {{}};
template<typename F> ScopeExit operator+(ScopeExitMaker, F&& callback) {{ return {{callback}}; }}
#define SCOPE_EXIT auto scopeExit = ScopeExitMaker{{}} + [&]()
using U16 = std::uint16_t;
using VkDevice = int;
using VkSemaphore = int;
using VkResult = int;
using Mutex = std::mutex;
template<typename T> using LockGuard = std::lock_guard<T>;
template<typename T> struct vec2 {{ T width, height; }};
struct VkExtent2D {{ unsigned width, height; }};
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_OUT_OF_DATE_KHR = -1, VK_SUBOPTIMAL_KHR = 1;
struct SemaphoreList : std::vector<VkSemaphore> {{ void reset_lose_memory() {{ clear(); }} }};
struct VKSubmitSempahore {{ using Container = SemaphoreList; }};
struct DisplayWindow {{
    int id;
    vec2<U16> size{{800, 600}};
    int getGUID() const {{ return id; }}
    vec2<U16> getDrawableSize() const {{ return size; }}
}};
struct SwapChain {{
    VkExtent2D extent{{800, 600}};
    VkResult nextAcquire = VK_SUCCESS;
    unsigned begins = 0, ends = 0;
    VkExtent2D surfaceExtent() const {{ return extent; }}
    VkResult beginFrame() {{ ++begins; return nextAcquire; }}
    VkResult endFrame(const SemaphoreList&) {{ ++ends; return VK_SUCCESS; }}
}};
struct VKPerWindowState {{
    DisplayWindow* _window = nullptr;
    SwapChain* _swapChain = nullptr;
    bool _skipEndFrame = false;
    bool _frameInProgress = false;
    bool _recreateAfterPresent = false;
    bool _needsRecreate = false;
    struct ActiveState {{ int unused = 0; }} _activeState;
}};
struct StateTracker {{ VKPerWindowState* _activeWindow = nullptr; }};
struct SubmitSemaphores {{ Mutex _lock; SemaphoreList _pendingSubmitSemaphores; }};
struct Console {{ template<typename... Args> static void errorfn(Args...) {{ assert(false); }} }};
std::string VKErrorString(VkResult) {{ return "error"; }}
void vkDestroySemaphore(VkDevice, VkSemaphore, void*) {{}}
struct VK_API {{
    static SubmitSemaphores s_submitSemaphores;
    static StateTracker tracker;
    std::map<int, VKPerWindowState> _perWindowState;
    unsigned recreations = 0;
    bool recreateSucceeds = true;
    static StateTracker& GetStateTracker() {{ return tracker; }}
    static int GetCurrentCommandBuffer() {{ return 1; }}
    static void FlushBufferTransferRequests(int) {{}}
    static void RegisterCustomAPIDelete(std::function<void(VkDevice)>, bool) {{}}
    void initStatePerWindow(VKPerWindowState&) {{ assert(false); }}
    bool recreateSwapChain(VKPerWindowState&) {{ ++recreations; return recreateSucceeds; }}
    bool drawToWindow(DisplayWindow&);
    void onRenderThreadLoopStart();
    void onRenderThreadLoopEnd();
    bool prepareFlushWindow(DisplayWindow&);
    void flushWindow(DisplayWindow&);
}};
SubmitSemaphores VK_API::s_submitSemaphores;
StateTracker VK_API::tracker;
{functions}
int main() {{
    VK_API api;
    DisplayWindow parent{{1}}, child{{2}};
    SwapChain parentChain, childChain;
    api._perWindowState[1] = {{&parent, &parentChain}};
    api._perWindowState[2] = {{&child, &childChain}};

    assert(api.drawToWindow(parent));
    assert(api.drawToWindow(child));
    assert(api.prepareFlushWindow(child));
    api.flushWindow(child);
    assert(VK_API::tracker._activeWindow == nullptr);
    assert(api.drawToWindow(parent));
    assert(parentChain.begins == 1 && childChain.begins == 1 && childChain.ends == 1);
    assert(api.prepareFlushWindow(parent));
    api.flushWindow(parent);
    assert(parentChain.ends == 1 && !api._perWindowState[1]._frameInProgress);

    parentChain.nextAcquire = VK_ERROR_OUT_OF_DATE_KHR;
    assert(!api.drawToWindow(parent));
    assert(api._perWindowState[1]._skipEndFrame && api.recreations == 1);
    assert(api.drawToWindow(child));
    api.flushWindow(child);
    parentChain.nextAcquire = VK_SUCCESS;
    assert(!api.drawToWindow(parent));
    assert(parentChain.begins == 2 && !api.prepareFlushWindow(parent));
    api.flushWindow(parent);
    assert(!api._perWindowState[1]._skipEndFrame && parentChain.ends == 1);

    assert(api.drawToWindow(parent));
    assert(parentChain.begins == 3 && api.prepareFlushWindow(parent));
    api.flushWindow(parent);
    assert(parentChain.ends == 2 && VK_API::tracker._activeWindow == nullptr);

    parentChain.nextAcquire = VK_SUBOPTIMAL_KHR;
    assert(api.drawToWindow(parent));
    assert(api._perWindowState[1]._recreateAfterPresent && api.recreations == 1);
    assert(api.drawToWindow(child));
    api.flushWindow(child);
    assert(api.drawToWindow(parent));
    assert(parentChain.begins == 4);
    api.flushWindow(parent);
    assert(parentChain.ends == 3 && api.recreations == 2);
    assert(!api._perWindowState[1]._recreateAfterPresent);

    api.recreateSucceeds = false;
    assert(api.drawToWindow(parent));
    api.flushWindow(parent);
    assert(api.recreations == 3 && api._perWindowState[1]._needsRecreate);
    parentChain.nextAcquire = VK_SUCCESS;
    assert(!api.drawToWindow(parent));
    assert(parentChain.begins == 5 && api._perWindowState[1]._skipEndFrame);
    api.flushWindow(parent);
    api.recreateSucceeds = true;
    assert(api.drawToWindow(parent));
    assert(parentChain.begins == 6 && !api._perWindowState[1]._needsRecreate);
    api.flushWindow(parent);
}}
""")


if __name__ == "__main__":
    unittest.main()
