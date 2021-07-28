#include "stdafx.h"

#include "Headers/ParticleSource.h"

#include "Core/Headers/EngineTaskPool.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

ParticleSource::ParticleSource(GFXDevice& context)
    : ParticleSource(context, 0)
{
}

ParticleSource::ParticleSource(GFXDevice& context, const F32 emitRate)
    : _emitRate(emitRate),
      _context(context)
{
}

void ParticleSource::emit(const U64 deltaTimeUS, const std::shared_ptr<ParticleData>& p) {
    ParticleData& data = *p;

    const F32 dt = Time::MicrosecondsToSeconds<F32>(deltaTimeUS);
    const U32 maxNewParticles = to_U32(dt * _emitRate);
    const U32 startID = data.aliveCount();
    const U32 endID = std::min(startID + maxNewParticles, data.totalCount() - 1);

    TaskPool& pool = _context.context().taskPool(TaskPoolType::HIGH_PRIORITY);

    Task* generateTask = CreateTask(TASK_NOP);
    for (std::shared_ptr<ParticleGenerator>& gen : _particleGenerators) {
        gen->generate(*generateTask, pool, deltaTimeUS, data, startID, endID);
    }

    StartAndWait(*generateTask, pool);

    for (U32 i = startID; i < endID; ++i) {
        p->wake(i);
    }
}
}