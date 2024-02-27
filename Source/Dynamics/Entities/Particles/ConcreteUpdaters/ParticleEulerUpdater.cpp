

#include "Headers/ParticleEulerUpdater.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

namespace {
    constexpr U32 g_partitionSize = 256;
}

void ParticleEulerUpdater::update(const U64 deltaTimeUS, ParticleData& p) {
    F32 const dt = Time::MicrosecondsToSeconds<F32>(deltaTimeUS);
    const vec4<F32> globalA(dt * _globalAcceleration, 0.0f);

    const U32 endID = p.aliveCount();


    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = endID;
    descriptor._partitionSize = g_partitionSize;
    descriptor._cbk = [&p, dt, globalA](const Task*, const U32 start, const U32 end) -> void {
        vector<vec4<F32>>& acceleration = p._acceleration;
        for (U32 i = start; i < end; ++i) {
            vec4<F32>& acc = acceleration[i];
            acc.xyz = (acc + globalA).xyz;
        }
        vector<vec4<F32>>& velocity = p._velocity;
        for (U32 i = start; i < end; ++i) {
            vec4<F32>& vel = velocity[i];
            vel.xyz = (vel + dt * acceleration[i]).xyz;
        }

        vector<vec4<F32>>& position = p._position;
        for (U32 i = start; i < end; ++i) {
            vec4<F32>& pos = position[i];
            pos.xyz = (pos + dt * velocity[i]).xyz;
        }
    };

    parallel_for(context(), descriptor);
}

}