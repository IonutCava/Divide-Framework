#if defined(__linux__)

#include "Headers/PlatformDefinesUnix.h"

#include <SDL2/SDL_syswm.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>

#if defined(HAS_WAYLAND_LIB)
#include <wayland-client.h>
#else
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
#error "SDL_VIDEO_DRIVER_WAYLAND is defined, but HAS_WAYLAND_LIB is not. Please ensure that the Wayland library is linked correctly."
#endif //SDL_VIDEO_DRIVER_WAYLAND
#endif //HAS_WAYLAND_LIB

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
#else
        raise(SIGABRT);
#endif
        return true;
    }

    void EnforceDPIScaling() noexcept
    {
        NOP();
    }

    bool GetAvailableMemory(SysInfo& info)
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGESIZE);

        if (pages == -1 || page_size == -1)
        {
            info._availableRamInBytes = 0;
            return false;
        }

        info._availableRamInBytes = pages * page_size;
        return true;
    }

    F32 PlatformDefaultDPI() noexcept
    {
        return 96.f;
    }

    void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept
    {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);

        handleOut._handle = nullptr;
        switch (wmInfo.subsystem) {
            case SDL_SYSWM_X11:
                handleOut.x11_window = wmInfo.info.x11.window;
                break;

            case SDL_SYSWM_WAYLAND:
#if defined(HAS_WAYLAND_LIB)
                handleOut.wl_display = wmInfo.info.wl.display;
                handleOut.wl_surface = wmInfo.info.wl.surface;
                break;
#endif //HAS_WAYLAND_LIB
            default:
                DIVIDE_UNEXPECTED_CALL();
                break;
        }
    }

    void SetThreadPriorityInternal(pthread_t thread, const ThreadPriority priority) {
        if (priority == ThreadPriority::COUNT) {
            return;
        }
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
        }

        if( pthread_setschedparam(thread, SCHED_FIFO, &sch_params) != 0)
        {
            Console::errofn(LOCALE_STR("ERROR_THREAD_PRIORITY"), thread, strerror(errno));
        }
    }

    void SetThreadPriority(const ThreadPriority priority)
    {
        SetThreadPriorityInternal(pthread_self(), priority);
    }

    void SetThreadName(const std::string_view threadName) noexcept
    {
        pthread_setname_np(pthread_self(), threadName.data());
    }

    bool CallSystemCmd(const std::string_view cmd, const std::string_view args)
    {
        return std::system(Util::StringFormat("{} {}", cmd, args).c_str()) == 0;
    }

}; //namespace Divide

#endif //defined(__linux__)
