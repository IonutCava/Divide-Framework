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

#ifndef _CORE_TIME_FRAMERATE_HANDLER_INL_
#define _CORE_TIME_FRAMERATE_HANDLER_INL_

namespace Divide {
namespace Time {

inline F32 FrameRateHandler::minFrameRate() const noexcept {
    return _minFPS;
}

inline F32 FrameRateHandler::maxFrameRate() const noexcept {
    return _maxFPS;
}

inline F32 FrameRateHandler::frameRate() const noexcept {
    return _framerate;
}

inline F32 FrameRateHandler::frameTime() const noexcept {
    return 1000.0f / frameRate();
}

inline void FrameRateHandler::frameRateAndTime(F32& fpsOut, F32& frameTimeOut) const noexcept {
    fpsOut = frameRate();
    frameTimeOut = frameTime();
}

inline F32 FrameRateHandler::averageFrameRate() const noexcept {
    return _averageFPS / _frameCount;
}

inline void FrameRateHandler::frameStates(F32& avgFPSOut, F32& minFPSOut, F32& maxFPSOut) const noexcept {
    avgFPSOut = averageFrameRate();
    minFPSOut = _minFPS;
    maxFPSOut = _maxFPS;
}

} //namespace Time
} //namespace Divide


#endif //_CORE_TIME_FRAMERATE_HANDLER_INL_