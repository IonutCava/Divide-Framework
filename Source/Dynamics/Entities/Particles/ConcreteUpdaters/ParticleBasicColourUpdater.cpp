

#include "Headers/ParticleBasicColourUpdater.h"

#include "Core/Headers/EngineTaskPool.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

namespace {
    constexpr U32 g_partitionSize = 128;
}

void ParticleBasicColourUpdater::update( [[maybe_unused]] const U64 deltaTimeUS, ParticleData& p ) {
    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = p.aliveCount();
    descriptor._partitionSize = g_partitionSize;
    descriptor._cbk = [&p](const Task*, const U32 start, const U32 end) -> void {
        for (U32 i = start; i < end; ++i) {
            p._colour[i].set(Lerp(p._startColour[i], p._endColour[i], p._misc[i].y));
        }
    };

    parallel_for(context(), descriptor);
}
}