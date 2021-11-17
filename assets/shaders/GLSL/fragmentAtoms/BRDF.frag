#ifndef _BRDF_FRAG_
#define _BRDF_FRAG_

#if defined(DEPTH_PASS)
#error BRDF can only be used in coloured passes!
#endif //DEPTH_PASS

#include "lightInput.cmn"

#include "materialData.frag"
#include "shadowMapping.frag"

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

#if defined(USE_SHADING_FLAT)
#define getLightContribution(Prop, LoD, N, V) (Prop._albedo * Prop._OMR[0] * getShadowMultiplier(N, LoD));
#else //USE_SHADING_FLAT
vec3 getLightContribution(in NodeMaterialProperties properties, in uint LoD, in vec3 N, in vec3 V)
{
    vec3 ret = vec3(0.f);

    const LightGrid grid        = lightGrid[GetClusterIndex(gl_FragCoord)];
    const uint dirLightCount    = DIRECTIONAL_LIGHT_COUNT;
    const uint pointLightCount  = grid.countPoint;
    const uint spotLightCount   = grid.countSpot;
    const uint lightIndexOffset = grid.offset;
    const float ndv = abs(dot(N, V)) + M_EPSILON;

    for (uint lightIdx = 0; lightIdx < dirLightCount; ++lightIdx) {
        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightVec = normalize(-light._directionWV.xyz);
        const float ndl = GetNdotL(N, lightVec);
#if defined(MAX_SHADOW_MAP_LOD)
        const float shadowMultiplier = LoD > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierDirectional(light._options.y, TanAcosNdL(ndl));
#else //MAX_SHADOW_MAP_LOD
        const float shadowMultiplier = getShadowMultiplierDirectional(light._options.y, TanAcosNdL(ndl));
#endif //MAX_SHADOW_MAP_LOD
        if (shadowMultiplier > M_EPSILON) {
            ret += GetBRDF(lightVec, V, N, light._colour.rgb, shadowMultiplier, ndl, ndv, properties);
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
        const float shadowMultiplier = LoD > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl));
#else //MAX_SHADOW_MAP_LOD
        const float shadowMultiplier = getShadowMultiplierPoint(light._options.y, TanAcosNdL(ndl));
#endif //MAX_SHADOW_MAP_LOD

        if (shadowMultiplier > M_EPSILON) {
            const float radiusSQ = SQUARED(light._positionWV.w);
            const float dist = length(lightDir);
            const float att = saturate(1.f - (SQUARED(dist) / radiusSQ));
            const float lightAtt = SQUARED(att) * shadowMultiplier;
            ret += GetBRDF(lightVec, V, N, light._colour.rgb, lightAtt, ndl, ndv, properties);
        }
    }

    for (uint i = 0; i < spotLightCount; ++i) {
        const uint lightIdx = globalLightIndexList[lightIndexOffset + pointLightCount + i] + dirLightCount;

        const Light light = dvd_LightSource[lightIdx];
        const vec3 lightDir = light._positionWV.xyz - VAR._vertexWV.xyz;
        const vec3 lightVec = normalize(lightDir);

        const float ndl = GetNdotL(N, lightVec);

#if defined(MAX_SHADOW_MAP_LOD)
        const float shadowMultiplier = LoD > MAX_SHADOW_MAP_LOD ? 1.f : getShadowMultiplierSpot(light._options.y, TanAcosNdL(ndl));
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

            ret += GetBRDF(lightVec, V, N, light._colour.rgb, lightAtt, ndl, ndv, properties);
        }
    }
#endif //!DIRECTIONAL_LIGHT_ONLY

    return ret;
}
#endif //USE_SHADING_FLAT

