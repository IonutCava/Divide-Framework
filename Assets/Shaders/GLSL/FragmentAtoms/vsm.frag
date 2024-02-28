#ifndef _VSM_FRAG_
#define _VSM_FRAG_

#include "nodeDataInput.cmn"

vec2 computeMoments()
{
#if defined(ORTHO_PROJECTION)
    const float Depth = gl_FragCoord.z;
#else //ORTHO_PROJECTION
    const float Depth = length(VAR._vertexW.xyz - dvd_CameraPosition) / dvd_ZPlanes.y;
#endif //ORTHO_PROJECTION

    // Compute partial derivatives of depth. 
    const float dx = dFdx(Depth);
    const float dy = dFdy(Depth);

    // First moment is the depth itself.
    // Compute second moment over the pixel extents.
    return vec2(Depth, Squared(Depth) + 0.25f * (Squared(dx) + Squared(dy)));
}

#endif //_VSM_FRAG_
