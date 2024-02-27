/*
   Copyright (c) 2015 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
   and associated documentation files (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef _PARTICLE_FOUNTAIN_UPDATER_H_
#define _PARTICLE_FOUNTAIN_UPDATER_H_

#include "Dynamics/Entities/Particles/Headers/ParticleUpdater.h"

namespace Divide
{

class ParticleFountainUpdater : public ParticleUpdater
{
public:
    /// particles per second
    I32 _emissionInterval;         
    /// particles per second used to vary emission (_emissionInterval + rand(-_emissionIntervalVariance,_emissionIntervalVariance))
    I32 _emissionIntervalVariance; 
    F32 _spread;
    /// particle velocity on emission
    F32 _velocity;                   
    /// velocity variance (_velocity + rand(-_velocityVariance, _velocityVariance))
    F32 _velocityVariance;           
    /// lifetime, in milliseconds of each particle
    I32 _lifetime;                  
    /// liftime variance (_lifetime + rand(-_lifetimeVariance, _lifetimeVariance))
    I32 _lifetimeVariance;       

public:
    ParticleFountainUpdater() = default;
    ~ParticleFountainUpdater() = default;

    void update(const U64 deltaTime, ParticleData &p) override; 
};

};
#endif
