

#include "Headers/ParticleSphereVelocityGenerator.h"

namespace Divide {

void ParticleSphereVelocityGenerator::generate([[maybe_unused]] Task& packagedTasksParent,
                                               [[maybe_unused]] TaskPool& parentPool,
                                               [[maybe_unused]] const U64 deltaTimeUS,
                                               ParticleData& p,
                                               const U32 startIndex,
                                               const U32 endIndex) {
    for (U32 i = startIndex; i < endIndex; ++i) {
        const F32 phi = Random(-M_PI_f, M_PI_f);
        const F32 theta = Random(-M_PI_f, M_PI_f);
        const F32 v = Random(_minVel, _maxVel);
        const F32 r = v * std::sin(phi);
        float4& vel = p._velocity[i];
        vel.set(r * std::cos(theta), r * std::sin(theta), v * std::cos(phi), vel.w);
    }
}
}
