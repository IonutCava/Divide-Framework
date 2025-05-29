

#if defined(__APPLE_CC__)

#include "Headers/PlatformDefinesApple.h"

#include <Carbon/Carbon.h>
#include <SDL_syswm.h>
#include <signal.h>

namespace Divide {

    bool DebugBreak(const bool condition) noexcept {
        if (!condition) {
            return false;
        }
#if defined(SIGTRAP)
        raise(SIGTRAP);
#else
        raise(SIGABRT);
#endif

        return true;
    }

    void EnforceDPIScaling() noexcept
    {
        NOP();
    }

    bool GetAvailableMemory(SysInfo& info) {
        I32 mib[2] = { CTL_HW, HW_MEMSIZE };
        U32 namelen = sizeof(mib) / sizeof(mib[0]);
        U64 size;
        size_t len = sizeof(size);
        if (sysctl(mib, namelen, &size, &len, NULL, 0) < 0) {
            perror("sysctl");
        } else {
            info._availableRamInBytes = to_size(size);
        }

        return true;
    }

    F32 PlatformDefaultDPI() noexcept {
        return 72.f;
    }

    void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);

        handleOut._handle = wmInfo.info.cocoa.window;
    }

    void SetThreadPriorityInternal(pthread_t thread, const ThreadPriority priority) {
        if (priority == ThreadPriority::COUNT) {
            return;
        }
        sched_param sch_params;
        int policy;
        pthread_getschedparam(thread, &policy, &sch_params);

        switch (priority) {
        case ThreadPriority::IDLE: {
            sch_params.sched_priority = 10;
        } break;
        case ThreadPriority::BELOW_NORMAL: {
            sch_params.sched_priority = 25;
        } break;
        case ThreadPriority::NORMAL: {
            sch_params.sched_priority = 50;
            } break;
        case ThreadPriority::ABOVE_NORMAL: {
            sch_params.sched_priority = 75;
        } break;
        case ThreadPriority::HIGHEST: {
            sch_params.sched_priority = 85;
        } break;
        case ThreadPriority::TIME_CRITICAL: {
            sch_params.sched_priority = 99;
        } break;
        }

        DIVIDE_EXPECTED_CALL( pthread_setschedparam(thread, SCHED_FIFO, &sch_params) );
    }

    void SetThreadPriority(const ThreadPriority priority) {
        SetThreadPriorityInternal(pthread_self(), priority);
    }

    #include <sys/prctl.h>
    void SetThreadName(const std::string_view threadName) noexcept {
        pthread_setname_np(pthread_self(), threadName.data());
    }

    bool CallSystemCmd(const char* cmd, const std::string_view args) {
        return std::system(Util::StringFormat("{} {}", cmd, args).c_str()) == 0;
    }
}; //namespace Divide

#endif //defined(__APPLE_CC__)
