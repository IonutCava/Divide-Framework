#include "stdafx.h"

#if defined(_WIN32)

#if defined(_DEBUG)
//#include <float.h>
//unsigned int fp_control_state = _controlfp(_EM_INEXACT, _MCW_EM);
#endif

#include "Headers/PlatformDefinesWindows.h"
#include "Core/Headers/StringHelper.h"

#include <direct.h>
#include <ShellScalingAPI.h>
#include <comdef.h>

#pragma comment(lib, "OpenAL32.lib")
#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "nvtt.lib")
#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2_mixer.lib")
#pragma comment(lib, "PhysX_64.lib")
#pragma comment(lib, "PhysXCooking_64.lib")
#pragma comment(lib, "PhysXFoundation_64.lib")
#pragma comment(lib, "PhysXPvdSDK_static_64.lib")
#pragma comment(lib, "PhysXExtensions_static_64.lib")
#pragma comment(lib, "vulkan-1.lib")

#ifdef _DEBUG
#pragma comment(lib, "SPIRVd.lib")
#pragma comment(lib, "SPVRemapperd.lib")
#pragma comment(lib, "glslangd.lib")
#pragma comment(lib, "MachineIndependentd.lib")
#pragma comment(lib, "GenericCodeGend.lib")
#pragma comment(lib, "OSDependentd.lib")
#pragma comment(lib, "OGLCompilerd.lib")
#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "glbindingd.lib")
#pragma comment(lib, "glbinding-auxd.lib")
#pragma comment(lib, "assimp-vc142-mtd.lib")
#pragma comment(lib, "IL_d.lib")
#pragma comment(lib, "ILU_d.lib")
#pragma comment(lib, "libpng_d.lib")
#pragma comment(lib, "jpeg_d.lib")
#pragma comment(lib, "libmng_d.lib")
#pragma comment(lib, "zlib_d.lib")
#pragma comment(lib, "freetype_d.lib")
#pragma comment(lib, "FreeImage_d.lib")
#pragma comment(lib, "CEGUIBase-0_d.lib")
#pragma comment(lib, "CEGUICommonDialogs-0_d.lib")
#pragma comment(lib, "CEGUICoreWindowRendererSet_d.lib")
#pragma comment(lib, "CEGUILuaScriptModule-0_d.lib")
#pragma comment(lib, "CEGUISTBImageCodec_d.lib")
#pragma comment(lib, "CEGUITinyXMLParser_d.lib")
#else  //_DEBUG
#pragma comment(lib, "SPIRV.lib")
#pragma comment(lib, "SPVRemapper.lib")
#pragma comment(lib, "glslang.lib")
#pragma comment(lib, "MachineIndependent.lib")
#pragma comment(lib, "GenericCodeGen.lib")
#pragma comment(lib, "OSDependent.lib")
#pragma comment(lib, "OGLCompiler.lib")
#pragma comment(lib, "glbinding.lib")
#pragma comment(lib, "glbinding-aux.lib")
#pragma comment(lib, "assimp-vc142-mt.lib")
#pragma comment(lib, "IL.lib")
#pragma comment(lib, "ILU.lib")
#pragma comment(lib, "libpng.lib")
#pragma comment(lib, "jpeg.lib")
#pragma comment(lib, "libmng.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "freetype.lib")
#pragma comment(lib, "FreeImage.lib")
#pragma comment(lib, "CEGUIBase-0.lib")
#pragma comment(lib, "CEGUICommonDialogs-0.lib")
#pragma comment(lib, "CEGUICoreWindowRendererSet.lib")
#pragma comment(lib, "CEGUILuaScriptModule-0.lib")
#pragma comment(lib, "CEGUISTBImageCodec.lib")
#pragma comment(lib, "CEGUITinyXMLParser.lib")
#endif  //_DEBUG

#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
// SDL redefines WIN32_LEAN_AND_MEAN
#include <SDL_syswm.h>
#endif

extern "C" {
    _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    _declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}

void* malloc_aligned(const size_t size, const size_t alignment, const size_t offset) noexcept {
    if (offset > 0u) {
        return _aligned_offset_malloc(size, alignment, offset);
    }
    return _aligned_malloc(size, alignment);
}

void  free_aligned(void*& ptr) noexcept {
    _aligned_free(ptr);
}

LRESULT DlgProc([[maybe_unused]] HWND hWnd, [[maybe_unused]] UINT uMsg, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam) noexcept {
    return FALSE;
}

