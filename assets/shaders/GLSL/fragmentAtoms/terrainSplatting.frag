#ifndef _TERRAIN_SPLATTING_FRAG_
#define _TERRAIN_SPLATTING_FRAG_

#include "waterData.cmn"

#define UNDERWATER_ROUGNESS 0.3f

vec4 textureNoTileInternal(in sampler2DArray tex, in vec3 texUV) {

#if !defined(REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS)
    if (VAR._LoDLevel >= 2) {
        return texture(tex, texUV);
    }
#endif //!REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS

#if defined(HIGH_QUALITY_TILE_ARTIFACT_REDUCTION)
    return textureNoTile(tex, texSpecular, 3, texUV, 0.5f);
#else //HIGH_QUALITY_TILE_ARTIFACT_REDUCTION
    return textureNoTile(tex, texUV);
#endif //HIGH_QUALITY_TILE_ARTIFACT_REDUCTION
}

#if defined(LOW_QUALITY) || !defined(REDUCE_TEXTURE_TILE_ARTIFACT)
#define SampleTextureNoTile texture
#else //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT
#define SampleTextureNoTile textureNoTileInternal
#endif //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT

#if defined(HAS_PARALLAX)

#define getTBNW() (mat3(dvd_InverseViewMatrix) * getTBNWV())

float getDisplacementValueFromCoords(in vec2 sampleUV, in vec4 amnt) {
    float ret = 0.0f;
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        ret = max(ret, SampleTextureNoTile(texMetalness, vec3(sampleUV * CURRENT_TILE_FACTORS[i], DISPLACEMENT_IDX[i])).r * amnt[i]);
    }
    // Transform the height to displacement (easier to fake depth than height on flat surfaces)
    return 1.f - ret;
}

vec2 ParallaxOcclusionMapping(in vec2 scaledCoords, in vec3 viewDirT, float currentDepthMapValue, float heightFactor, in BlendMapDataType amnt) {
    // number of depth layers
    const float minLayers = 8.f;
    const float maxLayers = 32.f;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.f, 0.f, 1.f), viewDirT)));
    // calculate the size of each layer
    float layerDepth = 1.f / numLayers;
    // depth of current layer
    float currentLayerDepth = 0.f;
    // the amount to shift the texture coordinates per layer (from vector P)
    vec2 P = viewDirT.xy * heightFactor;
    vec2 deltaTexCoords = P / numLayers;

    // get initial values
    vec2 currentTexCoords = scaledCoords;
    while (currentLayerDepth < currentDepthMapValue) {
        // shift texture coordinates along direction of P
        currentTexCoords -= deltaTexCoords;
        // get depthmap value at current texture coordinates
        currentDepthMapValue = getDisplacementValueFromCoords(currentTexCoords, amnt);
        // get depth of next layer
        currentLayerDepth += layerDepth;
    }

    const vec2 prevTexCoords = currentTexCoords + deltaTexCoords;

    // get depth after and before collision for linear interpolation
    const float afterDepth = currentDepthMapValue - currentLayerDepth;
    const float beforeDepth = getDisplacementValueFromCoords(prevTexCoords, amnt) - currentLayerDepth + layerDepth;

    // interpolation of texture coordinates
    const float weight = afterDepth / (afterDepth - beforeDepth);
    return prevTexCoords * weight + currentTexCoords * (1.f - weight);
}

#endif //HAS_PARALLAX

#if defined(LOW_QUALITY) || !defined(HAS_PARALLAX)
#define getScaledCoords(UV, AMNT) scaledTextureCoords(UV, TEXTURE_TILE_SIZE)
#else //LOW_QUALITY || !HAS_PARALLAX
vec2 getScaledCoords(vec2 uv, in vec4 amnt) {
    vec2 scaledCoords = scaledTextureCoords(uv, TEXTURE_TILE_SIZE);
#if 0
    //ToDo: Maybe bump this 1 unit? -Ionut
    if (VAR._LoDLevel == 0u) {
        const uint bumpMethod = dvd_BumpMethod(MATERIAL_IDX);
        if (bumpMethod == BUMP_PARALLAX || bumpMethod == BUMP_PARALLAX_OCCLUSION) {
            const vec3 viewDirT = transpose(getTBNW()) * normalize(dvd_cameraPosition.xyz - VAR._vertexW.xyz);
            const float currentHeight = getDisplacementValueFromCoords(scaledCoords, amnt);
            if (bumpMethod == BUMP_PARALLAX) {
                //ref: https://learnopengl.com/Advanced-Lighting/Parallax-Mapping
                const vec2 p = (viewDirT.xy / viewDirT.z) * currentHeight * dvd_ParallaxFactor(MATERIAL_IDX);
                const vec2 texCoords = uv - p;
                const vec2 clippedTexCoord = vec2(texCoords.x - floor(texCoords.x),
                                                  texCoords.y - floor(texCoords.y));
                scaledCoords = scaledTextureCoords(clippedTexCoord, TEXTURE_TILE_SIZE);
            } else {
                scaledCoords = ParallaxOcclusionMapping(uv, viewDirT, currentHeight, dvd_ParallaxFactor(MATERIAL_IDX), amnt);
            }
        }
    }
#endif

    return scaledCoords;
}
#endif //LOW_QUALITY || !HAS_PARALLAX

