#ifndef _BRDF_FRAG_
#define _BRDF_FRAG_

#if defined(NO_POST_FX)
#if !defined(NO_FOG)
#define NO_FOG
#endif //!NO_FOG
#endif //NO_POST_FX

#if defined(NO_REFLECTIONS)
#if !defined(NO_IBL)
#define NO_IBL
#endif //NO_IBL
#if !defined(NO_SSR)
#define NO_SSR
#endif //NO_SSR
#if !defined(NO_ENV_MAPPING)
#define NO_ENV_MAPPING
#endif //NO_ENV_MAPPING
#endif //NO_REFLECTIONS

#include "lightInput.cmn"

#include "materialData.frag"
#include "shadowMapping.frag"

#include "debug.frag"
#include "pbr.frag"
#include "specGloss.frag"
#include "specialBRDFs.frag"

vec3 GetBRDF(in vec3 L,
             in vec3 V,
             in vec3 N,
             in vec3 lightColour,
             in float lightAttenuation,
             in float NdotL,
             in float NdotV,
             in PBRMaterial material)
{
    if (material._shadingMode == SHADING_PBR_MR ||
        material._shadingMode == SHADING_PBR_SG)
    {
        return GetBRDF_PBR(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
    }
    
    if (material._shadingMode == SHADING_BLINN_PHONG)
    {
        return GetBRDF_Phong(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
    }

    return GetBRDF_Special(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
}

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

void getLightContribution(in PBRMaterial material, in vec3 N, in vec3 V, in bool receivesShadows, inout vec3 radianceOut)
{
    if (material._shadingMode == SHADING_FLAT) {
        radianceOut += material._diffuseColour * material._occlusion * (receivesShadows ? getShadowMultiplier(N) : 1.f);
        return;
    }

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
            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, shadowMultiplier, ndl, ndv, material);
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
            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, SQUARED(att) * shadowMultiplier, ndl, ndv, material);
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

            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, att * shadowMultiplier, ndl, ndv, material);
        }
    }
}

//https://iquilezles.org/www/articles/fog/fog.htm
vec3 applyFog(in vec3  rgb,      // original color of the pixel
              in float distance, // camera to point distance
              in vec3  rayOri,   // camera position
              in vec3  rayDir)   // camera to point vector
{
    const float c = dvd_fogDetails._colourSunScatter.a;
    const float b = dvd_fogDetails._colourAndDensity.a;
    const float fogAmount = c * exp(-rayOri.y * b) * (1.f - exp(-distance * rayDir.y * b)) / rayDir.y;
    return mix(rgb, dvd_fogDetails._colourAndDensity.rgb, fogAmount);
}

/// returns RGB - pixel lit colour, A - reserved
vec4 getPixelColour(in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV, in float normalVariation, in vec2 uv) {
    const PBRMaterial material = initMaterialProperties(materialData, albedo.rgb, uv, normalWV, normalVariation);

    const bool receivesShadows = dvd_receivesShadows(materialData);

    if (dvd_materialDebugFlag != DEBUG_NONE) {
        return vec4(getDebugColour(material, uv, normalWV, dvd_probeIndex(materialData), receivesShadows), albedo.a);
    }

    const vec3 viewVec = normalize(VAR._viewDirectionWV);

#if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
    vec3 iblRadiance = ApplyIBL(material, viewVec, normalWV, VAR._vertexW.xyz, dvd_probeIndex(materialData));
#else  //!NO_ENV_MAPPING && !NO_IBL
    vec3 iblRadiance = vec3(0.f);
#endif //!NO_ENV_MAPPING && !NO_IBL

    vec3 radianceOut = ApplySSR(dvd_AmbientColour.rgb + iblRadiance);

    getLightContribution(material, normalWV, viewVec, receivesShadows, radianceOut);
    radianceOut += material._emissive;

#if !defined(NO_FOG)
    if (dvd_fogEnabled) {
        radianceOut.rgb = applyFog(radianceOut.rgb,
                                   distance(VAR._vertexW.xyz, dvd_cameraPosition.xyz),
                                   dvd_cameraPosition.xyz,
                                   normalize(VAR._vertexW.xyz - dvd_cameraPosition.xyz));
    }
#endif //!NO_FOG

#if !defined(NO_SSAO)
    radianceOut *= texture(texSSAO, dvd_screenPositionNormalised).r;
#endif //!NO_SSAO

    return vec4(radianceOut, albedo.a);
}

#endif //_BRDF_FRAG_
