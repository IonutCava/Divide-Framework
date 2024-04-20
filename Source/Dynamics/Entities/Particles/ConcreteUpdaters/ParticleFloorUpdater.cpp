

#include "Headers/ParticleFloorUpdater.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

void ParticleFloorUpdater::update( [[maybe_unused]] const U64 deltaTimeUS, ParticleData& p) {
    constexpr U32 s_particlesPerThread = 1024;
    const U32 endID = p.aliveCount();

    STUBBED("ToDo: add proper orientation support! -Ionut");

    const F32 floorY = _floorY;
    const F32 bounce = _bounceFactor;


    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = endID;
    descriptor._partitionSize = s_particlesPerThread;
    descriptor._cbk = [&p, floorY, bounce](const Task*, const U32 start, const U32 end) {
        for (U32 i = start; i < end; ++i) {
            if (p._position[i].y - p._position[i].w / 2 < floorY) {
                vec3<F32> force(p._acceleration[i]);

                const F32 normalFactor = force.dot(WORLD_Y_AXIS);
                if (normalFactor < 0.0f) {
                    force -= WORLD_Y_AXIS * normalFactor;
                }
                const F32 velFactor = p._velocity[i].xyz.dot(WORLD_Y_AXIS);
                // if (velFactor < 0.0)
                p._velocity[i] -= vec4<F32>(WORLD_Y_AXIS * (1.0f + bounce) * velFactor, 0.0f);
                p._acceleration[i].xyz = force;
            }
        }
    };

    parallel_for(context(), descriptor);
}

} //namespace Divide
