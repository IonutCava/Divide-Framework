#ifndef _TERRAIN_SPLATTING_FRAG_
#define _TERRAIN_SPLATTING_FRAG_

#define SAMPLE_NO_TILE_ARRAYS

#include "texturing.frag"
#include "waterData.cmn"

#if defined(LOW_QUALITY) || !defined(REDUCE_TEXTURE_TILE_ARTIFACT)

#define SampleTextureNoTile texture

#else //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT

vec4 TextureNoTile(in sampler2DArray tex, in vec3 texUV) {
#if defined(HIGH_QUALITY_TILE_ARTIFACT_REDUCTION)
    return textureNoTile(tex, texSpecular, 3, texUV, 0.5f);
#else //HIGH_QUALITY_TILE_ARTIFACT_REDUCTION
    return textureNoTile(tex, texUV);
#endif //HIGH_QUALITY_TILE_ARTIFACT_REDUCTION
}

vec4 SampleTextureNoTile(in sampler2DArray tex, in vec3 texUV)
{
#if !defined(REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS)
    if (VAR._LoDLevel >= 2) {
        return texture(tex, texUV);
    }
#endif //!REDUCE_TEXTURE_TILE_ARTIFACT_ALL_LODS

    return TextureNoTile(tex, texUV);
}

#endif //LOW_QUALITY || !REDUCE_TEXTURE_TILE_ARTIFACT

float[TOTAL_LAYER_COUNT] getBlendFactor(in vec2 uv) {
    INSERT_BLEND_AMNT_ARRAY

    uint offset = 0;
    for (uint i = 0; i < MAX_TEXTURE_LAYERS; ++i) {
        const vec4 blendColour = texture(texOpacityMap, vec3(uv, i));
        for (uint j = 0; j < CURRENT_LAYER_COUNT[i]; ++j) {
            blendAmount[offset + j] = blendColour[j];
        }
        offset += CURRENT_LAYER_COUNT[i];
    }

    return blendAmount;
}

#define scaledTextureCoords(UV) scaledTextureCoords(UV, TEXTURE_TILE_SIZE)

mat3 cotangentFrame() {
    const vec3 V = normalize(dvd_cameraPosition.xyz - VAR._vertexW.xyz);

    // get edge vectors of the pixel triangle
    const vec3 dp1  = dFdx(-V);
    const vec3 dp2  = dFdy(-V);
    const vec2 duv1 = dFdx(VAR._texCoord);
    const vec2 duv2 = dFdy(VAR._texCoord);

    // solve the linear system
    const vec3 dp2perp = cross(dp2, VAR._normalW);
    const vec3 dp1perp = cross(VAR._normalW, dp1);
    const vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    const vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame 
    const float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax,
                B * invmax,
                VAR._normalW);

}

mat3 getTBNW() {
    // We need to compute tangent and bitengent vectors with 
    // as cotangent_frame's results do not apply for what we need them to do
    const vec3 N = normalize(VAR._normalW);
    const vec3 T1 = cross(N, vec3(0.f, 0.f, 1.f));
    const vec3 T2 = cross(N, vec3(0.f, 1.f, 0.f));
    const vec3 T = normalize(length(T1) > length(T2) ? T1 : T2);
    const vec3 B = normalize(-cross(N, T));
    // Orthogonal matrix(each axis is a perpendicular unit vector)
    // The transpose of an orthogonal matrix equals its inverse
    return mat3(T, B, N);
}

mat3 getTBNWV() {
    return mat3(dvd_ViewMatrix) * getTBNW();
}

#if defined(HAS_PARALLAX)
float getDisplacementValueFromCoords(in vec2 sampleUV, in float[TOTAL_LAYER_COUNT] amnt) {
    float ret = 0.0f;
    for (uint i = 0; i < TOTAL_LAYER_COUNT; ++i) {
        ret = max(ret, SampleTextureNoTile(texProjected, vec3(sampleUV * CURRENT_TILE_FACTORS[i], DISPLACEMENT_IDX[i])).r * amnt[i]);
    }
    // Transform the height to displacement (easier to fake depth than height on flat surfaces)
    return 1.f - ret;
}

