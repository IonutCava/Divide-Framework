// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#ifndef _SHADOW_UTILS_FRAG_
#define _SHADOW_UTILS_FRAG_

#if !defined(DISABLE_SHADOW_MAPPING) || !defined(DISABLE_WORLD_AO)

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

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_FRAME, 9, std430) coherent ACCESS_R buffer dvd_ShadowBlock
{
    PointShadowProperties dvd_PointShadowTransforms[MAX_SHADOW_CASTING_POINT_LIGHTS];
    SpotShadowProperties dvd_SpotShadowTransforms[MAX_SHADOW_CASTING_SPOT_LIGHTS];
    CSMShadowProperties dvd_CSMShadowTransforms[MAX_SHADOW_CASTING_DIR_LIGHTS];
    vec4 dvd_CSMCameraViewDepthRow;
    mat4 dvd_WorldAOVPMatrix;
};

#endif //!DISABLE_SHADOW_MAPPING || !DISABLE_WORLD_AO

#if !defined(DISABLE_SHADOW_MAPPING)

// find the appropriate depth map to look up in based on the depth of this fragment
int getCSMSlice(in vec4 props[MAX_CSM_SPLITS_PER_LIGHT]) {
    const float fragDepth = dot(dvd_CSMCameraViewDepthRow, VAR._vertexW) / VAR._vertexW.w;

    for (int split = 0; split < MAX_CSM_SPLITS_PER_LIGHT; ++split) {
        if (props[split].w > 0.f && fragDepth > -props[split].w) {
            return split;
        }
    }
    return MAX_CSM_SPLITS_PER_LIGHT;
}

#endif //DISABLE_SHADOW_MAPPING

#endif //_SHADOW_UTILS_FRAG_
