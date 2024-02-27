#include "Headers/ParticleFountainUpdater.h"

namespace Divide
{

void ParticleFountainUpdater::update( [[maybe_unused]] const U64 deltaTime, [[maybe_unused]] ParticleData& p) {
    /*F32 delta = Time::MicrosecondsToSeconds<F32>(deltaTime);

    F32 emissionVariance = random(-_emissionIntervalVariance, _emissionIntervalVariance);
    I32 newParticles = _emissionInterval + emissionVariance;
    newParticles = static_cast<I32>(newParticles * delta) / (p->lodLevel() + 1);

    F32 velVariance = random(-_velocityVariance, _velocityVariance);
    vec3<F32> mainDir = orientation * (WORLD_Y_AXIS * (_velocity + velVariance));

        for (I32 i = 0; i < newParticles; ++i) {
            ParticleDescriptor& currentParticle = _particles[findUnusedParticle()];
            F32 lifetimeVariance = random(-_descriptor._lifetimeVariance, _descriptor._lifetimeVariance);
            currentParticle.life = _descriptor._lifetime + Time::MillisecondsToSeconds(lifetimeVariance);
            memcpy(currentParticle.pos, origin._v, 3 * sizeof(F32));
            // Very bad way to generate a random direction;
            // See for instance http://stackoverflow.com/questions/5408276/python-uniform-spherical-distribution instead,
            // combined with some user-controlled parameters (main direction, spread, etc)
            currentParticle.speed[0] = mainDir.x + (rand() % 2000 - 1000.0f) / 1000.0f * spread;
            currentParticle.speed[1] = mainDir.y + (rand() % 2000 - 1000.0f) / 1000.0f * spread;
            currentParticle.speed[2] = mainDir.z + (rand() % 2000 - 1000.0f) / 1000.0f * spread;
            // Very bad way to generate a random color
            memcpy(currentParticle.rgba, DefaultColors::RANDOM()._v, 3 * sizeof(U8));
            currentParticle.rgba[3] = (rand() % 256) / 2;

            currentParticle.size = (rand() % 1000) / 2000.0f + 0.1f;
        }

        // Simulate all particles
        I32 particlesCount = 0;
        vec3<F32> half_gravity = DEFAULT_GRAVITY * delta * 0.5f;
        for (ParticleDescriptor& p : _particles) {
            if (p.life > 0.0f) {
                // Decrease life
                p.life -= delta;
                if (p.life > 0.0f) {
                    // Simulate simple physics : gravity only, no collisions
                    for (U8 i = 0; i < 3; ++i) {
                        p.speed[i] += half_gravity[i];
                        p.pos[i] += p.speed[i] * delta;
                    }
                    p.distanceToCameraSq = vec3<F32>(p.pos).distanceSquared(eyePos);

                    
                    _particlePositionData[4 * particlesCount + 0] = p.pos[0];
                    _particlePositionData[4 * particlesCount + 1] = p.pos[1];
                    _particlePositionData[4 * particlesCount + 2] = p.pos[2];
                    _particlePositionData[4 * particlesCount + 3] = p.size;

                    _particleColorData[4 * particlesCount + 0] = p.rgba[0];
                    _particleColorData[4 * particlesCount + 1] = p.rgba[1];
                    _particleColorData[4 * particlesCount + 2] = p.rgba[2];
                    _particleColorData[4 * particlesCount + 3] = p.rgba[3];
                } else {
                    // Particles that just died will be put at the end of the buffer in SortParticles();
                    p.distanceToCameraSq = -1.0f;
                }

                particlesCount++;
            }
        }
        */
}

};