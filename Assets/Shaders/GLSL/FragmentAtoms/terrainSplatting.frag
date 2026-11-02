#ifndef _TERRAIN_SPLATTING_FRAG_
#define _TERRAIN_SPLATTING_FRAG_

#include "waterData.cmn"

#define UNDERWATER_ROUGNESS 0.3f

vec4 textureNoTileInternal(in sampler2DArray tex, in vec3 texUV) {
    if (VAR._LoDLevel == 0u) {
        return textureNoTile(tex, texUV);
    }
    if (VAR._LoDLevel == 1u) {
        return textureNoTile(tex, texDiffuse1, texUV);
    }
    return texture(tex, texUV);
}

#if defined(LOW_QUALITY) || !defined(REDUCE_TEXTURE_TILE_ARTIFACT)
#define SampleTextureNoTile texture
#else //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT
#define SampleTextureNoTile textureNoTileInternal
#endif //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT

#define getScaledCoords(UV, AMNT) ScaledTextureCoords(UV, TEXTURE_TILE_SIZE)

#define getUnderwaterNormal() (2.f * GetCaustics(vec3(VAR._texCoord * UNDERWATER_TILE_SCALE, 2)).rgb - 1.f)

float _private_roughness = 1.f;
void getTextureOMR(in bool usePacked, in vec3 uv, in uvec3 texOps, inout vec3 OMR) {
    OMR.r = 1.f;
    OMR.g = 0.f;
    OMR.b = _private_roughness;
}

vec2 GetTerrainWaterData()
{
    return GetWaterDetails(VAR._vertexW.xyz, TERRAIN_HEIGHT_OFFSET);
}

vec3 GetTerrainGeometryNormalWV()
{
    const vec3 geometricNormal = cross(dFdx(VAR._vertexWV.xyz), dFdy(VAR._vertexWV.xyz));
    return normalize(geometricNormal) * (gl_FrontFacing ? 1.f : -1.f);
}

mat3 GetTerrainTBNWV()
{
    const vec3 N = GetTerrainGeometryNormalWV();
    const mat3 viewMatrix = mat3(dvd_ViewMatrix);
    const vec3 viewZAxis = normalize(viewMatrix * WORLD_Z_AXIS);
    const vec3 viewYAxis = normalize(viewMatrix * WORLD_Y_AXIS);
    const vec3 T1 = cross(N, viewZAxis);
    const vec3 T2 = cross(N, viewYAxis);
    const vec3 T = normalize(length(T1) > length(T2) ? T1 : T2);
    const vec3 B = normalize(-cross(N, T));
    return mat3(T, B, N);
}

vec4 getBlendedAlbedo() {
    vec4 albedo = vec4(0.f);
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendAmnt = GetBlend(vec3(VAR._texCoord, i));
        const vec2 uv = getScaledCoords(VAR._texCoord, blendAmnt);
        // Albedo & Roughness
        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            albedo = mix(albedo, SampleTextureNoTile(texDiffuse0, vec3(uv * CURRENT_TILE_FACTORS[offset], offset)), blendAmnt[j]);
        }
    }

    return albedo;
}

void BuildBlendedSurface(out vec4 albedo, out vec3 normal) {
    albedo = vec4(0.f);
    normal = vec3(0.f);
    for (int i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendAmnt = GetBlend(vec3(VAR._texCoord, i));
        const vec2 uv = getScaledCoords(VAR._texCoord, blendAmnt);

        for (int j = 0; j < 4; ++j) {
            const uint offset = Mad(i, 4u, j);
            const vec3 crtUV = vec3(uv * CURRENT_TILE_FACTORS[offset], offset);
            const vec4 albedoAndRoughness = SampleTextureNoTile(texDiffuse0, crtUV);
            albedo = mix(albedo, albedoAndRoughness, blendAmnt[j]);
            normal = mix(normal, SampleTextureNoTile(texNormalMap, crtUV).rgb, blendAmnt[j]);
        }
    }

    normal = 2.f * normal - 1.f;
}


#if defined(MAIN_DISPLAY_PASS) && defined(PRE_PASS)
vec4 GetBlendedNormalAndRoughness() {
    vec4 albedo = vec4(0.f);
    vec3 normal = vec3(0.f);
    BuildBlendedSurface(albedo, normal);
    return vec4(normal, albedo.a);
}

vec4 GetTerrainNormalWVAndRoughness() {
    const vec2 waterData = GetTerrainWaterData();
    vec4 normalAndRoughness = mix(GetBlendedNormalAndRoughness(), vec4(getUnderwaterNormal(), UNDERWATER_ROUGNESS), waterData.x);

    normalAndRoughness.xyz = normalize(GetTerrainTBNWV() * normalAndRoughness.xyz);
    _private_roughness = normalAndRoughness.w;

    return normalAndRoughness;
}

#else //MAIN_DISPLAY_PASS && PRE_PASS
vec4 getUnderwaterAlbedo(in vec2 uv, in float waterDepth) {
    const float time2 = MSToSeconds(dvd_GameTimeMS) * 0.1f;
    const vec4 uvNormal = vec4(uv + time2.xx, uv + vec2(-time2, time2));

    return vec4(mix(0.5f * (GetCaustics(vec3(uvNormal.xy, 0)).rgb + texture(texSpecular, vec3(uvNormal.zw, 0)).rgb),
                    GetCaustics(vec3(uv, 1)).rgb,
                    waterDepth),
                UNDERWATER_ROUGNESS);
}

vec4 BuildTerrainData(out vec3 normalWV) {
    const vec2 waterData = GetTerrainWaterData();
    vec4 terrainAlbedo = vec4(0.f);

#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    terrainAlbedo = getBlendedAlbedo();
    normalWV = normalize(unpackNormal(sampleTexSceneNormals().rg));
#else //MAIN_DISPLAY_PASS && !PRE_PASS
    vec3 blendedNormal = vec3(0.f);
    BuildBlendedSurface(terrainAlbedo, blendedNormal);
    const vec3 normalMap = mix(blendedNormal, getUnderwaterNormal(), waterData.x);
    normalWV = normalize(GetTerrainTBNWV() * normalMap);
#endif //MAIN_DISPLAY_PASS && !PRE_PASS

    const vec4 ret = mix(terrainAlbedo, getUnderwaterAlbedo(VAR._texCoord * UNDERWATER_TILE_SCALE, waterData.y), waterData.x);
    _private_roughness = ret.a;
    return ret;
}
#endif //MAIN_DISPLAY_PASS && PRE_PASS

#endif //_TERRAIN_SPLATTING_FRAG_
