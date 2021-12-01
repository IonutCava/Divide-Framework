#include "stdafx.h"

#if defined(__APPLE_CC__)

#include "Headers/PlatformDefinesApple.h"

#include <SDL_syswm.h>
#include <signal.h

void* malloc_aligned(const size_t size, size_t alignment, size_t offset) {
    (void)offset;
    return _mm_malloc(size, alignment);
}

void  free_aligned(void*& ptr) {
    _mm_free(ptr);
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

    void getWindowHandle(void* window, WindowHandle& handleOut) noexcept {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);

        handleOut._handle = wmInfo.info.cocoa.window;
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
