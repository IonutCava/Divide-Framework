

#include "Headers/ParticleEulerUpdater.h"
#include "Core/Headers/Kernel.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

namespace {
    constexpr U32 g_partitionSize = 256;
}

void ParticleEulerUpdater::update(const U64 deltaTimeUS, ParticleData& p) {
    F32 const dt = Time::MicrosecondsToSeconds<F32>(deltaTimeUS);
    const float4 globalA(dt * _globalAcceleration, 0.0f);

    const U32 endID = p.aliveCount();


    ParallelForDescriptor descriptor = {};
    descriptor._iterCount = endID;
    descriptor._partitionSize = g_partitionSize;
    Parallel_For( context().taskPool( TaskPoolType::HIGH_PRIORITY ), descriptor, [&p, dt, globalA](const Task*, const U32 start, const U32 end)
    {
        vector<float4>& acceleration = p._acceleration;
        for (U32 i = start; i < end; ++i)
        {
            float4& acc = acceleration[i];
            acc.xyz = (acc + globalA).xyz;
        }
        vector<float4>& velocity = p._velocity;
        for (U32 i = start; i < end; ++i)
        {
            float4& vel = velocity[i];
            vel.xyz = (vel + dt * acceleration[i]).xyz;
        }

        vector<float4>& position = p._position;
        for (U32 i = start; i < end; ++i)
        {
            float4& pos = position[i];
            pos.xyz = (pos + dt * velocity[i]).xyz;
        }
    });
}

} //namespace Divide
