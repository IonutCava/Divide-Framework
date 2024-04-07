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

#ifndef DVD_CORE_TIME_APPLICATION_TIMER_INL_
#define DVD_CORE_TIME_APPLICATION_TIMER_INL_

namespace Divide::Time
{

inline void  ApplicationTimer::resetFPSCounter() noexcept
{
    _frameRateHandler = {};
}

inline F32 ApplicationTimer::getFps() const noexcept
{
    return _frameRateHandler.frameRate();
}

inline F32 ApplicationTimer::getFrameTime() const noexcept
{
    return _frameRateHandler.frameTime();
}

inline void ApplicationTimer::getFrameRateAndTime(F32& fpsOut, F32& frameTimeOut) const noexcept
{
    _frameRateHandler.frameRateAndTime(fpsOut, frameTimeOut);
}

namespace App
{
    /// The following functions force a timer update (a call to query performance timer).
    FORCE_INLINE U64 ElapsedNanoseconds() noexcept
    {
        return MicrosecondsToNanoseconds( ElapsedMicroseconds() );
    }

    FORCE_INLINE D64 ElapsedMilliseconds() noexcept
    {
        return MicrosecondsToMilliseconds<D64, U64>( ElapsedMicroseconds() );
    }

    FORCE_INLINE D64 ElapsedSeconds() noexcept
    {
        return MicrosecondsToSeconds( ElapsedMicroseconds() );
    }
} //namespace App

}  // namespace Divide::Time

#endif  //DVD_CORE_TIME_APPLICATION_TIMER_INL_
