#include "stdafx.h"

#include "Headers/ParticleVelocityFromPositionGenerator.h"

namespace Divide {

void ParticleVelocityFromPositionGenerator::generate(Task& packagedTasksParent,
                                                     TaskPool& parentPool,
                                                     const U64 deltaTimeUS,
                                                     ParticleData& p,
                                                     const U32 startIndex,
                                                     const U32 endIndex) {
    for (U32 i = startIndex; i < endIndex; ++i) {
        p._velocity[i].xyz = Random(_minScale, _maxScale) * (p._position[i].xyz - _offset);
    }
}
}