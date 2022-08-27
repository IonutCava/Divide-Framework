#ifndef _SHADOW_UTILS_FRAG_
#define _SHADOW_UTILS_FRAG_

#if !defined(DISABLE_SHADOW_MAPPING)
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n

//dvd_shadowLightDetails:  x = light type, y =  arrayOffset, z - bias, w - strength

struct PointShadowProperties
{
    vec4  dvd_shadowLightDetails;
    vec4  dvd_shadowLightPosition;
};

struct SpotShadowProperties
{
    vec4  dvd_shadowLightDetails;
    vec4  dvd_shadowLightPosition;
    mat4  dvd_shadowLightVP;
};

struct CSMShadowProperties
{
    vec4  dvd_shadowLightDetails;
    vec4  dvd_shadowLightPosition[MAX_CSM_SPLITS_PER_LIGHT];
    mat4  dvd_shadowLightVP[MAX_CSM_SPLITS_PER_LIGHT];
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_FRAME, 13, std430) coherent ACCESS_R buffer dvd_ShadowBlock
{
    PointShadowProperties dvd_PointShadowTransforms[MAX_SHADOW_CASTING_POINT_LIGHTS];
    SpotShadowProperties dvd_SpotShadowTransforms[MAX_SHADOW_CASTING_SPOT_LIGHTS];
    CSMShadowProperties dvd_CSMShadowTransforms[MAX_SHADOW_CASTING_DIR_LIGHTS];
};

// find the appropriate depth map to look up in based on the depth of this fragment
int getCSMSlice(in vec4 props[MAX_CSM_SPLITS_PER_LIGHT]) {

    const float fragDepth = (VAR._vertexWV.z / VAR._vertexWV.w);

    // Figure out which cascade to sample from
    int Split = 0;
    for (; Split < MAX_CSM_SPLITS_PER_LIGHT; ++Split) {
        if (fragDepth > -props[Split].w) {
            break;
        }
    }

    // GLOBAL
    const int SplitPowLookup[] = {
        0,
        1, 1,
        2, 2, 2, 2,
        3
#       if MAX_CSM_SPLITS_PER_LIGHT > 4
        , 3, 3, 3, 3, 3, 3, 3,
        LT(4),
        LT(5), LT(5),
        LT(6), LT(6), LT(6), LT(6)
#       endif //MAX_CSM_SPLITS_PER_LIGHT
    };

    // Ensure that every fragment in the quad choses the same split so that derivatives
    // will be meaningful for proper texture filtering and LOD selection.
    const int SplitPow = 1 << Split;
    const int SplitX = int(abs(dFdx(SplitPow)));
    const int SplitY = int(abs(dFdy(SplitPow)));
    const int SplitXY = int(abs(dFdx(SplitY)));
    const int SplitMax = max(SplitXY, max(SplitX, SplitY));

    return SplitMax > 0 ? SplitPowLookup[SplitMax - 1] : Split;
}

#endif //DISABLE_SHADOW_MAPPING

#endif //_SHADOW_UTILS_FRAG_
