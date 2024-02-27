

#include "Headers/LoopTimingData.h"

namespace Divide
{
    F32 LoopTimingData::alpha() const noexcept
    {
        const F32 diff = Time::MicrosecondsToMilliseconds<F32>( _accumulator ) / Time::MicrosecondsToMilliseconds<F32>( FIXED_UPDATE_RATE_US );
        return _freezeGameTime ? 1.f : CLAMPED_01( diff );
    }

    void LoopTimingData::update( const U64 elapsedTimeUSApp, const U64 fixedGameTickDurationUS ) noexcept
    {
        _updateLoops = 0u;

        _gameTimeDeltaUS = _freezeGameTime ? 0u : fixedGameTickDurationUS;
        _gameCurrentTimeUS += _gameTimeDeltaUS;

        // In case we break in the debugger
        _appTimeDeltaUS = elapsedTimeUSApp - _appCurrentTimeUS;
        _appCurrentTimeUS += _appTimeDeltaUS;
        _appTimeDeltaUS = std::min( _appTimeDeltaUS, MAX_FRAME_TIME_US);

        _accumulator += _appTimeDeltaUS;
    }

} //namespace Divide
