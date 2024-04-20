

#include "Headers/ParticleBoxGenerator.h"

namespace Divide {

void ParticleBoxGenerator::generate(Task& packagedTasksParent,
                                    TaskPool& parentPool,
                                    [[maybe_unused]] const U64 deltaTimeUS,
                                    ParticleData& p,
                                    U32 startIndex,
                                    U32 endIndex) {
    vec3<F32> min(_posMin + _sourcePosition);
    vec3<F32> max(_posMax + _sourcePosition);
    
    using iter_t = decltype(std::begin(p._position));
    for_each_interval<iter_t>(std::begin(p._position) + startIndex,
                              std::begin(p._position) + endIndex,
                              ParticleData::g_threadPartitionSize,
                              [&](iter_t from, iter_t to)
    {
        Start(*CreateTask(
            &packagedTasksParent,
            [from, to, min, max](const Task&) mutable
            {
                std::for_each(from, to, [min, max](vec4<F32>& position)
                {
                    position.xyz = Random(min, max);
                });
            }),
            parentPool);
    });
}

} //namespace Divide
