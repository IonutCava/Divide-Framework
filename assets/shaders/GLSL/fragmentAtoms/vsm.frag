#ifndef _VSM_FRAG_
#define _VSM_FRAG_

#include "nodeDataInput.cmn"

vec2 computeMoments() {
    const float Depth = dvd_IsOrthoCamera ? gl_FragCoord.z
                                          : length(VAR._vertexW.xyz - dvd_cameraPosition.xyz) / dvd_zPlanes.y;
    // Compute partial derivatives of depth. 
    const float dx = dFdx(Depth);
    const float dy = dFdy(Depth);

    // First moment is the depth itself.
    // Compute second moment over the pixel extents.
    return vec2(Depth, Squared(Depth) + 0.25f * (Squared(dx) + Squared(dy)));
}

#endif //_VSM_FRAG_