vec2 ParallaxOcclusionMapping(in vec2 scaledCoords, in vec3 viewDirT, float currentDepthMapValue, float heightFactor, in float[TOTAL_LAYER_COUNT] amnt) {
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
#define getScaledCoords(UV, AMNT) scaledTextureCoords(UV)
#else //LOW_QUALITY || !HAS_PARALLAX
vec2 getScaledCoords(vec2 uv, in float[TOTAL_LAYER_COUNT] amnt) {
    vec2 scaledCoords = scaledTextureCoords(uv);
#if 0
    //ToDo: Maybe bump this 1 unit? -Ionut
    if (VAR._LoDLevel == 0u) {
        const uint bumpMethod = dvd_bumpMethod(MATERIAL_IDX);
        if (bumpMethod == BUMP_PARALLAX || bumpMethod == BUMP_PARALLAX_OCCLUSION) {
            const vec3 viewDirT = transpose(getTBNW()) * normalize(dvd_cameraPosition.xyz - VAR._vertexW.xyz);
            const float currentHeight = getDisplacementValueFromCoords(scaledCoords, amnt);
            if (bumpMethod == BUMP_PARALLAX) {
                //ref: https://learnopengl.com/Advanced-Lighting/Parallax-Mapping
                const vec2 p = (viewDirT.xy / viewDirT.z) * currentHeight * dvd_parallaxFactor(MATERIAL_IDX);
                const vec2 texCoords = uv - p;
                const vec2 clippedTexCoord = vec2(texCoords.x - floor(texCoords.x),
                                                  texCoords.y - floor(texCoords.y));
                scaledCoords = scaledTextureCoords(clippedTexCoord);
            } else {
                scaledCoords = ParallaxOcclusionMapping(uv, viewDirT, currentHeight, dvd_parallaxFactor(MATERIAL_IDX), amnt);
            }
        }
    }
#endif

    return scaledCoords;
}
#endif //LOW_QUALITY || !HAS_PARALLAX

vec4 getTerrainNormal() {
    float blendAmount[TOTAL_LAYER_COUNT] = getBlendFactor(VAR._texCoord);
    const vec2 uv = getScaledCoords(VAR._texCoord, blendAmount);

    vec3 normal = vec3(0.f);
    for (uint i = 0; i < TOTAL_LAYER_COUNT; ++i) {
        normal = mix(normal, SampleTextureNoTile(texDiffuse1, vec3(uv * CURRENT_TILE_FACTORS[i], NORMAL_IDX[i])).rgb, blendAmount[i]);
    }

    return vec4(2.f * normal - 1.f, 0.f);
}

vec4 getUnderwaterNormal() {
    const vec3 normal = texture(texSpecular, vec3(VAR._texCoord * UNDERWATER_TILE_SCALE, 2)).rgb;

    return vec4(2.f * normal - 1.f, 0.f);
}

vec4 getTerrainAlbedo() {
    const float blendAmount[TOTAL_LAYER_COUNT] = getBlendFactor(VAR._texCoord);
    const vec2 uv = getScaledCoords(VAR._texCoord, blendAmount);

    vec4 albedo = vec4(0.0f);
    for (uint i = 0; i < TOTAL_LAYER_COUNT; ++i) {
        // Albedo & Roughness
        albedo = mix(albedo, SampleTextureNoTile(texDiffuse0, vec3(uv * CURRENT_TILE_FACTORS[i], ALBEDO_IDX[i])), blendAmount[i]);
    }

    return albedo;
}

vec4 getUnderwaterAlbedo(in vec2 uv, in float waterDepth) {
    const float time2 = MSToSeconds(dvd_time) * 0.1f;
    const vec4 uvNormal = vec4(uv + time2.xx, uv + vec2(-time2, time2));

    return vec4(mix(0.5f * (texture(texSpecular, vec3(uvNormal.xy, 0)).rgb + texture(texSpecular, vec3(uvNormal.zw, 0)).rgb),
                    texture(texSpecular, vec3(uv, 1)).rgb,
                    waterDepth),
                0.3f);
}

vec4 BuildTerrainData(out vec3 normalWV, out float normalVariation) {
    const vec2 waterData = GetWaterDetails(VAR._vertexW.xyz, TERRAIN_HEIGHT_OFFSET);

#if defined(LOW_QUALITY)
    normalWV = normalize(mat3(dvd_ViewMatrix) * VAR._normalW);
    return getTerrainAlbedo();
#endif //LOW_QUALITY
    if (VAR._LoDLevel > 1) {
        const vec4 normalMap = getTerrainNormal();
        normalVariation = normalMap.w;
        normalWV = normalize(mat3(dvd_ViewMatrix) * cotangentFrame() * normalMap.xyz);
        return getTerrainAlbedo();
    }

    const vec4 normalMap = mix(getTerrainNormal(), getUnderwaterNormal(), saturate(waterData.x));
    normalVariation = normalMap.w;
    normalWV = normalize(mat3(dvd_ViewMatrix) * cotangentFrame() * normalMap.xyz);
    return mix(getTerrainAlbedo(), getUnderwaterAlbedo(VAR._texCoord * UNDERWATER_TILE_SCALE, waterData.y), saturate(waterData.x));
}

#endif //_TERRAIN_SPLATTING_FRAG_
