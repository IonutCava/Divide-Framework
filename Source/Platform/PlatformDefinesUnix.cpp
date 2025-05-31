#if defined(IS_MACOS_BUILD) || defined(IS_LINUX_BUILD)

#include "Headers/PlatformDefinesUnix.h"

#include <SDL2/SDL_syswm.h>
#include <unistd.h>
#include <signal.h>
#include <unistd.h>
#include "Utility/Headers/Localization.h"

#if defined(IS_MACOS_BUILD)

#if !defined(__APPLE__)
#error "IS_MACOS_BUILD is defined, but __APPLE__ is not! Please check your build configuration."
#endif /!__APPLE__

#include <Carbon/Carbon.h>
#include <sys/sysctl.h>

#else //IS_MACOS_BUILD


#if defined(HAS_WAYLAND_LIB)
#include <wayland-client.h>
#else //HAS_WAYLAND_LIB
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
#error "SDL_VIDEO_DRIVER_WAYLAND is defined, but HAS_WAYLAND_LIB is not. Please ensure that the Wayland library is linked correctly."
#endif //SDL_VIDEO_DRIVER_WAYLAND
#endif //HAS_WAYLAND_LIB

#include <malloc.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#endif //IS_MACOS_BUILD

int _vscprintf (const char * format, va_list pargs)
{
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}

namespace Divide
{
    bool DebugBreak(const bool condition) noexcept
    {
        if (!condition)
        {
            return false;
        }
#if defined(SIGTRAP)
        raise(SIGTRAP);
#else //SIGTRAP
        raise(SIGABRT);
#endif //SIGTRAP
        return true;
    }

    void EnforceDPIScaling() noexcept
    {
        NOP();
    }

    bool GetAvailableMemory(SysInfo& info)
    {

        info._availableRamInBytes = 0u;
#if defined(IS_MACOS_BUILD)
        I32 mib[2] = { CTL_HW, HW_MEMSIZE };
        U32 namelen = sizeof(mib) / sizeof(mib[0]);
        U64 size;
        size_t len = sizeof(size);
        if (sysctl(mib, namelen, &size, &len, NULL, 0) < 0)
        {
            perror("sysctl");
            return false;
        }
        else
        {
            info._availableRamInBytes = to_size(size);
        }
#else //IS_MACOS_BUILD
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGESIZE);

        if (pages == -1 || page_size == -1)
        {
            return false;
        }

        info._availableRamInBytes = pages * page_size;

#endif //IS_MACOS_BUILD

        return true;
    }

    F32 PlatformDefaultDPI() noexcept
    {
#if defined(IS_MACOS_BUILD)
        return 72.f;
#else //IS_MACOS_BUILD
        return 96.f;
#endif //IS_MACOS_BUILD
    }

    void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept
    {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);

        handleOut = {};
#if defined(IS_MACOS_BUILD)
        handleOut._handle = wmInfo.info.cocoa.window;
#else //IS_MACOS_BUILD
        switch (wmInfo.subsystem)
        {
            case SDL_SYSWM_X11:
                handleOut.x11_window = wmInfo.info.x11.window;
                break;

            case SDL_SYSWM_WAYLAND:
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
                handleOut.wl_display = wmInfo.info.wl.display;
                handleOut.wl_surface = wmInfo.info.wl.surface;
                break;
#endif //SDL_VIDEO_DRIVER_WAYLAND
            default:
                DIVIDE_UNEXPECTED_CALL();
                break;
        }
#endif //IS_MACOS_BUILD
    }

    void SetThreadPriorityInternal(pthread_t thread, const ThreadPriority priority)
    {
        sched_param sch_params;
        int policy;
        pthread_getschedparam(thread, &policy, &sch_params);

        switch (priority) {
            default:
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
            case ThreadPriority::COUNT: 
                DIVIDE_UNEXPECTED_CALL();
                break;
        }

        if( pthread_setschedparam(thread, SCHED_FIFO, &sch_params) != 0)
        {
#if defined(IS_MACOS_BUILD)
            uint64_t threadId = 0;
            pthread_threadid_np(thread, &threadId);
#else //IS_MACOS_BUILD
            pid_t threadId = syscall(SYS_gettid);
#endif //IS_MACOS_BUILD
            Console::errorfn(LOCALE_STR("ERROR_THREAD_PRIORITY"), threadId, strerror(errno));
        }
    }

    void SetThreadPriority(const ThreadPriority priority)
    {
        SetThreadPriorityInternal(pthread_self(), priority);
    }

    void SetThreadName(const std::string_view threadName) noexcept
    {
#if defined(IS_MACOS_BUILD)
        pthread_setname_np(/*pthread_self(), */threadName.data());
#else //IS_MACOS_BUILD
        pthread_setname_np(pthread_self(), threadName.data());
#endif //IS_MACOS_BUILD
    }

    bool CallSystemCmd(const std::string_view cmd, const std::string_view args)
    {
        return std::system(Util::StringFormat("{} {}", cmd, args).c_str()) == 0;
    }

}; //namespace Divide

#endif //IS_MACOS_BUILD || IS_LINUX_BUILD