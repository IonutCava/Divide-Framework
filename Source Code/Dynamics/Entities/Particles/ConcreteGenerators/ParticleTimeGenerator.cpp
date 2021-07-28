#include "stdafx.h"

#include "Headers/ParticleTimeGenerator.h"

namespace Divide {

void ParticleTimeGenerator::generate(Task& packagedTasksParent,
                                     TaskPool& parentPool,
                                     const U64 deltaTimeUS,
                                     ParticleData& p,
                                     const U32 startIndex,
                                     const U32 endIndex) {
    for (U32 i = startIndex; i < endIndex; ++i) {
        const F32 time = Random(_minTime, _maxTime);
        vec4<F32>& misc = p._misc[i];
        misc.x = time;
        misc.y = 0.0f;
        misc.z = time > std::numeric_limits<F32>::epsilon() ? 1.0f / misc.x : 0.0f;
    }
}
}