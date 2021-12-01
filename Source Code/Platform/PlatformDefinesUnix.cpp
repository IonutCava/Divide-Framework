#include "stdafx.h"

#if !defined(_WIN32) && !defined(__APPLE_CC__)

#include "Headers/PlatformDefinesUnix.h"

#include <SDL_syswm.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h

void* malloc_aligned(const size_t size, size_t alignment, size_t offset) {
    (void)offset;
    return _mm_malloc(size, alignment);
}

void  free_aligned(void*& ptr) {
    _mm_free(ptr);
}

int _vscprintf (const char * format, va_list pargs) {
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}

namespace Divide {

    void DebugBreak(const bool condition) noexcept {
        if (!condition) {
            return;
        }
#if defined(SIGTRAP)
        raise(SIGTRAP)
#else
        raise(SIGABRT)
#endif
    }

    ErrorCode PlatformInitImpl(int argc, char** argv) noexcept {
        return ErrorCode::NO_ERR;
    }

    bool PlatformCloseImpl() noexcept {
        return true;
    }

    bool GetAvailableMemory(SysInfo& info) {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGESIZE);
        info._availableRamInBytes = pages * page_size;
        return true;
    }

    F32 PlatformDefaultDPI() noexcept {
        return 96.f;
    }

    void getWindowHandle(void* window, WindowHandle& handleOut) noexcept {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);

        handleOut._handle = wmInfo.info.x11.window;
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

#endif //defined(_UNIX)
