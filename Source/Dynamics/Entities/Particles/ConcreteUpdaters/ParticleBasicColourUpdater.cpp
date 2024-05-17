

#include "Headers/ParticleBasicColourUpdater.h"

#include "Core/Headers/TaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

namespace {
    constexpr U32 g_partitionSize = 128;
}

void ParticleBasicColourUpdater::update( [[maybe_unused]] const U64 deltaTimeUS, ParticleData& p ) {
    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = p.aliveCount();
    descriptor._partitionSize = g_partitionSize;
    Parallel_For( context().taskPool( TaskPoolType::HIGH_PRIORITY ), descriptor, [&p](const Task*, const U32 start, const U32 end)
    {
        for (U32 i = start; i < end; ++i) {
            p._colour[i].set(Lerp(p._startColour[i], p._endColour[i], p._misc[i].y));
        }
    });
}

} //namespace Divide
