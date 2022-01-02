#ifndef _BRDF_FRAG_
#define _BRDF_FRAG_

#if defined(DEPTH_PASS)
#error BRDF can only be used in coloured passes!
#endif //DEPTH_PASS

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
    if (material._shadingMode == SHADING_PBR_MR || material._shadingMode == SHADING_PBR_SG) {
        return GetBRDF_PBR(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
    }
    else if (material._shadingMode == SHADING_BLINN_PHONG) {
        return GetBRDF_Phong(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
    }

    return GetBRDF_Special(L, V, N, lightColour, lightAttenuation, NdotL, NdotV, material);
}


layout(binding = TEXTURE_SSAO_SAMPLE) uniform sampler2D texSSAO;

#if defined(NO_SSAO)
#define GetSSAO() 1.f
#else //NO_SSAO
float GetSSAO() {
    return texture(texSSAO, dvd_screenPositionNormalised).r;
}
#endif //NO_SSAO

#if 0
#define GetNdotL(N, L) saturate(dot(N, L))
#else
#define GetNdotL(N, L) clamp(dot(N, L), M_EPSILON, 1.f)
#endif

#if 0
#define TanAcosNdL(NdL) saturate(tan(acos(ndl)))
#else // Same as above but maybe faster?
#define TanAcosNdL(NdL) (saturate(sqrt(1.f - SQUARED(ndl)) / ndl))
#endif

float getShadowMultiplier(in vec3 normalWV);

void getLightContribution(in PBRMaterial material, in vec3 N, in vec3 V, inout vec3 radianceOut)
{
    if (material._shadingMode == SHADING_FLAT) {
        radianceOut += material._diffuseColour * material._occlusion* getShadowMultiplier(N);
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
#if defined(MAX_SHADOW_MAP_LOD)
        const float shadowMultiplier = VAR._LoDLevel > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierDirectional(light._options.y, TanAcosNdL(ndl));
#else //MAX_SHADOW_MAP_LOD
        const float shadowMultiplier = getShadowMultiplierDirectional(light._options.y, TanAcosNdL(ndl));
#endif //MAX_SHADOW_MAP_LOD
        if (shadowMultiplier > M_EPSILON) {
            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, shadowMultiplier, ndl, ndv, material);
        }
    }

#if !defined(DIRECTIONAL_LIGHT_ONLY)
    for (uint i = 0; i < pointLightCount; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);

#if defined(MAX_SHADOW_MAP_LOD)
        const float shadowMultiplier = VAR._LoDLevel > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl));
#else //MAX_SHADOW_MAP_LOD
        const float shadowMultiplier = getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl));
#endif //MAX_SHADOW_MAP_LOD

        if (shadowMultiplier > M_EPSILON) {
            const float radiusSQ = SQUARED(light._positionWV.w);
            const float dist = length(lightDir);
            const float att = saturate(1.f - (SQUARED(dist) / radiusSQ));
            const float lightAtt = SQUARED(att) * shadowMultiplier;
            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, lightAtt, ndl, ndv, material);
        }
    }

    for (uint i = 0u; i < spotLightCount; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + pointLightCount + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);

#if defined(MAX_SHADOW_MAP_LOD)
        const float shadowMultiplier = VAR._LoDLevel > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierSpot(light._options.y, TanAcosNdL(ndl));
#else //MAX_SHADOW_MAP_LOD
        const float shadowMultiplier = getShadowMultiplierSpot(light._options.y, TanAcosNdL(ndl));
#endif //MAX_SHADOW_MAP_LOD

        if (shadowMultiplier > M_EPSILON) {
            const vec3  spotDirectionWV = normalize(light._directionWV.xyz);
            const float cosOuterConeAngle = light._colour.w;
            const float cosInnerConeAngle = light._directionWV.w;

            const float theta = dot(lightVec, normalize(-spotDirectionWV));
            const float intensity = saturate((theta - cosOuterConeAngle) / (cosInnerConeAngle - cosOuterConeAngle));

            const float dist = length(lightDir);
            const float radius = mix(float(light._SPOT_CONE_SLANT_HEIGHT), light._positionWV.w, 1.f - intensity);
            const float att = saturate(1.0f - (SQUARED(dist) / SQUARED(radius))) * intensity;
            const float lightAtt = att * shadowMultiplier;

            radianceOut += GetBRDF(lightVec, V, N, light._colour.rgb, lightAtt, ndl, ndv, material);
        }
    }
