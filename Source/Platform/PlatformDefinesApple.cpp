

#if defined(__APPLE_CC__)

#include "Headers/PlatformDefinesApple.h"

#include <SDL_syswm.h>
#include <signal.h>

void* malloc_aligned(const size_t size, size_t alignment, size_t offset) {
    (void)offset;
    return _mm_malloc(size, alignment);
}

void  free_aligned(void*& ptr) {
    _mm_free(ptr);
}

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

        if (!pthread_setschedparam(thread, SCHED_FIFO, &sch_params)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    void SetThreadPriority(const ThreadPriority priority) {
        SetThreadPriorityInternal(pthread_self(), priority);
    }

    extern void SetThreadPriority(std::thread* thread, ThreadPriority priority) {
        SetThreadPriorityInternal(static_cast<pthread_t>(thread->native_handle()), priority);
    }

    void SetThreadName(std::thread* thread, const char* threadName) noexcept {
        auto handle = thread->native_handle();
        pthread_setname_np(handle, threadName);
    }

    #include <sys/prctl.h>
    void SetThreadName(const char* threadName) noexcept {
        prctl(PR_SET_NAME, threadName, 0, 0, 0);
    }

    bool CallSystemCmd(const char* cmd, const char* args) {
        return std::system(Util::StringFormat("%s %s", cmd, args).c_str()) == 0;
    }
}; //namespace Divide

#endif //defined(__APPLE_CC__)
