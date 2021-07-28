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

SysInfo::SysInfo() : _availableRamInBytes(0),
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
    if (pathExists(Paths::g_exePath + Paths::g_assetsLocation)) {
        // Read language table
        err = Locale::Init();
        if (err == ErrorCode::NO_ERR) {
            Console::start();
            // Print a copyright notice in the log file
            if (!Util::FindCommandLineArgument(argc, argv, "disableCopyright")) {
                Console::printCopyrightNotice();
            }
            Console::toggleTimeStamps(true);
            Console::togglethreadID(true);
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

void InitSysInfo(SysInfo& info, const I32 argc, char** argv) {
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
    assert(path != nullptr && strlen(path) > 0);
    //Always end in a '/'
    assert(path[strlen(path) - 1] == '/');

    vectorEASTL<stringImpl> directories;
    Util::Split<vectorEASTL<stringImpl>, stringImpl>(path, '/', directories);
    if (directories.empty()) {
        Util::Split<vectorEASTL<stringImpl>, stringImpl>(path, '\\', directories);
    }

    stringImpl previousPath = "./";
    for (const stringImpl& dir : directories) {
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

const char* GetClipboardText(void* user_data) noexcept
{
    ACKNOWLEDGE_UNUSED(user_data);

    return SDL_GetClipboardText();
}

void SetClipboardText(void* user_data, const char* text) noexcept
{
    ACKNOWLEDGE_UNUSED(user_data);

    SDL_SetClipboardText(text);
}

void ToggleCursor(const bool state) noexcept
{
    SDL_ShowCursor(state ? SDL_TRUE : SDL_FALSE);
}

bool CursorState() noexcept
{
    return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
}

};  // namespace Divide

void* operator new(const size_t size, const char* zFile, const size_t nLine) {
    void* ptr = malloc(size);
#if defined(_DEBUG)
    Divide::MemoryManager::log_new(ptr, size, zFile, nLine);
#else
    ACKNOWLEDGE_UNUSED(zFile);
    ACKNOWLEDGE_UNUSED(nLine);
#endif
    return ptr;
}

void operator delete(void* ptr, const char* zFile, size_t nLine) {
    ACKNOWLEDGE_UNUSED(zFile);
    ACKNOWLEDGE_UNUSED(nLine);
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete(ptr);
#endif

    free(ptr);
}

void* operator new[](const size_t size, const char* zFile, const size_t nLine) {
    void* ptr = malloc(size);
#if defined(_DEBUG)
    Divide::MemoryManager::log_new(ptr, size, zFile, nLine);
#else
    ACKNOWLEDGE_UNUSED(zFile);
    ACKNOWLEDGE_UNUSED(nLine);
#endif
    return ptr;
}

void operator delete[](void* ptr, const char* zFile, size_t nLine) {
    ACKNOWLEDGE_UNUSED(zFile);
    ACKNOWLEDGE_UNUSED(nLine);
#if defined(_DEBUG)
    Divide::MemoryManager::log_delete(ptr);
#endif
    free(ptr);
}
