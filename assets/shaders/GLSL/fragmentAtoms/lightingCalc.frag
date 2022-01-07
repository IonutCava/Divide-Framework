#ifndef _LIGHTING_CALC_FRAG_
#define _LIGHTING_CALC_FRAG_

#include "lightInput.cmn"

#include "IBL.frag"

#if defined(SHADING_MODE_PBR)
#include "pbr.frag"
#elif defined(SHADING_MODE_BLINN_PHONG)
#include "specGloss.frag"
#else //SHADING_MODE_PBR...
#include "specialBRDFs.frag"
#endif //SHADING_MODE_PBR..

#include "shadowMapping.frag"

// Same as "saturate(tan(acos(ndl)))" but maybe faster?
#define TanAcosNdL(NdL) (saturate(sqrt(1.f - SQUARED(NdL)) / NdL))
#define GetNdotL(N, L) clamp(dot(N, L), M_EPSILON, 1.f)

float getShadowMultiplier(in vec3 normalWV) {
    float ret = 1.f;

    const uint dirLightCount = DIRECTIONAL_LIGHT_COUNT;

    for (uint lightIdx = 0u; lightIdx < dirLightCount; ++lightIdx) {
        const Light light = dvd_LightSource[lightIdx];

        const vec3 lightVec = normalize(-light._directionWV.xyz);
        ret *= getShadowMultiplierDirectional(dvd_LightSource[lightIdx]._options.y, TanAcosNdL(GetNdotL(normalWV, lightVec)));
    }

    const uint cluster = GetClusterIndex(gl_FragCoord);
    const uint lightIndexOffset = lightGrid[cluster]._offset;
    const uint lightCountPoint = lightGrid[cluster]._countPoint;
    const uint lightCountSpot = lightGrid[cluster]._countSpot;

    for (uint i = 0u; i < lightCountPoint; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        ret *= getShadowMultiplierPoint(light._options.y, TanAcosNdL(GetNdotL(normalWV, normalize(lightDir))));
    }

    for (uint i = 0u; i < lightCountSpot; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + lightCountPoint + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        ret *= getShadowMultiplierSpot(light._options.y, TanAcosNdL(GetNdotL(normalWV, normalize(lightDir))));
    }

    return ret;
}

vec3 getLightContribution(in PBRMaterial material, in vec3 N, in vec3 V, in bool receivesShadows, in vec3 radianceIn)
{
#if defined(SHADING_MODE_FLAT)
    radianceIn += material._diffuseColour * material._occlusion * (receivesShadows ? getShadowMultiplier(N) : 1.f);
#else //SHADING_MODE_FLAT

    const LightGrid grid        = lightGrid[GetClusterIndex(gl_FragCoord)];
    const uint dirLightCount    = DIRECTIONAL_LIGHT_COUNT;
    const uint pointLightCount  = grid._countPoint;
    const uint spotLightCount   = grid._countSpot;
    const uint lightIndexOffset = grid._offset;
    const float ndv = abs(dot(N, V)) + M_EPSILON;

    for (uint lightIdx = 0u; lightIdx < dirLightCount; ++lightIdx) {
        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightVec = normalize(-light._directionWV.xyz);
        const float ndl = GetNdotL(N, lightVec);
        const float shadowMultiplier = receivesShadows ? getShadowMultiplierDirectional(light._options.y, TanAcosNdL(ndl)) : 1.f;
        if (shadowMultiplier > M_EPSILON) {
            radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, shadowMultiplier, ndl, ndv, material);
        }
    }

    for (uint i = 0u; i < pointLightCount; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);
        const float shadowMultiplier = receivesShadows ? getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl)) : 1.f;

        if (shadowMultiplier > M_EPSILON) {
            const float att = saturate(1.f - (SQUARED(length(lightDir)) / SQUARED(light._positionWV.w)));
            radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, SQUARED(att) * shadowMultiplier, ndl, ndv, material);
        }
    }

    for (uint i = 0u; i < spotLightCount; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + pointLightCount + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);
        const float shadowMultiplier = receivesShadows ? getShadowMultiplierSpot(light._options.y, TanAcosNdL(ndl)) : 1.f;

        if (shadowMultiplier > M_EPSILON) {
            const vec3  spotDirectionWV = normalize(light._directionWV.xyz);
            const float cosOuterConeAngle = light._colour.w;
            const float cosInnerConeAngle = light._directionWV.w;

            const float theta = dot(lightVec, normalize(-spotDirectionWV));
            const float intensity = saturate((theta - cosOuterConeAngle) / (cosInnerConeAngle - cosOuterConeAngle));

            const float radius = mix(float(light._SPOT_CONE_SLANT_HEIGHT), light._positionWV.w, 1.f - intensity);
            const float att = saturate(1.0f - (SQUARED(length(lightDir)) / SQUARED(radius))) * intensity;

            radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, att * shadowMultiplier, ndl, ndv, material);
        }
    }
#endif //SHADING_MODE_FLAT
    return radianceIn + material._emissive;
}

#endif //_LIGHTING_CALC_FRAG_