#endif //!DIRECTIONAL_LIGHT_ONLY
}

float getShadowMultiplier(in vec3 normalWV) {
    float ret = 1.0f;
#if defined(MAX_SHADOW_MAP_LOD)
    if (VAR._LoDLevel > MAX_SHADOW_MAP_LOD) {
        return ret;
    }
#endif //MAX_SHADOW_MAP_LOD
    const uint dirLightCount = dvd_LightData.x;

    for (uint lightIdx = 0; lightIdx < dirLightCount; ++lightIdx) {
        const Light light = dvd_LightSource[lightIdx];

        const vec3 lightDirectionWV = -light._directionWV.xyz;
        const float ndl = saturate((dot(normalWV, normalize(lightDirectionWV))));
        ret *= getShadowMultiplierDirectional(dvd_LightSource[lightIdx]._options.y, TanAcosNdL(ndl));
    }

    const uint cluster          = GetClusterIndex(gl_FragCoord);
    const uint lightIndexOffset = lightGrid[cluster]._offset;
    const uint lightCountPoint  = lightGrid[cluster]._countPoint;
    const uint lightCountSpot   = lightGrid[cluster]._countSpot;

    for (uint i = 0; i < lightCountPoint; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];

        const vec3 lightDirectionWV = light._positionWV.xyz - VAR._vertexWV.xyz;
        const float ndl = saturate((dot(normalWV, normalize(lightDirectionWV))));
        ret *= getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl));
    }

    for (uint i = 0; i < lightCountSpot; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + lightCountPoint + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];

        const vec3 lightDirectionWV = light._positionWV.xyz - VAR._vertexWV.xyz;
        const float ndl = saturate((dot(normalWV, normalize(lightDirectionWV))));
        ret *= getShadowMultiplierSpot(light._options.y, TanAcosNdL(ndl));
    }

    return ret;
}

vec3 lightClusters() {
    switch (GetClusterZIndex(gl_FragCoord.z) % 8) {
        case 0:  return vec3(1.0f, 0.0f, 0.0f);
        case 1:  return vec3(0.0f, 1.0f, 0.0f);
        case 2:  return vec3(0.0f, 0.0f, 1.0f);
        case 3:  return vec3(1.0f, 1.0f, 0.0f);
        case 4:  return vec3(1.0f, 0.0f, 1.0f);
        case 5:  return vec3(1.0f, 1.0f, 1.0f);
        case 6:  return vec3(1.0f, 0.5f, 0.5f);
        case 7:  return vec3(0.0f, 0.0f, 0.0f);
    }

    return vec3(0.5f, 0.25f, 0.75f);
}

vec3 lightHeatMap() {
    uint lights = lightGrid[GetClusterIndex(gl_FragCoord)]._countTotal;

    // show possible clipping
    if (lights == 0) {
        --lights;
    } else if (lights == MAX_LIGHTS_PER_CLUSTER) {
        ++lights;
    }

    return turboColormap(float(lights) / MAX_LIGHTS_PER_CLUSTER);
}

#if defined(DISABLE_SHADOW_MAPPING)
#define CSMSplitColour() vec3(0.f)
#else //DISABLE_SHADOW_MAPPING
vec3 CSMSplitColour() {
    vec3 colour = vec3(0.f);

    const uint dirLightCount = dvd_LightData.x;
    for (uint lightIdx = 0; lightIdx < dirLightCount; ++lightIdx) {
        const Light light = dvd_LightSource[lightIdx];
        const int shadowIndex = dvd_LightSource[lightIdx]._options.y;
        if (shadowIndex > -1) {
            switch (getCSMSlice(dvd_CSMShadowTransforms[shadowIndex].dvd_shadowLightPosition)) {
                case  0: colour.r += 0.15f; break;
                case  1: colour.g += 0.25f; break;
                case  2: colour.b += 0.40f; break;
                case  3: colour   += 1 * vec3(0.15f, 0.25f, 0.40f); break;
                case  4: colour   += 2 * vec3(0.15f, 0.25f, 0.40f); break;
                case  5: colour   += 3 * vec3(0.15f, 0.25f, 0.40f); break;
            };
            break;
        }
    }

    return colour;
}
#endif //DISABLE_SHADOW_MAPPING

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

