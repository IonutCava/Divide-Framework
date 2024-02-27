

#include "Headers/ParticlePositionColourUpdater.h"

namespace Divide {

void ParticlePositionColourUpdater::update( [[maybe_unused]] const U64 deltaTimeUS, ParticleData& p) {
    const U32 endID = p.aliveCount();
    const F32 diffr = _maxPos.x - _minPos.x;
    const F32 diffg = _maxPos.y - _minPos.y;
    const F32 diffb = _maxPos.z - _minPos.z;

    
    for (U32 i = 0; i < endID; ++i) {
        p._colour[i].set(
            (p._position[i].x - _minPos.x) /
                diffr,  // lerp(p._startColour[i].r, p._endColour[i].r, scaler),
            (p._position[i].y - _minPos.y) /
                diffg,  // lerp(p._startColour[i].g, p._endColour[i].g, scaleg),
            (p._position[i].z - _minPos.z) /
                diffb,  // lerp(p._startColour[i].b, p._endColour[i].b,
                         // scaleb),
             Lerp(to_F32(p._startColour[i].a) / 255.0f,
                  to_F32(p._endColour[i].a) / 255.0f,
                  p._misc[i].y) * 255.0f);
    }
}
}