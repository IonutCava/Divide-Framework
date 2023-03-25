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
#ifndef _CORE_LOOP_TIMING_DATA_H_
#define _CORE_LOOP_TIMING_DATA_H_

namespace Divide
{
/// Application update rate
constexpr U32 TICKS_PER_SECOND = Config::TARGET_FRAME_RATE / Config::TICK_DIVISOR;
constexpr U64 FIXED_UPDATE_RATE_US = Time::SecondsToMicroseconds( 1 ) / TICKS_PER_SECOND;
constexpr U64 MAX_FRAME_TIME_US = Time::MillisecondsToMicroseconds( 250 );

struct LoopTimingData
{
    PROPERTY_R( U64, currentTimeUS, 0ULL );
    PROPERTY_R( U64, previousTimeUS, 0ULL );
    PROPERTY_R( U64, currentTimeDeltaUS, 0ULL );
    PROPERTY_RW( U64, accumulator, 0ULL );

    PROPERTY_RW( U8, updateLoops, 0u );
    PROPERTY_R( bool, freezeLoopTime, false );  //Pause scene processing
    PROPERTY_R( bool, forceRunPhysics, false ); //Simulate physics even if the scene is paused
    PROPERTY_R( U64, currentTimeFrozenUS, 0ULL );

    /// Real app delta time between frames. Can't be paused (e.g. used by editor)
    [[nodiscard]] inline U64 appTimeDeltaUS() const noexcept
    {
        return currentTimeDeltaUS();
    }

    /// Simulated app delta time between frames. Can be paused. (e.g. used by physics)
    [[nodiscard]] inline U64 realTimeDeltaUS() const noexcept
    {
        return _freezeLoopTime ? 0ULL : appTimeDeltaUS();
    }

    /// Framerate independent delta time between frames. Can be paused. (e.g. used by scene updates)
    [[nodiscard]] inline U64 fixedTimeStep() const noexcept
    {
        return _freezeLoopTime ? 0ULL : FIXED_UPDATE_RATE_US;
    }

    [[nodiscard]] F32 alpha() const noexcept;

    void update( const U64 elapsedTimeUS ) noexcept;
    /// return true on change
    bool freezeTime( const bool state ) noexcept;
};

} //namespace Divide
#endif //_CORE_LOOP_TIMING_DATA_H_
