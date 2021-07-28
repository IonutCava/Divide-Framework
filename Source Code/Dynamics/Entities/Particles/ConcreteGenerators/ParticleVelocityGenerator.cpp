#include "stdafx.h"

#include "Headers/ParticleVelocityGenerator.h"

namespace Divide {

void ParticleVelocityGenerator::generate(Task& packagedTasksParent,
                                         TaskPool& parentPool,
                                         const U64 deltaTimeUS,
                                         ParticleData& p,
                                         U32 startIndex,
                                         U32 endIndex) {
    vec3<F32> min = _sourceOrientation * _minStartVel;
    vec3<F32> max = _sourceOrientation * _maxStartVel;
    
    //ToDo: Use parallel-for for this
    using iter_t = decltype(std::begin(p._velocity));
    for_each_interval<iter_t>(std::begin(p._velocity) + startIndex,
                              std::begin(p._velocity) + endIndex,
                              ParticleData::g_threadPartitionSize,
                              [&](iter_t from, iter_t to)
    {
        Start(*CreateTask(
            &packagedTasksParent,
            [from, to, min, max](const Task&) mutable
            {
                std::for_each(from, to, [&](vec4<F32>& velocity)
                {
                    velocity.set(Random(min, max));
                });
            }),
            parentPool);
        });
}
}