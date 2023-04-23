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

// Same as "Saturate(tan(acos(ndl)))" but maybe faster?
#define TanAcosNdL(NdL) (Saturate(sqrt(1.f - Squared(NdL)) / NdL))
#define GetNdotL(N, L) clamp(dot(N, L), M_EPSILON, 1.f)

float getShadowMultiplier(in vec3 normalWV)
{
    float ret = 1.f;

    for (int lightIdx = 0; lightIdx < DIRECTIONAL_LIGHT_COUNT; ++lightIdx)
    {
        const Light light = dvd_LightSource[lightIdx];

        const vec3 lightVec = normalize(-light._directionWV.xyz);
        const int shadowIndex = dvd_LightSource[lightIdx]._options.y;
        ret *= (shadowIndex >= 0 ? getShadowMultiplierDirectional(shadowIndex, TanAcosNdL(GetNdotL(normalWV, lightVec))) : 1.f);
    }

    const uint cluster = GetClusterIndex(gl_FragCoord);
    const uint lightIndexOffset = lightGrid[cluster]._offset;

    const uint lightCountPoint = lightGrid[cluster]._countPoint;
    for (int i = 0; i < lightCountPoint; ++i)
    {
        const Light light = dvd_LightSource[globalLightIndexList[lightIndexOffset + i] + DIRECTIONAL_LIGHT_COUNT];
        ret *= (light._options.y >= 0 ? getShadowMultiplierPoint( light._options.y, TanAcosNdL(GetNdotL(normalWV, normalize( light._positionWV.xyz - VAR._vertexWV.xyz )))) : 1.f);
    }

    const uint lightCountSpot = lightGrid[cluster]._countSpot;
    for (int i = 0; i < lightCountSpot; ++i)
    {
        const Light light = dvd_LightSource[globalLightIndexList[lightIndexOffset + lightCountPoint + i] + DIRECTIONAL_LIGHT_COUNT];
        ret *= (light._options.y >= 0 ? getShadowMultiplierSpot( light._options.y, TanAcosNdL(GetNdotL(normalWV, normalize( light._positionWV.xyz - VAR._vertexWV.xyz )))) : 1.f);
    }

    return ret;
}

vec3 getLightContribution(in PBRMaterial material, in vec3 N, in vec3 V, in bool receivesShadows, vec3 radianceIn)
{
    radianceIn += material._emissive;

#if defined(SHADING_MODE_FLAT)
    radianceIn += material._diffuseColour * material._occlusion * (receivesShadows ? getShadowMultiplier(N) : 1.f);
#else //SHADING_MODE_FLAT

    const float ndv = abs(dot(N, V)) + M_EPSILON;
    for ( uint lightIdx = 0u; lightIdx < DIRECTIONAL_LIGHT_COUNT; ++lightIdx )
    {
        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightVec = normalize(-light._directionWV.xyz);
        const float ndl = GetNdotL(N, lightVec);
        const int shadowIndex = light._options.y;
        const float shadowMultiplier = mix(1.f, getShadowMultiplierDirectional(shadowIndex, TanAcosNdL(ndl)), (shadowIndex >= 0 && receivesShadows));
        radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, shadowMultiplier, ndl, ndv, material);
    }

    const LightGrid grid = lightGrid[GetClusterIndex(gl_FragCoord)];
    const uint lightIndexOffset = grid._offset;

    const uint pointLightCount = grid._countPoint;
    for (uint i = 0u; i < pointLightCount; ++i)
    {
        const Light light = dvd_LightSource[globalLightIndexList[lightIndexOffset + i] + DIRECTIONAL_LIGHT_COUNT];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);
        const int shadowIndex = light._options.y;
        const float shadowMultiplier = mix(1.f, getShadowMultiplierPoint(shadowIndex, TanAcosNdL(ndl)), (shadowIndex >= 0 && receivesShadows));

        const float att = Saturate(1.f - (Squared(length(lightDir)) / Squared(light._positionWV.w)));
        radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, Squared(att) * shadowMultiplier, ndl, ndv, material);
    }

    const uint spotLightCount = grid._countSpot;
    for (uint i = 0u; i < spotLightCount; ++i)
    {
        const Light light = dvd_LightSource[globalLightIndexList[lightIndexOffset + pointLightCount + i] + DIRECTIONAL_LIGHT_COUNT];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);
        const int shadowIndex = light._options.y;
        const float shadowMultiplier = mix(1.f, getShadowMultiplierSpot(shadowIndex, TanAcosNdL(ndl)), (shadowIndex >= 0 && receivesShadows));

        const vec3  spotDirectionWV = normalize(light._directionWV.xyz);
        const float cosOuterConeAngle = light._colour.w;
        const float cosInnerConeAngle = light._directionWV.w;

        const float theta = dot(lightVec, normalize(-spotDirectionWV));
        const float intensity = Saturate((theta - cosOuterConeAngle) / (cosInnerConeAngle - cosOuterConeAngle));

        const float radius = mix(float(light._SPOT_CONE_SLANT_HEIGHT), light._positionWV.w, 1.f - intensity);
        const float att = Saturate(1.0f - (Squared(length(lightDir)) / Squared(radius))) * intensity;

        radianceIn += GetBRDF(lightVec, V, N, light._colour.rgb, att * shadowMultiplier, ndl, ndv, material);
    }
#endif //SHADING_MODE_FLAT

    return radianceIn;
}

vec3 computeViewDirectionWV()
{
    return normalize( mat3( dvd_ViewMatrix ) * normalize( dvd_CameraPosition - VAR._vertexW.xyz ) );
}

#endif //_LIGHTING_CALC_FRAG_
