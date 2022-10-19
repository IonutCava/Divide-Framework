#include "stdafx.h"

#include "Headers/Application.h"

#include "Headers/Kernel.h"
#include "Headers/ParamHandler.h"

#include "Core/Time/Headers/ApplicationTimer.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Utility/Headers/MemoryTracker.h"

namespace Divide {

bool MemoryManager::MemoryTracker::Ready = false;
bool MemoryManager::MemoryTracker::LogAllAllocations = false;
MemoryManager::MemoryTracker MemoryManager::AllocTracer;

Application::Application() noexcept
: _requestShutdown( false ),
  _requestRestart( false ),
  _mainLoopPaused( false ),
  _mainLoopActive( false )
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        MemoryManager::MemoryTracker::Ready = true; ///< faster way of disabling memory tracking
        MemoryManager::MemoryTracker::LogAllAllocations = false;
    }
}

Application::~Application()
{
    assert(!_isInitialized);
}

ErrorCode Application::start(const string& entryPoint, const I32 argc, char** argv) {
    assert(!entryPoint.empty());

    Profiler::Init();

    _isInitialized = true;
    _timer.reset();
     
    Console::toggleImmediateMode(true);
    Console::printfn(Locale::Get(_ID("START_APPLICATION")));
    Console::printfn(Locale::Get(_ID("START_APPLICATION_CMD_ARGUMENTS")));
    for (I32 i = 1; i < argc; ++i) {
        Console::printfn("%s", argv[i]);
    }
    // Create a new kernel
    assert(_kernel == nullptr);
    _kernel = MemoryManager_NEW Kernel(argc, argv, *this);

    // and load it via an XML file config
    const ErrorCode err = Attorney::KernelApplication::initialize(_kernel, entryPoint);

    // failed to start, so cleanup
    if (err != ErrorCode::NO_ERR) {
        throwError(err);
        stop();
    } else {
        Attorney::KernelApplication::warmup(_kernel);
        Console::printfn(Locale::Get(_ID("START_MAIN_LOOP")));
        Console::toggleImmediateMode(false);
        mainLoopActive(true);
    }

    return err;
}

void Application::stop() {
    if (_isInitialized) {
        if (_kernel != nullptr) {
            Attorney::KernelApplication::shutdown(_kernel);
        }
        for (const DELEGATE<void>& cbk : _shutdownCallback) {
            cbk();
        }

        _windowManager.close();
        MemoryManager::DELETE(_kernel);
        Console::printfn(Locale::Get(_ID("STOP_APPLICATION")));
        _isInitialized = false;

        if constexpr(Config::Build::IS_DEBUG_BUILD) {
            MemoryManager::MemoryTracker::Ready = false;
            bool leakDetected = false;
            size_t sizeLeaked = 0;
            const string allocLog = MemoryManager::AllocTracer.Dump(leakDetected, sizeLeaked);
            if (leakDetected) {
                Console::errorfn(Locale::Get(_ID("ERROR_MEMORY_NEW_DELETE_MISMATCH")), to_I32(std::ceil(sizeLeaked / 1024.0f)));
            }
            std::ofstream memLog;
            memLog.open((Paths::g_logPath + _memLogBuffer).str());
            memLog << allocLog;
            memLog.close();
        }

        Profiler::Shutdown();
    }
}

void Application::idle() {
    NOP();
}

bool Application::step(bool& restartEngineOnClose) {
    if (onLoop()) {
        if (RestartRequested()) {
            restartEngineOnClose = true;
            RequestShutdown();
        } else {
            restartEngineOnClose = false;
        }

        PROFILE_FRAME( "Main Thread" );
        Attorney::KernelApplication::onLoop(_kernel);
        return true;
    }

    windowManager().hideAll();
    CancelRestart();

    return false;
}

bool Application::onLoop() {
    ScopedLock<Mutex> r_lock(_taskLock);
    if (!_mainThreadCallbacks.empty()) {
        while(!_mainThreadCallbacks.empty()) {
            _mainThreadCallbacks.back()();
            _mainThreadCallbacks.pop_back();
        }
    }


    return mainLoopActive();
}


bool Application::onSDLEvent(const SDL_Event event) noexcept {
    if (event.type == SDL_QUIT) {
        RequestShutdown();
        return true;
    }

    return false;
}

bool Application::onWindowSizeChange(const SizeChangeParams& params) const {
    Attorney::KernelApplication::onWindowSizeChange(_kernel, params);
    return true;
}

bool Application::onResolutionChange(const SizeChangeParams& params) const {
    Attorney::KernelApplication::onResolutionChange(_kernel, params);
    return true;
}
void Application::mainThreadTask(const DELEGATE<void>& task, const bool wait) {
    std::atomic_bool done = false;
    if (wait) {
        ScopedLock<Mutex> w_lock(_taskLock);
        _mainThreadCallbacks.push_back([&done, &task] { task(); done = true; });
    } else {
        ScopedLock<Mutex> w_lock(_taskLock);
        _mainThreadCallbacks.push_back(task);
    }

    if (wait) {
        WAIT_FOR_CONDITION(done);
    }
}

}; //namespace Divide
