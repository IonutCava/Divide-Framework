

#include "Headers/ParticleVelocityFromPositionGenerator.h"

namespace Divide {

void ParticleVelocityFromPositionGenerator::generate([[maybe_unused]] Task& packagedTasksParent,
                                                     [[maybe_unused]] TaskPool& parentPool,
                                                     [[maybe_unused]] const U64 deltaTimeUS,
                                                     ParticleData& p,
                                                     const U32 startIndex,
                                                     const U32 endIndex) {
    for (U32 i = startIndex; i < endIndex; ++i) {
        p._velocity[i].xyz = Random(_minScale, _maxScale) * (p._position[i].xyz - _offset);
    }
}

} //namespace Divide
