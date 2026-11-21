// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#ifndef _SHADOW_MAPPING_FRAG_
#define _SHADOW_MAPPING_FRAG_

// Can't disable CSM maps as it also contains our worldAO
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 5) uniform sampler2DArray    layeredDepthMaps;

#include "shadowUtils.frag"
#include "shadowStrength.cmn"
#include "spotShadowProjection.cmn"
#include "csmShadow.cmn"

float chebyshevUpperBound( in vec2 moments, in float distance, in float minVariance )
{
    // Compute and clamp minimum variance to avoid numeric issues that may occur during filtering
    const float variance = max( moments.y - Squared( moments.x ), minVariance );
    // Compute probabilistic upper bound.
    const float mD = moments.x - distance;

    const float p_max = variance / (variance + Squared( mD ));

    // Reduce light bleed
    const float p = SHADOW_REDUCE_LIGHT_BLEED( p_max, dvd_LightBleedBias );

    return max( p, (distance <= moments.x ? 1.f : 0.f) );
}

#if !defined(DISABLE_WORLD_AO)
float getWorldAO(in vec4 positionDx, in vec4 positionDy)
{
    const vec4 aoCoords = dvd_WorldAOVPMatrix * VAR._vertexW;
    const vec3 lookupCoords = Homogenize( aoCoords );
    const float currentDepth = lookupCoords.z - 0.01f;
    const vec4 aoDx = dvd_WorldAOVPMatrix * positionDx;
    const vec4 aoDy = dvd_WorldAOVPMatrix * positionDy;
    const vec2 uvDx = (aoDx.xy - lookupCoords.xy * aoDx.w) / aoCoords.w;
    const vec2 uvDy = (aoDy.xy - lookupCoords.xy * aoDy.w) / aoCoords.w;
    const vec2 moments = textureGrad( layeredDepthMaps, vec3( lookupCoords.xy, WORLD_AO_LAYER_INDEX ), uvDx, uvDy ).rg;
    return IsInFrustum( lookupCoords ) ? chebyshevUpperBound( moments, currentDepth, dvd_MinVariance ) : 1.f;
}
#else //DISABLE_WORLD_AO
#define getWorldAO(DX, DY) 1.f
#endif //DISABLE_WORLD_AO

#if !defined(DISABLE_SHADOW_MAPPING)

#if !defined(DISABLE_SHADOW_MAPPING_SPOT)
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 4) uniform sampler2DArray    singleDepthMaps;
#endif //DISABLE_SHADOW_MAPPING_SPOT

#if !defined(DISABLE_SHADOW_MAPPING_POINT)
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 6) uniform samplerCubeArray  cubeDepthMaps;
#endif //DISABLE_SHADOW_MAPPING_POINT

#if !defined(DISABLE_SHADOW_MAPPING_CSM)
bool sampleDirectionalShadow(in CSMShadowProperties properties, in int split, in float TanAcosNdotL,
                             in float referenceGradientSq, in vec4 positionDx, in vec4 positionDy,
                             out float visibility)
{
    const mat4 lightVP = properties.dvd_shadowLightVP[split];
    const vec4 sc = lightVP * VAR._vertexW;
    const vec3 shadowCoord = Homogenize(sc);

    if (IsInFrustum(shadowCoord))
    {
        const vec4 crtDetails = properties.dvd_shadowLightDetails;
        // Preserve cascade 0's world-space variance and bias in this orthographic depth range.
        const vec3 depthGradient = vec3(lightVP[0].z, lightVP[1].z, lightVP[2].z);
        const float scaleSq = CSM_DEPTH_SCALE_SQUARED(dot(depthGradient, depthGradient), referenceGradientSq);
        const float baseBias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.00001f);
        const float bias = CSM_DEPTH_BIAS_FOR_SCALE(baseBias, sqrt(scaleSq));
        const float currentDepth = shadowCoord.z - bias;
        const vec4 scDx = lightVP * positionDx;
        const vec4 scDy = lightVP * positionDy;
        const vec2 uvDx = (scDx.xy - shadowCoord.xy * scDx.w) / sc.w;
        const vec2 uvDy = (scDy.xy - shadowCoord.xy * scDy.w) / sc.w;
        const vec2 moments = textureGrad(layeredDepthMaps, vec3(shadowCoord.xy, crtDetails.y + split), uvDx, uvDy).rg;
        const float minVariance = CSM_MIN_VARIANCE_FOR_SCALE(dvd_MinVariance, scaleSq);
        visibility = chebyshevUpperBound(moments, currentDepth, minVariance);
        return true;
    }

    visibility = 1.f;
    return false;
}
#endif // !DISABLE_SHADOW_MAPPING_CSM

