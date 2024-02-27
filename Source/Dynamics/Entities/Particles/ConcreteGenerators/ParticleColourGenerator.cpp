

#include "Headers/ParticleColourGenerator.h"

#include "Core/Headers/PlatformContext.h"

namespace Divide {

void ParticleColourGenerator::generate(Task& packagedTasksParent,
                                       TaskPool& parentPool,
                                       [[maybe_unused]] const U64 deltaTimeUS,
                                       ParticleData& p,
                                       const U32 startIndex,
                                       const U32 endIndex) {

    using iter_t_start = decltype(std::begin(p._startColour));
    for_each_interval<iter_t_start>(std::begin(p._startColour) + startIndex,
                                    std::begin(p._startColour) + endIndex,
                                    ParticleData::g_threadPartitionSize,
                                    [&](iter_t_start from, iter_t_start to)
    {
        Start(*CreateTask(
                   &packagedTasksParent,
                   [this, from, to](const Task&) {
                       std::for_each(from, to, [&](FColour4& colour)
                       {
                           colour.set(Random(_minStartCol, _maxStartCol));
                       });
                   }),
            parentPool);
    });

    using iter_t_end = decltype(std::begin(p._endColour));
    for_each_interval<iter_t_end>(std::begin(p._endColour) + startIndex,
                                  std::begin(p._endColour) + endIndex,
                                  ParticleData::g_threadPartitionSize,
                                  [&](iter_t_end from, iter_t_end to)
    {
        Start(*CreateTask(
                   &packagedTasksParent,
                   [this, from, to](const Task&) {
                       std::for_each(from, to, [&](FColour4& colour)
                       {
                           colour.set(Random(_minEndCol, _maxEndCol));
                       });
                   }),
            parentPool);
    });
}
}
