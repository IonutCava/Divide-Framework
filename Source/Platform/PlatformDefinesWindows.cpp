

#if defined(_WIN32)

#if defined(_DEBUG)
//#include <float.h>
//unsigned int fp_control_state = _controlfp(_EM_INEXACT, _MCW_EM);
#endif

#include "Headers/PlatformDefinesWindows.h"
#include "Core/Headers/StringHelper.h"
#include <ShellScalingApi.h>
#include <comdef.h>

#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
// SDL redefines WIN32_LEAN_AND_MEAN
#include <SDL2/SDL_syswm.h>
#endif

#include <iostream>

#if defined(ENABLE_MIMALLOC)
#if defined(MIMALLOC_OVERRIDE_NEW_DELETE)
#include <mimalloc-new-delete.h>
#endif //MIMALLOC_OVERRIDE_NEW_DELETE
#endif //ENABLE_MIMALLOC

DISABLE_NON_MSVC_WARNING_PUSH("missing-variable-declarations")
extern "C"
{
    _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    _declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}
DISABLE_NON_MSVC_WARNING_POP()

LRESULT DlgProc([[maybe_unused]] HWND hWnd, [[maybe_unused]] UINT uMsg, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam) noexcept
{
    return FALSE;
}

namespace Divide {
    //https://msdn.microsoft.com/en-us/library/windows/desktop/ms679360%28v=vs.85%29.aspx
    static std::string GetLastErrorText() noexcept
    {
        //Get the error message ID, if any.
        DWORD errorMessageID = ::GetLastError();
        if ( errorMessageID == 0 )
        {
            return std::string(); //No error message has been recorded
        }

        LPSTR messageBuffer = nullptr;

        //Ask Win32 to give us the string version of that message ID.
        //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
        size_t size = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      NULL, errorMessageID, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPSTR)&messageBuffer, 0, NULL );

        //Copy the error message into a std::string.
        std::string message( messageBuffer, size );

        //Free the Win32's string's buffer.
        LocalFree( messageBuffer );

        return message;
    }

    F32 PlatformDefaultDPI() noexcept
    {
        return 96.f;
    }

    void GetWindowHandle(void* window, WindowHandle& handleOut) noexcept
    {
        SDL_SysWMinfo wmInfo = {};
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window), &wmInfo);
        handleOut._handle = wmInfo.info.win.window;
    }

    bool DebugBreak(const bool condition) noexcept
    {
        if (condition && IsDebuggerPresent())
        {
            __debugbreak();
            return true;
        }

        return false;
    }

    void EnforceDPIScaling() noexcept
    {
        if (FAILED( SetProcessDpiAwareness( PROCESS_PER_MONITOR_DPI_AWARE ) ))
        {
            std::cerr << GetLastErrorText();
        }
    }

    bool GetAvailableMemory(SysInfo& info)
    {
        MEMORYSTATUSEX status; 
        status.dwLength = sizeof(status);
        const BOOL infoStatus = GlobalMemoryStatusEx(&status);

        if (infoStatus != FALSE)
        {
            info._availableRamInBytes = status.ullAvailPhys;
            return true;
        }
        else
        {
            Console::errorfn( GetLastErrorText().c_str() );
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

    static void SetThreadPriorityInternal(HANDLE thread, const ThreadPriority priority)
    {
        if (priority == ThreadPriority::COUNT)
        {
            return;
        }

        switch (priority)
        {
            case ThreadPriority::IDLE:          ::SetThreadPriority(thread, THREAD_PRIORITY_IDLE);          break;
            case ThreadPriority::BELOW_NORMAL:  ::SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);  break;
            case ThreadPriority::NORMAL:        ::SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);        break;
            case ThreadPriority::ABOVE_NORMAL:  ::SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);  break;
            case ThreadPriority::HIGHEST:       ::SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);       break;
            case ThreadPriority::TIME_CRITICAL: ::SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL); break;

            default:
            case ThreadPriority::COUNT:          DIVIDE_UNEXPECTED_CALL();                                  break;
        }
    }

    void SetThreadPriority(const ThreadPriority priority)
    {
        SetThreadPriorityInternal(GetCurrentThread(), priority);
    }

    static void SetThreadName(const U32 threadID, const std::string_view threadName) noexcept
    {
        // DWORD dwThreadID = ::GetThreadId( static_cast<HANDLE>( t.native_handle() ) );

        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = threadName.data();
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

    void SetThreadName(const std::string_view threadName) noexcept
    {
        SetThreadName(GetCurrentThreadId(), threadName);
    }

    bool CallSystemCmd(const std::string_view cmd, const std::string_view args)
    {
        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        const string commandLine = Util::StringFormat("\"{}\" {}", cmd, args);
        char* lpCommandLine = const_cast<char*>(commandLine.c_str());

        const BOOL ret = CreateProcess(nullptr, lpCommandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return ret == TRUE;
    }
}; //namespace Divide

#endif //defined(_WIN32)
