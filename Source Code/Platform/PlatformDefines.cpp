#include "stdafx.h"

#include "config.h"

#include "Headers/PlatformDefines.h"
#include "Headers/PlatformRuntime.h"

#include "GUI/Headers/GUI.h"

#include "Utility/Headers/Localization.h"
#include "Utility/Headers/MemoryTracker.h"

#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

namespace {
    SysInfo g_sysInfo;
};

namespace MemoryManager {
void log_new(void* p, const size_t size, const char* zFile, const size_t nLine) {
    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        if (MemoryTracker::Ready) {
             AllocTracer.Add( p, size, zFile, nLine );
        }
    }
}

void log_delete(void* p) {
    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        if (MemoryTracker::Ready) {
            AllocTracer.Remove( p );
        }
    }
}
};  // namespace MemoryManager

namespace Assert {

    bool DIVIDE_ASSERT_FUNC(const bool expression, [[maybe_unused]] const char* file, [[maybe_unused]] const int line, [[maybe_unused]] const char* failMessage) noexcept {
        if_constexpr(!Config::Build::IS_SHIPPING_BUILD) {
            if (!expression) {
                const char* msgOut = FormatText("[ %s ] [ %s ] AT [ %d ]", failMessage, file, line);
                if (strlen(msgOut) == 0) {
                    return DIVIDE_ASSERT_FUNC(expression, file, line, "Message truncated");
                }

                if_constexpr(Config::Assert::LOG_ASSERTS) {
                    Console::errorfn(failMessage);
                }

                Console::flush();

                DIVIDE_ASSERT_MSG_BOX(msgOut);

                if_constexpr(!Config::Assert::CONTINUE_ON_ASSERT) {
                    assert(expression && msgOut);
                }

                DebugBreak();
            }
        }

        return expression;
    }
}; // namespace Assert

SysInfo::SysInfo() noexcept : _availableRamInBytes(0),
                     _systemResolutionWidth(0),
                     _systemResolutionHeight(0)
{
}

SysInfo& sysInfo() noexcept {
    return g_sysInfo;
}

const SysInfo& const_sysInfo() noexcept {
    return g_sysInfo;
}


ErrorCode PlatformPreInit(const int argc, char** argv) {
    InitSysInfo(sysInfo(), argc, argv);
    return ErrorCode::NO_ERR;
}

ErrorCode PlatformPostInit(const int argc, char** argv) {
    Runtime::mainThreadID(std::this_thread::get_id());
    SeedRandom();
    Paths::initPaths(sysInfo());

    ErrorCode err = ErrorCode::WRONG_WORKING_DIRECTORY;
    if (pathExists(Paths::g_rootPath + Paths::g_assetsLocation)) {
        // Read language table
        err = Locale::Init();
        if (err == ErrorCode::NO_ERR) {
            Console::start();
            // Print a copyright notice in the log file
            if (!Util::FindCommandLineArgument(argc, argv, "disableCopyright")) {
                Console::printCopyrightNotice();
            }
            Console::toggleTextDecoration(true);
        }
    }

    return err;
}

ErrorCode PlatformInit(const int argc, char** argv) {
    ErrorCode err = PlatformPreInit(argc, argv);
    if (err == ErrorCode::NO_ERR) {
        err = PlatformInitImpl(argc, argv);
        if (err == ErrorCode::NO_ERR) {
            err = PlatformPostInit(argc, argv);
        }
    }

    return err;
}

bool PlatformClose() {
    Runtime::resetMainThreadID();
    if (PlatformCloseImpl()) {
        Console::stop();
        Locale::Clear();
        return true;
    }

    return false;
}

void InitSysInfo(SysInfo& info, [[maybe_unused]] const I32 argc, [[maybe_unused]] char** argv) {
    if (!GetAvailableMemory(info)) {
        DebugBreak();
        // Assume 256Megs as a minimum
        info._availableRamInBytes = (1 << 8) * 1024 * 1024;
    }
    info._workingDirectory = getWorkingDirectory();
    info._workingDirectory.append("/");
}

U32 HardwareThreadCount() noexcept {
    return std::max(std::thread::hardware_concurrency(), 2u);
}

bool CreateDirectories(const ResourcePath& path) {
    return CreateDirectories(path.c_str());
}

bool CreateDirectories(const char* path) {
    static Mutex s_DirectoryLock;

    ScopedLock<Mutex> w_lock(s_DirectoryLock);
    assert(path != nullptr && strlen(path) > 0);
    //Always end in a '/'
    assert(path[strlen(path) - 1] == '/');

    vector<string> directories;
    Util::Split<vector<string>, string>(path, '/', directories);
    if (directories.empty()) {
        Util::Split<vector<string>, string>(path, '\\', directories);
    }

    string previousPath = "./";
    for (const string& dir : directories) {
        if (!createDirectory((previousPath + dir).c_str())) {
            return false;
        }
        previousPath += dir;
        previousPath += "/";
    }

    return true;
}

FileAndPath GetInstallLocation(char* argv0) {
    if (argv0 == nullptr || argv0[0] == 0) {
        return {};
    }

    return splitPathToNameAndLocation(extractFilePathAndName(argv0).c_str());
}

const char* GetClipboardText([[maybe_unused]] void* user_data) noexcept
{
    return SDL_GetClipboardText();
}

void SetClipboardText([[maybe_unused]] void* user_data, const char* text) noexcept
{
    SDL_SetClipboardText(text);
}

void ToggleCursor(const bool state) noexcept
{
    if (CursorState() != state) {
        SDL_ShowCursor(state ? SDL_TRUE : SDL_FALSE);
    }
}

bool CursorState() noexcept
{
    return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
}

};  // namespace Divide

void* operator new(const size_t size, [[maybe_unused]] const char* zFile, [[maybe_unused]] const size_t nLine)
#if !defined(_DEBUG)
noexcept
#endif //!_DEBUG
{
    void* ptr = malloc(size);
#if defined(_DEBUG)
    Divide::MemoryManager::log_new(ptr, size, zFile, nLine);
#endif
    return ptr;
}

void operator delete(void* ptr, [[maybe_unused]] const char* zFile, [[maybe_unused]] size_t nLine) {
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete(ptr);
#endif

    free(ptr);
}

void* operator new[](const size_t size, [[maybe_unused]] const char* zFile, [[maybe_unused]] const size_t nLine) noexcept {
    void* ptr = malloc(size);
#if defined(_DEBUG)
    Divide::MemoryManager::log_new(ptr, size, zFile, nLine);
#endif
    return ptr;
}

void operator delete[](void* ptr, [[maybe_unused]] const char* zFile, [[maybe_unused]] size_t nLine) {
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete(ptr);
#endif
    free(ptr);
}
