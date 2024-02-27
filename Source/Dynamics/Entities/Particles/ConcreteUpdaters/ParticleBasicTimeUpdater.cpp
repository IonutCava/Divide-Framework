

#include "Headers/ParticleBasicTimeUpdater.h"

namespace Divide {

void ParticleBasicTimeUpdater::update(const U64 deltaTimeUS, ParticleData& p) {
    U32 endID = p.aliveCount();
    const F32 localDT = Time::MicrosecondsToSeconds<F32>(deltaTimeUS);

    if (endID == 0) {
        return;
    }

    for (U32 i = 0; i < endID; ++i) {
        vec4<F32>& misc = p._misc[i];

        misc.x -= localDT;
        // interpolation: from 0 (start of life) till 1 (end of life)
        misc.y =
            1.0f - misc.x * misc.z;  // .z is 1.0/max life time

        if (misc.x <= 0.0f) {
            p.kill(i);
            endID = p.aliveCount() < p.totalCount() ? p.aliveCount()
                                                    : p.totalCount();
        }
    }
}
}