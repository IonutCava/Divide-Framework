

#include "Headers/ParticleTimeGenerator.h"

namespace Divide {

void ParticleTimeGenerator::generate([[maybe_unused]] Task& packagedTasksParent,
                                     [[maybe_unused]] TaskPool& parentPool,
                                     [[maybe_unused]] const U64 deltaTimeUS,
                                     ParticleData& p,
                                     const U32 startIndex,
                                     const U32 endIndex) {
    for (U32 i = startIndex; i < endIndex; ++i) {
        const F32 time = Random(_minTime, _maxTime);
        float4& misc = p._misc[i];
        misc.x = time;
        misc.y = 0.0f;
        misc.z = time > EPSILON_F32 ? 1.f / misc.x : 0.f;
    }
}

} //namespace Divide