#define getUnderwaterNormal() (2.f * GetCaustics(vec3(VAR._texCoord * UNDERWATER_TILE_SCALE, 2)).rgb - 1.f)

float _private_roughness = 1.f;
void getTextureOMR(in bool usePacked, in vec3 uv, in uvec3 texOps, inout vec3 OMR) {
    OMR.r = 1.f;
    OMR.g = 0.f;
    OMR.b = _private_roughness;
}

vec4 getBlendedAlbedo() {
    vec4 albedo = vec4(0.f);
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendAmnt = GetBlend(vec3(VAR._texCoord, i));
        const vec2 uv = getScaledCoords(VAR._texCoord, blendAmnt);
        // Albedo & Roughness
        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            albedo = mix(albedo, SampleTextureNoTile(texDiffuse0, vec3(uv * CURRENT_TILE_FACTORS[offset], ALBEDO_IDX[offset])), blendAmnt[j]);
        }
    }

    return albedo;
}

vec3 getBlendedNormal() {
    vec3 normal = vec3(0.f);
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendAmnt = GetBlend(vec3(VAR._texCoord, i));
        const vec2 uv = getScaledCoords(VAR._texCoord, blendAmnt);

        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            normal = mix(normal, SampleTextureNoTile(texNormalMap, vec3(uv * CURRENT_TILE_FACTORS[offset], NORMAL_IDX[offset])).rgb, blendAmnt[j]);
        }
    }

    return 2.f * normal - 1.f;
}


#if defined(MAIN_DISPLAY_PASS) && defined(PRE_PASS)
vec4 GetBlendedNormalAndRoughness() {
    vec4 normalAndRoughness = vec4(0.f);
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendAmnt = GetBlend(vec3(VAR._texCoord, i));
        const vec2 uv = getScaledCoords(VAR._texCoord, blendAmnt);
        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            normalAndRoughness.xyz = mix(normalAndRoughness.xyz, SampleTextureNoTile(texNormalMap, vec3(uv * CURRENT_TILE_FACTORS[offset], NORMAL_IDX[offset])).rgb, blendAmnt[j]);
        }
        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            normalAndRoughness.w = mix(normalAndRoughness.w, SampleTextureNoTile(texDiffuse0, vec3(uv * CURRENT_TILE_FACTORS[offset], ALBEDO_IDX[offset])).a, blendAmnt[j]);
        }
    }

    normalAndRoughness.xyz = 2.f * normalAndRoughness.xyz - 1.f;
    return normalAndRoughness;
}

vec4 GetTerrainNormalWVAndRoughness() {
    const vec2 waterData = GetWaterDetails(VAR._vertexW.xyz, TERRAIN_HEIGHT_OFFSET);
    vec4 normalAndRoughness = mix(GetBlendedNormalAndRoughness(), vec4(getUnderwaterNormal(), UNDERWATER_ROUGNESS), waterData.x);

    normalAndRoughness.xyz = normalize(VAR._tbnWV * normalAndRoughness.xyz);
    _private_roughness = normalAndRoughness.w;

    return normalAndRoughness;
}

#else //MAIN_DISPLAY_PASS && PRE_PASS
vec4 getUnderwaterAlbedo(in vec2 uv, in float waterDepth) {
    const float time2 = MSToSeconds(dvd_TimeMS) * 0.1f;
    const vec4 uvNormal = vec4(uv + time2.xx, uv + vec2(-time2, time2));

    return vec4(mix(0.5f * (GetCaustics(vec3(uvNormal.xy, 0)).rgb + texture(texSpecular, vec3(uvNormal.zw, 0)).rgb),
                    GetCaustics(vec3(uv, 1)).rgb,
                    waterDepth),
                UNDERWATER_ROUGNESS);
}

vec4 BuildTerrainData(out vec3 normalWV) {
    const vec2 waterData = GetWaterDetails(VAR._vertexW.xyz, TERRAIN_HEIGHT_OFFSET);

#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    normalWV = normalize(unpackNormal(sampleTexSceneNormals().rg));
#else //MAIN_DISPLAY_PASS && !PRE_PASS
    const vec3 normalMap = mix(getBlendedNormal(), getUnderwaterNormal(), waterData.x);
    normalWV = normalize(VAR._tbnWV * normalMap);
#endif //MAIN_DISPLAY_PASS && !PRE_PASS

    const vec4 ret = mix(getBlendedAlbedo(), getUnderwaterAlbedo(VAR._texCoord * UNDERWATER_TILE_SCALE, waterData.y), waterData.x);
    _private_roughness = ret.a;
    return ret;
}
#endif //MAIN_DISPLAY_PASS && PRE_PASS

#endif //_TERRAIN_SPLATTING_FRAG_
