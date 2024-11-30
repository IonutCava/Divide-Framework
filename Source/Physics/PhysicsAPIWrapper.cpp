

#include "Headers/PhysicsAPIWrapper.h"

namespace Divide
{
    PhysicsAPIWrapper::PhysicsAPIWrapper( PlatformContext& context )
        : PlatformContextComponent( context )
    {
    }

    void PhysicsAPIWrapper::updateTimeStep(const U8 simulationFrameRate, const F32 simSpeed)
    {
        _simualationFrameRate = simulationFrameRate == 0u ? 1u : simulationFrameRate;
        _timeStepSec = 1.f / _simualationFrameRate;
        _timeStepSec *= simSpeed;
    }

    void PhysicsAPIWrapper::frameStarted(const U64 deltaTimeGameUS)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Physics);

        if (_timeStepSec > 0.f)
        {
            _accumulatorSec += Time::MicrosecondsToSeconds<F32>(deltaTimeGameUS);

            if (_accumulatorSec < _timeStepSec)
            {
                return;
            }

            _accumulatorSec -= _timeStepSec;
            frameStartedInternal(Time::SecondsToMicroseconds<U64>(_timeStepSec));
        }
        
    }

    void PhysicsAPIWrapper::frameEnded(const U64 deltaTimeGameUS)
    {
        frameEndedInternal(deltaTimeGameUS);
    }

} //namespace Divide
