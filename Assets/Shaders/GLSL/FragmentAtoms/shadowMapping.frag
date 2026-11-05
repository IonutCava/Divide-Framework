// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#ifndef _SHADOW_MAPPING_FRAG_
#define _SHADOW_MAPPING_FRAG_

// Can't disable CSM maps as it also contains our worldAO
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 5) uniform sampler2DArray    layeredDepthMaps;

#include "shadowUtils.frag"
#include "shadowStrength.cmn"
#include "spotShadowProjection.cmn"

float chebyshevUpperBound( in vec2 moments, in float distance )
{
    // Compute and clamp minimum variance to avoid numeric issues that may occur during filtering
    const float variance = max( moments.y - Squared( moments.x ), dvd_MinVariance );
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
    return IsInFrustum( lookupCoords ) ? chebyshevUpperBound( moments, currentDepth ) : 1.f;
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

float getShadowMultiplierDirectional(in int shadowIndex, in float TanAcosNdotL, in vec4 positionDx, in vec4 positionDy)
{

#if !defined(DISABLE_SHADOW_MAPPING_CSM)
    const CSMShadowProperties properties = dvd_CSMShadowTransforms[shadowIndex];

    const int Split = getCSMSlice(properties.dvd_shadowLightPosition);
    if (Split >= MAX_CSM_SPLITS_PER_LIGHT)
    {
        return 1.f;
    }
    const mat4 lightVP = properties.dvd_shadowLightVP[Split];
    const vec4 sc = lightVP * VAR._vertexW;
    const vec3 shadowCoord = Homogenize(sc);
    
    if (IsInFrustum(shadowCoord))
    {
        const vec4 crtDetails = properties.dvd_shadowLightDetails;
        const float bias = clamp(crtDetails.z * TanAcosNdotL, 0.f, 0.00001f);
        
        // now get current linear depth as the length between the fragment and light position
        const float currentDepth = shadowCoord.z - bias;
        const vec4 scDx = lightVP * positionDx;
        const vec4 scDy = lightVP * positionDy;
        const vec2 uvDx = (scDx.xy - shadowCoord.xy * scDx.w) / sc.w;
        const vec2 uvDy = (scDy.xy - shadowCoord.xy * scDy.w) / sc.w;
        const vec2 moments = textureGrad(layeredDepthMaps, vec3(shadowCoord.xy, crtDetails.y + Split), uvDx, uvDy).rg;

        const float ret = chebyshevUpperBound(moments, currentDepth);

        return Saturate(SHADOW_VISIBILITY_WITH_STRENGTH(ret, crtDetails.w));
    }
#endif // !DISABLE_SHADOW_MAPPING_CSM

    return 1.f;
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
    const float ret = chebyshevUpperBound(moments, currentDepth);
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
    const float ret = chebyshevUpperBound(moments, currentDepth);
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
