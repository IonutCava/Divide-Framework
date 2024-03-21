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

#ifndef DVD_CORE_APPLICATION_INL_
#define DVD_CORE_APPLICATION_INL_

namespace Divide {

inline bool operator==( const DisplayManager::OutputDisplayProperties& lhs, const DisplayManager::OutputDisplayProperties& rhs ) noexcept
{
    return lhs._resolution == rhs._resolution &&
           lhs._bitsPerPixel == rhs._bitsPerPixel &&
           lhs._maxRefreshRate == rhs._maxRefreshRate &&
           lhs._formatName == rhs._formatName;
}

inline void Application::RequestShutdown(bool clearCache) noexcept
{
    _requestShutdown = true;
    _clearCacheOnExit = clearCache;
}

inline void Application::CancelShutdown() noexcept
{
    _requestShutdown = false;
}

inline bool Application::ShutdownRequested() const noexcept
{
    return _requestShutdown;
}

inline void Application::RequestRestart( bool clearCache ) noexcept
{
    _requestRestart = true;
    _clearCacheOnExit = clearCache;
}

inline void Application::CancelRestart() noexcept
{
    _requestRestart = false;
}

inline bool Application::RestartRequested() const noexcept
{
    return _requestRestart;
}

inline WindowManager& Application::windowManager() noexcept
{
    return _windowManager;
}

inline const WindowManager& Application::windowManager() const noexcept
{
    return _windowManager;
}

inline ErrorCode Application::errorCode() const noexcept
{
    return _errorCode;
}

inline Time::ApplicationTimer& Application::timer() noexcept
{
    return _timer;
}

inline void Application::mainLoopPaused( const bool state ) noexcept
{
    _mainLoopPaused = state;
}

inline void Application::mainLoopActive( const bool state ) noexcept
{
    _mainLoopActive = state;
}

inline void Application::freezeRendering( const bool state ) noexcept
{
    _freezeRendering = state;
}
};  // namespace Divide

#endif  //DVD_CORE_APPLICATION_INL_
