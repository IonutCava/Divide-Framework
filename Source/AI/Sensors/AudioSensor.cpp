

#include "Headers/AudioSensor.h"

namespace Divide {
using namespace AI;

AudioSensor::AudioSensor(AIEntity* const parentEntity)
    : Sensor(parentEntity, SensorType::AUDIO_SENSOR)
{
}

void AudioSensor::update([[maybe_unused]] const U64 deltaTimeUS) {
}

}  // namespace Divide
