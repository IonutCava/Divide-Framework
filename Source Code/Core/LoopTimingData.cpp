#include "stdafx.h"

#include "Headers/LoopTimingData.h"

namespace Divide
{
    F32 LoopTimingData::alpha() const noexcept
    {
        const F32 diff = Time::MicrosecondsToMilliseconds<F32>( _accumulator ) / Time::MicrosecondsToMilliseconds<F32>( FIXED_UPDATE_RATE_US );
        return _freezeLoopTime ? 1.f : CLAMPED_01( diff );
    }

    void LoopTimingData::update( const U64 elapsedTimeUS ) noexcept
    {
        if ( _currentTimeUS == 0u )
        {
            _currentTimeUS = elapsedTimeUS;
        }

        _updateLoops = 0u;
        _previousTimeUS = _currentTimeUS;
        _currentTimeUS = elapsedTimeUS;
        _currentTimeDeltaUS = _currentTimeUS - _previousTimeUS;

        // In case we break in the debugger
        if ( _currentTimeDeltaUS > MAX_FRAME_TIME_US )
        {
            _currentTimeDeltaUS = MAX_FRAME_TIME_US;
        }

        _accumulator += _currentTimeDeltaUS;
    }

    // return true on change
    bool LoopTimingData::freezeTime( const bool state ) noexcept
    {
        if ( _freezeLoopTime != state )
        {
            _freezeLoopTime = state;
            _currentTimeFrozenUS = _currentTimeUS;
            return true;
        }
        return false;
    }
} //namespace Divide
