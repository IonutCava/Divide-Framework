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

#ifndef _CORE_APPLICATION_INL_
#define _CORE_APPLICATION_INL_

namespace Divide {

inline const char* getErrorCodeName(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::NO_ERR:                   return "No error.";
        case ErrorCode::PLATFORM_CLOSE_ERROR:     return "Could not deinitialize target platform!";
        case ErrorCode::PLATFORM_INIT_ERROR:      return "Could not initialize target platform!";
        case ErrorCode::MISSING_SCENE_DATA:       return "Invalid Scene Data. SceneManager failed to load the specified scene";
        case ErrorCode::MISSING_SCENE_LOAD_CALL:  return "The specified scene failed to load all of its data properly";
        case ErrorCode::CPU_NOT_SUPPORTED:        return "The current CPU has an insufficient core count and as such, it's not supported";
        case ErrorCode::GFX_NOT_SUPPORTED:        return "The specified rendering API is not fully implemented and as such, it's not supported";
        case ErrorCode::GFX_NON_SPECIFIED:        return "No rendering API specified before trying to initialize the GFX Device";
        case ErrorCode::SFX_NON_SPECIFIED:        return "No audio API specified before trying to initialize the SFX Device";
        case ErrorCode::PFX_NON_SPECIFIED:        return "No physx API specified before trying to initialize the PFX Device";
        case ErrorCode::WINDOW_INIT_ERROR:        return "Windowing system failed to initialize";
        case ErrorCode::SDL_WINDOW_INIT_ERROR:    return "SDL failed to create a valid window";
        case ErrorCode::FONT_INIT_ERROR:          return "Font system failed to create a valid context";
        case ErrorCode::GLBINGING_INIT_ERROR:     return "GLBinding failed to initialize";
        case ErrorCode::GLSL_INIT_ERROR:          return "GLSL pre-init failed";
        case ErrorCode::GL_OLD_HARDWARE:          return "Current hardware does not support the minimum OpenGL features required";
        case ErrorCode::DX_INIT_ERROR:            return "DirectX API failed to initialize";
        case ErrorCode::DX_OLD_HARDWARE:          return "Current hardware does not support the minimum DirectX features required";
        case ErrorCode::OGL_OLD_HARDWARE:         return "Current hardware does not support the minimum Opengl features required or the maximum supported version is too old"; 
        case ErrorCode::SDL_AUDIO_INIT_ERROR:     return "SDL Audio library failed to initialize";
        case ErrorCode::SDL_AUDIO_MIX_INIT_ERROR: return "SDL Audio Mixer failed to initialize";
        case ErrorCode::FMOD_AUDIO_INIT_ERROR:    return "FMod Audio library failed to initialize";
        case ErrorCode::OAL_INIT_ERROR:           return "OpenAL failed to initialize";
        case ErrorCode::OCL_INIT_ERROR:           return "OpenCL could not find any compatible devices!";
        case ErrorCode::PHYSX_INIT_ERROR:         return "The PhysX library failed to initialize";
        case ErrorCode::PHYSX_EXTENSION_ERROR:    return "The PhysX library failed to load the required extensions";
        case ErrorCode::NO_LANGUAGE_INI:          return "Invalid language file";
        case ErrorCode::NOT_ENOUGH_RAM:           return "Insufficient physical RAM available to run the application!";
        case ErrorCode::WRONG_WORKING_DIRECTORY:  return "Wrong working directory specified! All paths are relative based on the executable's location.";
        case ErrorCode::EDITOR_INIT_ERROR:        return "Editor failed to load!";
        case ErrorCode::GUI_INIT_ERROR:           return "GUI failed to initialise!";
    }

    DIVIDE_UNEXPECTED_CALL();
    return "Unknown Error!";
}

inline void Application::RequestShutdown() noexcept {
    _requestShutdown = true;
}

inline void Application::CancelShutdown() noexcept {
    _requestShutdown = false;
}

inline bool Application::ShutdownRequested() const noexcept {
    return _requestShutdown;
}

inline void Application::RequestRestart() noexcept {
    _requestRestart = true;
}

inline void Application::CancelRestart() noexcept {
    _requestRestart = false;
}

inline bool Application::RestartRequested() const noexcept {
    return _requestRestart;
}

inline Kernel& Application::kernel() const noexcept {
    assert(_kernel != nullptr);
    return *_kernel;
}

inline WindowManager& Application::windowManager() noexcept {
    return _windowManager;
}

inline const WindowManager& Application::windowManager() const noexcept {
    return _windowManager;
}

inline void Application::setMemoryLogFile(const Str256& fileName) {
    _memLogBuffer = fileName;
}

inline bool Application::mainLoopActive() const noexcept {
    return _mainLoopActive;
}

inline void Application::mainLoopActive(const bool state) noexcept {
    _mainLoopActive = state;
}

inline bool Application::mainLoopPaused() const noexcept {
    return _mainLoopPaused;
}

inline void Application::mainLoopPaused(const bool state) noexcept {
    _mainLoopPaused = state;
}

inline void Application::throwError(const ErrorCode err) noexcept {
    _errorCode = err;
}

inline ErrorCode Application::errorCode() const noexcept {
    return _errorCode;
}

inline void Application::registerShutdownCallback(const DELEGATE<void>& cbk) {
    _shutdownCallback.push_back(cbk);
}

};  // namespace Divide

#endif  //_CORE_APPLICATION_INL_