float getShadowMultiplier(in vec3 normalWV, in uint LoD) {
    float ret = 1.0f;
#if defined(MAX_SHADOW_MAP_LOD)
    if (LoD > MAX_SHADOW_MAP_LOD) {
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
    const uint lightIndexOffset = lightGrid[cluster].offset;
    const uint lightCountPoint  = lightGrid[cluster].countPoint;
    const uint lightCountSpot   = lightGrid[cluster].countSpot;

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

vec3 lightClusterColours(const bool debugDepthClusters) {
    if (debugDepthClusters) {
        const uint zTile = GetClusterZIndex(gl_FragCoord.z) % 8;

        switch (zTile) {
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

    const LightGrid grid = lightGrid[GetClusterIndex(gl_FragCoord)];

    uint lights = grid.countPoint + grid.countSpot;

    // show possible clipping
    if (lights == 0) {
        lights--;
    } else if (lights == MAX_LIGHTS_PER_CLUSTER) {
        lights++;
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

/// returns RGB - pixel lit colour, A - reserved
vec4 getPixelColour(in uint LoD, in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV, in float normalVariation, in vec2 uv, out vec3 SpecularColourOut, out vec3 MetalnessRoughnessProbeID) {
    const vec3 viewVec = normalize(VAR._viewDirectionWV);

    NodeMaterialProperties materialProperties;
    initMaterialProperties(materialData, albedo.rgb, uv, viewVec, normalWV, normalVariation, materialProperties);
    SpecularColourOut = materialProperties._kS;

    MetalnessRoughnessProbeID = vec3(materialProperties._OMR[1], materialProperties._OMR[2], float(dvd_probeIndex(materialData)));

    const uint selection = dvd_selectionFlag(materialData);
    const vec4 overlayCol = vec4(0.f, selection == 1u ? 2.f : 0.f, selection == 2u ? 2.f : 0.f, 0.f);

    switch (dvd_materialDebugFlag) {
        case DEBUG_ALBEDO:         return vec4(materialProperties._albedo, 1.f) + overlayCol;
        case DEBUG_LIGHTING:
        {
            materialProperties._albedo = vec3(1.f);
            return vec4(getLightContribution(materialProperties, LoD, normalWV, viewVec), 1.f) + overlayCol;
        }
        case DEBUG_SPECULAR:       return vec4(materialProperties._specular.rgb, 1.f) + overlayCol;
        case DEBUG_KS:             return vec4(SpecularColourOut, 1.f) + overlayCol;
        case DEBUG_UV:             return vec4(fract(uv), 0.f, 1.f) + overlayCol;
        case DEBUG_EMISSIVE:       return vec4(materialProperties._emissive, 1.f) + overlayCol;
        case DEBUG_ROUGHNESS:      return vec4(vec3(materialProperties._OMR[2]), 1.f) + overlayCol;
        case DEBUG_METALNESS:      return vec4(vec3(materialProperties._OMR[1]), 1.f) + overlayCol;
        case DEBUG_NORMALS:        return vec4(normalize(mat3(dvd_InverseViewMatrix) * normalWV), 1.f) + overlayCol;
        case DEBUG_TANGENTS:       return vec4(normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[0]), 1.f) + overlayCol;
        case DEBUG_BITANGENTS:     return vec4(normalize(mat3(dvd_InverseViewMatrix) * getTBNWV()[1]), 1.f) + overlayCol;
        case DEBUG_SHADOW_MAPS:    return vec4(vec3(getShadowMultiplier(normalWV, LoD)), 1.f) + overlayCol;
#if defined(MAIN_DISPLAY_PASS)
        case DEBUG_CSM_SPLITS:     return vec4(CSMSplitColour(), 1.f) + overlayCol;
        case DEBUG_LIGHT_HEATMAP:  return vec4(lightClusterColours(false), 1.f) + overlayCol;
        case DEBUG_DEPTH_CLUSTERS: return vec4(lightClusterColours(true), 1.f) + overlayCol;
        case DEBUG_REFRACTIONS:
        case DEBUG_REFLECTIONS:    return vec4(vec3(0.f), 1.f) + overlayCol;
#endif //MAIN_DISPLAY_PASS
        case DEBUG_MATERIAL_IDS:   return vec4(turboColormap(float(MATERIAL_IDX + 1) / MAX_CONCURRENT_MATERIALS), 1.f) + overlayCol;
    }

    const vec3 oColour = getLightContribution(materialProperties, LoD, normalWV, viewVec) + materialProperties._emissive;
    return vec4(oColour, albedo.a) + overlayCol;
}

vec4 getPixelColour(in vec4 albedo, in NodeMaterialData materialData, in vec3 normalWV, in float normalVariation, in vec2 uv, out vec3 SpecularColourOut, out vec3 MetalnessRoughnessProbeID) {
    return getPixelColour(0u, albedo, materialData, normalWV, normalVariation, uv, SpecularColourOut, MetalnessRoughnessProbeID);
}
#endif //_BRDF_FRAG_
