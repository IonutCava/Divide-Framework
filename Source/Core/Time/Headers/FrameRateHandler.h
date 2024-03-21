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
#ifndef DVD_CORE_TIME_FRAMERATE_HANDLER_H_
#define DVD_CORE_TIME_FRAMERATE_HANDLER_H_

namespace Divide {
namespace Time {
class FrameRateHandler {
    static constexpr U32 FRAME_AVG_DELAY_COUNT = 5;
    static constexpr U32 FRAME_ARRAY_SIZE = 120;
    static constexpr U32 FRAME_AVG_RESET_COUNT = 60 * 5;

public:
    void tick(U64 deltaTimeUS) noexcept;

    [[nodiscard]] inline F32 minFrameRate() const noexcept;
    [[nodiscard]] inline F32 maxFrameRate() const noexcept;
    [[nodiscard]] inline F32 frameRate() const noexcept;
    [[nodiscard]] inline F32 frameTime() const noexcept;
    [[nodiscard]] inline F32 averageFrameRate() const noexcept;
    inline void frameRateAndTime(F32& fpsOut, F32& frameTimeOut) const noexcept;
    inline void frameStates(F32& avgFPSOut, F32& minFPSOut, F32& maxFPSOut) const noexcept;

private:
    std::array<F32, FRAME_ARRAY_SIZE> _framerateSecPerFrame = {};
    U32 _frameCount = 0u;
    F32 _minFPS = F32_MAX;
    F32 _maxFPS = F32_LOWEST;
    F32 _averageFPS = 0.f;
    F32 _previousElapsedSeconds = 0.f;
    F32 _framerate = 0.f;
    I32 _framerateSecPerFrameIdx = 0;
    F32 _framerateSecPerFrameAccum = 0.f;
};

} //namespace Time
} //namespace Divide

#endif //DVD_CORE_TIME_FRAMERATE_HANDLER_H_

#include "FrameRateHandler.inl"