namespace Divide {
    //https://msdn.microsoft.com/en-us/library/windows/desktop/ms679360%28v=vs.85%29.aspx
    static CHAR * GetLastErrorText(CHAR *pBuf, const LONG bufSize) noexcept {
        LPTSTR pTemp = nullptr;

        if (bufSize < 16) {
            if (bufSize > 0) {
                pBuf[0] = '\0';
            }
            return(pBuf);
        }

        const DWORD retSize = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                            nullptr,
                                            GetLastError(),
                                            LANG_NEUTRAL,
                                            reinterpret_cast<LPTSTR>(&pTemp),
                                            0,
                                            nullptr);

        if (!retSize || pTemp == nullptr) {
            pBuf[0] = '\0';
        } else {
            pTemp[strlen(pTemp) - 2] = '\0'; //remove cr and newline character
            sprintf(pBuf, "%0.*s (0x%x)", bufSize - 16, pTemp, GetLastError());
            LocalFree(static_cast<HLOCAL>(pTemp));
        }
        return(pBuf);
    }

    F32 PlatformDefaultDPI() noexcept {
        return 96.f;
    }

    void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept {
        SDL_SysWMinfo wmInfo = {};
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);
        handleOut._handle = wmInfo.info.win.window;
    }

    bool DebugBreak(const bool condition) noexcept {
        if (condition && IsDebuggerPresent()) {
            __debugbreak();
            return true;
        }
        return false;
    }

    ErrorCode PlatformInitImpl([[maybe_unused]] int argc, [[maybe_unused]] char** argv) noexcept {
        const HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        if (FAILED(hr)) {
            const _com_error err(hr);
            std::cerr << "SetProcessDpiAwareness: " << err.ErrorMessage() << std::endl;
        }

        return ErrorCode::NO_ERR;
    }

    bool PlatformCloseImpl() noexcept {
        return true;
    }

    bool GetAvailableMemory(SysInfo& info) {
        MEMORYSTATUSEX status; 
        status.dwLength = sizeof(status);
        const BOOL infoStatus = GlobalMemoryStatusEx(&status);
        if (infoStatus != FALSE) {
            info._availableRamInBytes = status.ullAvailPhys;
            return true;
        } else {
            CHAR msgText[256];
            GetLastErrorText(msgText,sizeof(msgText));
            std::cerr << msgText << std::endl;
        }
        return false;
    }


    constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO
    {
        DWORD  dwType; // Must be 0x1000.
        LPCSTR szName; // Pointer to name (in user addr space).
        DWORD  dwThreadID; // Thread ID (-1=caller thread).
        DWORD  dwFlags; // Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)

    void SetThreadPriorityInternal(HANDLE thread, const ThreadPriority priority) {
        if (priority == ThreadPriority::COUNT) {
            return;
        }

        switch (priority) {
            case ThreadPriority::IDLE: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_IDLE);
            } break;
            case ThreadPriority::BELOW_NORMAL: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            } break;
            case ThreadPriority::NORMAL: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
            } break;
            case ThreadPriority::ABOVE_NORMAL: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
            } break;
            case ThreadPriority::HIGHEST: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
            } break;
            case ThreadPriority::TIME_CRITICAL: {
                ::SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
            } break;
        }
    }

    void SetThreadPriority(const ThreadPriority priority) {
        SetThreadPriorityInternal(GetCurrentThread(), priority);
    }

    extern void SetThreadPriority(std::thread* thread, ThreadPriority priority) {
        SetThreadPriorityInternal(static_cast<HANDLE>(thread->native_handle()), priority);
    }

    void SetThreadName(const U32 threadID, const char* threadName) noexcept {
        // DWORD dwThreadID = ::GetThreadId( static_cast<HANDLE>( t.native_handle() ) );

        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = threadName;
        info.dwThreadID = threadID;
        info.dwFlags = 0;

        __try
        {
            RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            DebugBreak();
        }
    }

    void SetThreadName(const char* threadName) noexcept {
        SetThreadName(GetCurrentThreadId(), threadName);
    }

    void SetThreadName(std::thread* thread, const char* threadName) {
        const DWORD threadId = GetThreadId(static_cast<HANDLE>(thread->native_handle()));
        SetThreadName(threadId, threadName);
    }

    bool CallSystemCmd(const char* cmd, const char* args) {
        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        const string commandLine = Util::StringFormat("\"%s\" %s", cmd, args);
        char* lpCommandLine = const_cast<char*>(commandLine.c_str());

        const BOOL ret = CreateProcess(nullptr, lpCommandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return ret == TRUE;
    }
}; //namespace Divide

#endif //defined(_WIN32)