float getShadowMultiplierDirectional(in int shadowIndex, in float TanAcosNdotL, in vec4 positionDx, in vec4 positionDy)
{
#if !defined(DISABLE_SHADOW_MAPPING_CSM)
    const CSMShadowProperties properties = dvd_CSMShadowTransforms[shadowIndex];
    const int split = getCSMSlice(properties.dvd_shadowLightPosition);
    if (split >= MAX_CSM_SPLITS_PER_LIGHT)
    {
        return 1.f;
    }

    const mat4 referenceVP = properties.dvd_shadowLightVP[0];
    const vec3 referenceGradient = vec3(referenceVP[0].z, referenceVP[1].z, referenceVP[2].z);
    const float referenceGradientSq = max(dot(referenceGradient, referenceGradient), 1e-20f);
    float visibility;
    const bool currentValid = sampleDirectionalShadow(properties, split, TanAcosNdotL, referenceGradientSq,
                                                       positionDx, positionDy, visibility);

    if (split + 1 < MAX_CSM_SPLITS_PER_LIGHT && properties.dvd_shadowLightPosition[split + 1].w > 0.f)
    {
        const float depth = -dot(dvd_CSMCameraViewDepthRow, VAR._vertexW) / VAR._vertexW.w;
        const float previousEnd = split > 0 ? properties.dvd_shadowLightPosition[split - 1].w : 0.f;
        const float splitEnd = properties.dvd_shadowLightPosition[split].w;
        const float blendStart = CSM_BLEND_START(previousEnd, splitEnd);
        const float linearWeight = CSM_BLEND_LINEAR_WEIGHT(depth, blendStart, splitEnd);
        if (linearWeight > 0.f)
        {
            float nextVisibility;
            if (sampleDirectionalShadow(properties, split + 1, TanAcosNdotL, referenceGradientSq,
                                        positionDx, positionDy, nextVisibility))
            {
                const float weight = CSM_BLEND_SMOOTH_WEIGHT(linearWeight);
                visibility = currentValid ? mix(visibility, nextVisibility, weight) : nextVisibility;
            }
        }
    }

    return Saturate(SHADOW_VISIBILITY_WITH_STRENGTH(visibility, properties.dvd_shadowLightDetails.w));
#else
    return 1.f;
#endif // !DISABLE_SHADOW_MAPPING_CSM
}

float getShadowMultiplierSpot(in int shadowIndex, in float TanAcosNdotL, in vec4 positionDx, in vec4 positionDy) {

#if !defined(DISABLE_SHADOW_MAPPING_SPOT)
    const SpotShadowProperties properties = dvd_SpotShadowTransforms[shadowIndex];
    const mat4 lightVP = properties.dvd_shadowLightVP;
    const vec4 sc = lightVP * VAR._vertexW;
    if (!SPOT_SHADOW_INSIDE_FRUSTUM(sc.x, sc.y, sc.z, sc.w))
    {
        return 1.f;
    }
    const vec3 shadowCoord = sc.xyz / sc.w;
    const vec4 crtDetails = properties.dvd_shadowLightDetails;

    const float fragToLight = length(VAR._vertexW.xyz - properties.dvd_shadowLightPosition.xyz);
    const vec4 scDx = lightVP * positionDx;
    const vec4 scDy = lightVP * positionDy;
    const vec2 uvDx = (scDx.xy - shadowCoord.xy * scDx.w) / sc.w;
    const vec2 uvDy = (scDy.xy - shadowCoord.xy * scDy.w) / sc.w;
    const vec2 moments = textureGrad(singleDepthMaps, vec3(shadowCoord.xy, crtDetails.y), uvDx, uvDy).rg;

    const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.01f);
    const float farPlane = properties.dvd_shadowLightPosition.w;
    const float currentDepth = (fragToLight / farPlane) - bias;
    const float ret = chebyshevUpperBound(moments, currentDepth, dvd_MinVariance);
    return Saturate(SHADOW_VISIBILITY_WITH_STRENGTH(ret, crtDetails.w));
#else // !DISABLE_SHADOW_MAPPING_SPOT
    return 1.f;
#endif // !DISABLE_SHADOW_MAPPING_SPOT
}

float getShadowMultiplierPoint(in int shadowIndex, in float TanAcosNdotL, in vec4 positionDx, in vec4 positionDy) {

#if !defined(DISABLE_SHADOW_MAPPING_POINT)
    const PointShadowProperties properties = dvd_PointShadowTransforms[shadowIndex];

    const vec4 crtDetails = properties.dvd_shadowLightDetails;
    // get vector between fragment position and light position
    const vec3 fragToLight = VAR._vertexW.xyz - properties.dvd_shadowLightPosition.xyz;
    // use the light to fragment vector to sample from the depth map 
    const vec2 moments = textureGrad(cubeDepthMaps, vec4(fragToLight, crtDetails.y), positionDx.xyz, positionDy.xyz).rg;

    const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.01f);
    const float farPlane = properties.dvd_shadowLightPosition.w;
    const float currentDepth = (length(fragToLight) / farPlane) - bias;
    const float ret = chebyshevUpperBound(moments, currentDepth, dvd_MinVariance);
    return Saturate(SHADOW_VISIBILITY_WITH_STRENGTH(ret, crtDetails.w));
#else // !DISABLE_SHADOW_MAPPING_POINT
    return 1.f;
#endif // !DISABLE_SHADOW_MAPPING_POINT
}

#else //DISABLE_SHADOW_MAPPING

#define getCSMSlice(PROPS) 0
#define getShadowMultiplierDirectional(I, T, DX, DY) 1.f
#define getShadowMultiplierPoint(I, T, DX, DY) 1.f
#define getShadowMultiplierSpot(I, T, DX, DY) 1.f

#endif //DISABLE_SHADOW_MAPPING

#endif //_SHADOW_MAPPING_FRAG_
