#ifndef _SHADOW_MAPPING_FRAG_
#define _SHADOW_MAPPING_FRAG_

#if !defined(DISABLE_SHADOW_MAPPING)

#if !defined(DISABLE_SHADOW_MAPPING_SPOT)
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_SHADOW_SINGLE)  uniform sampler2DArray    singleDepthMaps;
#endif //DISABLE_SHADOW_MAPPING_SPOT

#if !defined(DISABLE_SHADOW_MAPPING_CSM)
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_SHADOW_LAYERED) uniform sampler2DArray    layeredDepthMaps;
#endif //DISABLE_SHADOW_MAPPING_CSM

#if !defined(DISABLE_SHADOW_MAPPING_POINT)
DESCRIPTOR_SET_RESOURCE(0, TEXTURE_SHADOW_CUBE)    uniform samplerCubeArray  cubeDepthMaps;
#endif //DISABLE_SHADOW_MAPPING_POINT

#include "shadowUtils.frag"

float chebyshevUpperBound(in vec2 moments, in float distance) {
    // Compute and clamp minimum variance to avoid numeric issues that may occur during filtering
    const float variance = max(moments.y - Squared(moments.x), dvd_MinVariance);
    // Compute probabilistic upper bound.
    const float mD = moments.x - distance;
    const float p_max = variance / (variance + Squared(mD));

    // Reduce light bleed
    const float p = LinStep(dvd_LightBleedBias, 1.f, p_max);

    return max(p, (distance <= moments.x ? 1.f : 0.f));
}

float getShadowMultiplierDirectional(in int shadowIndex, in float TanAcosNdotL) {
#if !defined(DISABLE_SHADOW_MAPPING_CSM)
    const CSMShadowProperties properties = dvd_CSMShadowTransforms[shadowIndex];

    const int Split = getCSMSlice(properties.dvd_shadowLightPosition);
    vec4 sc = properties.dvd_shadowLightVP[Split] * VAR._vertexW;
    const vec3 shadowCoord = sc.xyz / sc.w;
    if (isInFrustum(shadowCoord))
    {
        const vec4 crtDetails = properties.dvd_shadowLightDetails;
        const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.00001f);
        // now get current linear depth as the length between the fragment and light position
        const float currentDepth = shadowCoord.z - bias;
        const vec2 moments = texture(layeredDepthMaps, vec3(shadowCoord.xy, crtDetails.y + Split)).rg;
        const float ret = chebyshevUpperBound(moments, currentDepth);
        return Saturate(ret * crtDetails.w);
    }
#endif // !DISABLE_SHADOW_MAPPING_CSM

    return 1.f;
}

float getShadowMultiplierSpot(in int shadowIndex, in float TanAcosNdotL) {
#if !defined(DISABLE_SHADOW_MAPPING_SPOT)
    const SpotShadowProperties properties = dvd_SpotShadowTransforms[shadowIndex];

    const vec4 sc = properties.dvd_shadowLightVP * VAR._vertexW;
    const vec3 shadowCoord = sc.xyz / sc.w;
    const vec4 crtDetails = properties.dvd_shadowLightDetails;

    const float fragToLight = length(VAR._vertexW.xyz - properties.dvd_shadowLightPosition.xyz);
    const vec2 moments = texture(singleDepthMaps, vec3(shadowCoord.xy, crtDetails.y)).rg;

    const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.01f);
    const float farPlane = properties.dvd_shadowLightPosition.w;
    const float currentDepth = (fragToLight / farPlane) - bias;
    const float ret = chebyshevUpperBound(moments, currentDepth);
    return Saturate(ret * crtDetails.w);
#else // !DISABLE_SHADOW_MAPPING_SPOT
    return 1.f;
#endif // !DISABLE_SHADOW_MAPPING_SPOT
}

float getShadowMultiplierPoint(in int shadowIndex, in float TanAcosNdotL) {
#if !defined(DISABLE_SHADOW_MAPPING_POINT)
    const PointShadowProperties properties = dvd_PointShadowTransforms[shadowIndex];

    const vec4 crtDetails = properties.dvd_shadowLightDetails;
    // get vector between fragment position and light position
    const vec3 fragToLight = VAR._vertexW.xyz - properties.dvd_shadowLightPosition.xyz;
    // use the light to fragment vector to sample from the depth map 
    const vec2 moments = texture(cubeDepthMaps, vec4(fragToLight, crtDetails.y)).rg;

    const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.01f);
    const float farPlane = properties.dvd_shadowLightPosition.w;
    const float currentDepth = (length(fragToLight) / farPlane) - bias;
    const float ret = chebyshevUpperBound(moments, currentDepth);
    return Saturate(ret * crtDetails.w);
#else // !DISABLE_SHADOW_MAPPING_POINT
    return 1.f;
#endif // !DISABLE_SHADOW_MAPPING_POINT
}

#else //DISABLE_SHADOW_MAPPING

#define getCSMSlice(PROPS) 0
#define getShadowMultiplierDirectional(I, T) 1.f
#define getShadowMultiplierPoint(I, T) 1.f
#define getShadowMultiplierSpot(I, T) 1.f

#endif //DISABLE_SHADOW_MAPPING

#endif //_SHADOW_MAPPING_FRAG_
