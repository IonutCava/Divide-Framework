/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _CORE_APPLICATION_H_
#define _CORE_APPLICATION_H_

#include "WindowManager.h"
#include "Core/Time/Headers/ApplicationTimer.h"

namespace Divide {

struct Task;
struct Configuration;

class Kernel;

const char* getErrorCodeName(ErrorCode code) noexcept;
  
namespace Attorney {
    class ApplicationTask;
};

struct SizeChangeParams {
    /// Window GUID
    I64 winGUID{ -1 };
    /// The new width and height
    U16 width{ 0u };
    U16 height{ 0u };
    /// Is the window that fired the event fullscreen?
    bool isFullScreen{ false };
    bool isMainWindow{ false };
};

/// Lightweight singleton class that manages our application's kernel and window
/// information
class Application : public SDLEventListener {
    friend class Attorney::ApplicationTask;
  public:
     Application() noexcept;
     ~Application();

    /// Startup and shutdown
    ErrorCode start(const string& entryPoint, I32 argc, char** argv);
    void      stop();

    void idle();
    bool step(bool& restartEngineOnClose);
    bool onLoop();

    inline void RequestShutdown() noexcept;
    inline void CancelShutdown() noexcept;
    inline bool ShutdownRequested() const noexcept;

    inline void RequestRestart() noexcept;
    inline void CancelRestart() noexcept;
    inline bool RestartRequested() const noexcept;

    inline Kernel& kernel() const noexcept;
    inline WindowManager& windowManager() noexcept;
    inline const WindowManager& windowManager() const noexcept;

    void mainThreadTask(const DELEGATE<void>& task, bool wait = true);

    inline void setMemoryLogFile(const Str256& fileName);

    inline bool mainLoopActive() const noexcept;
    inline void mainLoopActive(bool state) noexcept;

    inline bool mainLoopPaused() const noexcept;
    inline void mainLoopPaused(bool state) noexcept; 
    
    inline bool freezeRendering() const noexcept;
    inline void freezeRendering(bool state) noexcept;

    bool onWindowSizeChange(const SizeChangeParams& params) const;
    bool onResolutionChange(const SizeChangeParams& params) const;

    inline void throwError(ErrorCode err) noexcept;
    inline ErrorCode errorCode() const noexcept;

    /// Add a list of callback functions that should be called when the application
    /// instance is destroyed
    /// (release hardware, file handlers, etc)
    inline void registerShutdownCallback(const DELEGATE<void>& cbk);

    PROPERTY_R(Time::ApplicationTimer, timer);
    Time::ApplicationTimer& timer() noexcept { return _timer; }

  private:
    bool onSDLEvent(SDL_Event event) noexcept override;

  private:
    WindowManager _windowManager;
     
    ErrorCode _errorCode = ErrorCode::NO_ERR;
    /// this is true when we are inside the main app loop
    std::atomic_bool _mainLoopActive;
    std::atomic_bool _mainLoopPaused;
    std::atomic_bool _freezeRendering;
    std::atomic_bool _requestShutdown;
    std::atomic_bool _requestRestart;
    std::atomic_bool _stepLoop;
    bool             _isInitialized = false;
    Kernel* _kernel = nullptr;
    /// buffer to register all of the memory allocations recorded via
    /// "MemoryManager_NEW"
    Str256 _memLogBuffer;
    /// A list of callback functions that get called when the application instance
    /// is destroyed
    vector<DELEGATE<void> > _shutdownCallback;

    /// A list of callbacks to execute on the main thread
    mutable Mutex _taskLock;
    vector<DELEGATE<void> > _mainThreadCallbacks;
};

FWD_DECLARE_MANAGED_CLASS(Application);

};  // namespace Divide

#endif  //_CORE_APPLICATION_H_

#include "Application.inl"
