/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_PARTICLE_COLOUR_GENERATOR_H_
#define DVD_PARTICLE_COLOUR_GENERATOR_H_

#include "Dynamics/Entities/Particles/Headers/ParticleGenerator.h"
#include "Utility/Headers/Colours.h"

namespace Divide {
struct ParticleColourGenerator final : ParticleGenerator {
    FColour4 _minStartCol = DefaultColours::BLACK;
    FColour4 _maxStartCol = DefaultColours::BLACK;
    FColour4 _minEndCol = DefaultColours::WHITE;
    FColour4 _maxEndCol = DefaultColours::WHITE;

    void generate(Task& packagedTasksParent,
                  TaskPool& parentPool,
                  U64 deltaTimeUS,
                  ParticleData& p,
                  U32 startIndex,
                  U32 endIndex) override;
};
}

#endif //DVD_PARTICLE_COLOUR_GENERATOR_H_
