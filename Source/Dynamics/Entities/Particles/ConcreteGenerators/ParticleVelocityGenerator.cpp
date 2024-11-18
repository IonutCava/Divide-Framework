

#include "Headers/ParticleVelocityGenerator.h"

namespace Divide {

void ParticleVelocityGenerator::generate(Task& packagedTasksParent,
                                         TaskPool& parentPool,
                                         [[maybe_unused]] const U64 deltaTimeUS,
                                         ParticleData& p,
                                         U32 startIndex,
                                         U32 endIndex) {
    float3 min = _sourceOrientation * _minStartVel;
    float3 max = _sourceOrientation * _maxStartVel;
    
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
                std::for_each(from, to, [&](float4& velocity)
                {
                    velocity.set(Random(min, max));
                });
            }),
            parentPool);
        });
}

} //namespace Divide