vec3 getDebugNormal(in vec3 normalWV) {
    const vec3 normalW = normalize(mat3(dvd_InverseViewMatrix) * normalWV);
    return abs(normalW);
}

/// returns RGB - pixel lit colour, A - reserved
vec4 getPixelColour(in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV, in float normalVariation, in vec2 uv) {
    const PBRMaterial material = initMaterialProperties(materialData, albedo.rgb, uv, normalWV, normalVariation);

    vec3 radianceOut = dvd_AmbientColour.rgb;
    vec3 iblRadiance = vec3(0.f);

    const vec3 viewVec = normalize(VAR._viewDirectionWV);

    switch (dvd_materialDebugFlag) {
        case DEBUG_ALBEDO:       
        {
            return vec4(material._diffuseColour, albedo.a);
        }
        case DEBUG_LIGHTING:
        {
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            radianceOut = dvd_AmbientColour.rgb;
            getLightContribution(materialCopy, normalWV, viewVec, radianceOut);
            radianceOut += dvd_AmbientColour.rgb * materialCopy._diffuseColour * materialCopy._occlusion;
            return vec4(radianceOut, albedo.a);
        }
        case DEBUG_SPECULAR:
        {
            return vec4(material._specular.rgb, albedo.a);
        }
        case DEBUG_KS:
        {
            const vec3 H = normalize(normalWV + viewVec);
            const vec3 kS = computeFresnelSchlickRoughness(H, viewVec, material._F0, material._roughness);
            return vec4(kS, albedo.a);
        }
        case DEBUG_IBL:
        {
#if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
            PBRMaterial materialCopy = material;
            materialCopy._diffuseColour = vec3(1.f);
            iblRadiance = ApplyIBL(materialCopy, viewVec, normalWV, VAR._vertexW.xyz, dvd_probeIndex(materialData));
#endif
            return vec4(iblRadiance, albedo.a);
        }
        case DEBUG_SSAO:           return vec4(vec3(texture(texSSAO, dvd_screenPositionNormalised).r), albedo.a);
        case DEBUG_UV:             return vec4(fract(uv), 0.f, albedo.a);
        case DEBUG_EMISSIVE:       return vec4(material._emissive, albedo.a);
        case DEBUG_ROUGHNESS:      return vec4(vec3(material._roughness), albedo.a);
        case DEBUG_METALNESS:      return vec4(vec3(material._metallic), albedo.a);
        case DEBUG_NORMALS:        return vec4(getDebugNormal(normalWV), albedo.a);
        case DEBUG_TANGENTS:       return vec4(normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[0]), albedo.a);
        case DEBUG_BITANGENTS:     return vec4(normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[1]), albedo.a);
        case DEBUG_SHADOW_MAPS:    return vec4(vec3(getShadowMultiplier(normalWV)), albedo.a);
        case DEBUG_CSM_SPLITS:     return vec4(CSMSplitColour(), albedo.a);
        case DEBUG_LIGHT_HEATMAP:  return vec4(lightHeatMap(), albedo.a);
        case DEBUG_DEPTH_CLUSTERS: return vec4(lightClusters(), albedo.a);
        case DEBUG_REFRACTIONS:
        case DEBUG_REFLECTIONS:    return vec4(vec3(0.f), albedo.a);
        case DEBUG_MATERIAL_IDS:   return vec4(turboColormap(float(MATERIAL_IDX + 1) / MAX_CONCURRENT_MATERIALS), albedo.a);
    }

#if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
    iblRadiance = ApplyIBL(material, viewVec, normalWV, VAR._vertexW.xyz, dvd_probeIndex(materialData));
#endif //!NO_ENV_MAPPING && !NO_IBL
    radianceOut += iblRadiance;

#if !defined(NO_IBL)
    radianceOut = ApplySSR(radianceOut);
#endif //!NO_IBL

    getLightContribution(material, normalWV, viewVec, radianceOut);
    radianceOut += material._emissive;
#if !defined(NO_FOG)
    if (dvd_fogEnabled()) {
        radianceOut.rgb = applyFog(radianceOut.rgb,
                                   distance(VAR._vertexW.xyz, dvd_cameraPosition.xyz),
                                   dvd_cameraPosition.xyz,
                                   normalize(VAR._vertexW.xyz - dvd_cameraPosition.xyz));
    }
#endif
    return vec4(radianceOut * GetSSAO(), albedo.a);
}

#endif //_BRDF_FRAG_
