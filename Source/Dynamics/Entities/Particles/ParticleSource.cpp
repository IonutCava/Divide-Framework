// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/ParticleSource.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"

#include <cmath>

namespace Divide {

namespace
{
    constexpr U32 EMISSION_REMAINDER_CLEANUP_INTERVAL = 64u;
}

ParticleSource::ParticleSource(GFXDevice& context) noexcept
    : ParticleSource(context, 0)
{
}

ParticleSource::ParticleSource(GFXDevice& context, const F32 emitRate) noexcept
    : _emitRate(emitRate),
      _context(context)
{
}

void ParticleSource::emit(const U64 deltaTimeUS, const std::shared_ptr<ParticleData>& p) {
    if ( !DIVIDE_ASSERT(p != nullptr, "ParticleSource::emit: particle data is null") )
    {
        return;
    }

    ParticleData& data = *p;
    const U32 startID = data.aliveCount();
    const U32 capacity = data.totalCount();
    U32 emitCount = 0u;
    {
        LockGuard<Mutex> lock(_emissionLock);
        if ( !DIVIDE_ASSERT(startID <= capacity && std::isfinite(_emitRate) && _emitRate >= 0.0f,
                            "ParticleSource::emit: invalid particle count or emission rate") )
        {
            return;
        }

        if (++_emissionRemainderCleanupCounter % EMISSION_REMAINDER_CLEANUP_INTERVAL == 0u)
        {
            for ( auto it = _emissionRemainders.begin(); it != _emissionRemainders.end(); )
            {
                if ( it->first.expired() )
                {
                    it = _emissionRemainders.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        D64& remainder = _emissionRemainders[std::weak_ptr<ParticleData>(p)];
        const D64 requested = Time::MicrosecondsToSeconds<D64>(deltaTimeUS) * _emitRate + remainder;
        const D64 wholeParticles = std::floor(requested);
        remainder = requested - wholeParticles;
        const U32 available = capacity - startID;
        emitCount = wholeParticles >= to_D64(available) ? available : to_U32(wholeParticles);
    }
    if ( emitCount == 0u )
    {
        return;
    }
    const U32 endID = startID + emitCount;

    TaskPool& pool = _context.context().taskPool(TaskPoolType::HIGH_PRIORITY);

    Task* generateTask = CreateTask(TASK_NOP);
    for (const std::shared_ptr<ParticleGenerator>& gen : _particleGenerators) {
        gen->generate(*generateTask, pool, deltaTimeUS, data, startID, endID);
    }

    pool.enqueue( *generateTask );
    pool.wait( *generateTask );

    for (U32 i = startID; i < endID; ++i) {
        p->wake(i);
    }
}

} //namespace Divide
