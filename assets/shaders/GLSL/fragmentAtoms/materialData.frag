#ifndef _MATERIAL_DATA_FRAG_
#define _MATERIAL_DATA_FRAG_

#include "nodeBufferedInput.cmn"

#include "utility.frag"

#if defined(PBR_SHADING)
#include "pbr.frag"
#elif defined(USE_SHADING_BLINN_PHONG)
#include "specGloss.frag"
#else //PBR_SHADING
#include "specialBRDFs.frag"
#endif //PBR_SHADING

#if defined(COMPUTE_TBN)
#include "bumpMapping.frag"
#endif //COMPUTE_TBN

#if defined(USE_CUSTOM_ROUGHNESS)
void getOMR(in NodeMaterialData matData, in vec2 uv, inout float occlusion, inout float metallic, inout float roughness);
#else //USE_CUSTOM_ROUGHNESS
void getOMR(in NodeMaterialData matData, in vec2 uv, inout float occlusion, inout float metallic, inout float roughness) {
#if defined(SAMPLER_OMR_COMPACT)
    vec3 OMR = texture(texMetalness, uv).rgb;
#else //SAMPLER_OMR_COMPACT
    vec3 OMR = PACKED_OMR(matData);
#if defined(USE_OCCLUSION_MAP)
    OMR[0] = texture(texOcclusion, uv).r;
#endif //USE_OCCLUSION_MAP
#if defined(USE_METALLIC_MAP)
    OMR[1] = texture(texMetalness, uv).r;
#endif //USE_METALLIC_MAP
#if defined(USE_ROUGHNESS_MAP)
    OMR[2] = texture(texRoughness, uv).r;
#endif //USE_ROUGHNESS_MAP
#endif //SAMPLER_OMR_COMPACT

    occlusion = OMR[0];
    metallic = OMR[1];
    roughness = OMR[2];
}
#endif //USE_CUSTOM_ROUGHNESS

#if defined(USE_CUSTOM_SPECULAR)
vec4 getSpecular(in NodeMaterialData matData, in vec2 uv);
#else //USE_CUSTOM_SPECULAR
vec4 getSpecular(in NodeMaterialData matData, in vec2 uv) {
    vec4 specData = vec4(Specular(matData), SpecularStrength(matData));

#if defined(USE_SPECULAR_MAP)
    // Specular strength is baked into the specular colour, so even if we use a texture, we still need to multiply the strength in
    const vec4 texIn = texture(texSpecular, uv);
    specData.rgb = ApplyTexOperation(vec4(specData.rgb, 1.f),
                                     texture(texSpecular, uv),
                                     dvd_texOperations(matData).z).rgb;
#endif //USE_SPECULAR_MAP

    return specData;
}
#endif //USE_CUSTOM_SPECULAR

#if defined(USE_CUSTOM_EMISSIVE)
vec3 getEmissiveColour(in NodeMaterialData matData, in vec2 uv);
#else //USE_CUSTOM_EMISSIVE
vec3 getEmissiveColour(in NodeMaterialData matData, in vec2 uv) {
#if defined(USE_EMISSIVE_MAP)
    return texture(texEmissive, uv).rgb;
#endif //USE_EMISSIVE_MAP

    return EmissiveColour(matData);
}
#endif //USE_CUSTOM_EMISSIVE

// A Fresnel term that dampens rough specular reflections.
// https://seblagarde.wordpress.com/2011/08/17/hello-world/
vec3 computeFresnelSchlickRoughness(in vec3 H, in vec3 V, in vec3 F0, in float roughness) {
    const float cosTheta = saturate(dot(H, V));
    return F0 + (max(vec3(1.f - roughness), F0) - F0) * pow(1.f - cosTheta, 5.f);
}

PBRMaterial initMaterialProperties(in NodeMaterialData matData, in vec3 albedo, in vec2 uv, in vec3 V, in vec3 N, in float normalVariation) {
    PBRMaterial material;

    const vec3 albedoIn = albedo + Ambient(matData);

    getOMR(matData, uv, material._occlusion, material._metallic, material._roughness);
    material._emissive = getEmissiveColour(matData, uv);
    material._specular = getSpecular(matData, uv);
#if !defined(PBR_SHADING)
    // Deduce a roughness factor from specular colour and shininess
    const float specularIntensity = Luminance(material._specular.rgb);
    const float specularPower = material._specular.a / 1000.f;
    const float roughnessFactor = 1.f - sqrt(specularPower);
    // Specular intensity directly impacts roughness regardless of shininess
    material._roughness = (1.f - (saturate(pow(roughnessFactor, 2)) * specularIntensity));
#endif //!PBR_SHADING

    // Try to reduce specular aliasing by increasing roughness when minified normal maps have high variation.
    material._roughness = mix(material._roughness, 1.f, normalVariation);

    const vec3 dielectricSpecular = vec3(0.04f);
    const vec3 black = vec3(0.f);
    material._diffuseColour = mix(albedoIn * (vec3(1.f) - dielectricSpecular), black, material._metallic);
    material._F0 = mix(dielectricSpecular, albedoIn, material._metallic);
    material._a = max(SQUARED(material._roughness), 0.01f);

    return material;
}


#if defined(USE_CUSTOM_TBN)
mat3 getTBNWV();
#else //USE_CUSTOM_TBN
#if defined(COMPUTE_TBN)
#define getTBNWV() VAR._tbnWV
#else //COMPUTE_TBN
// Default: T - X-axis, B - Z-axis, N - Y-axis
#define getTBNWV() mat3(vec3(1.f, 0.f, 0.f), vec3(0.f, 0.f, 1.f), vec3(1.f, 0.f, 0.f))
#endif //COMPUTE_TBN
#endif //USE_CUSTOM_TBN

#if !defined(PRE_PASS)
// Reduce specular aliasing by producing a modified roughness value
// Tokuyoshi et al. 2019. Improved Geometric Specular Antialiasing.
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
float specularAntiAliasing(vec3 N, float a) {
    // normal-based isotropic filtering
    // this is originally meant for deferred rendering but is a bit simpler to implement than the forward version
    // saves us from calculating uv offsets and sampling textures for every light
    const float SIGMA2 = 0.25f; // squared std dev of pixel filter kernel (in pixels)
    const float KAPPA = 0.18f; // clamping threshold
    vec3 dndu = dFdx(N);
    vec3 dndv = dFdy(N);
    float variance = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernelRoughness2 = min(2.f * variance, KAPPA);
    return saturate(a + kernelRoughness2);
}
#endif //!PRE_PASS

#if !defined(PRE_PASS) || defined(HAS_TRANSPARENCY)
vec4 getTextureColour(in vec4 albedo, in vec2 uv, in uvec2 texOperations) {
    vec4 colour = albedo;
#if !defined(SKIP_TEX0)
    const vec4 colourA = texture(texDiffuse0, uv);
    colour = ApplyTexOperation(colour, colourA, texOperations.x);
#endif //SKIP_TEX0
#if !defined(SKIP_TEX1)
    const vec4 colourB = texture(texDiffuse1, uv);
    colour = ApplyTexOperation(colour, colourB, texOperations.y);
#endif //!SKIP_TEX1

#if 0
    colour = saturate(colour);
#endif

    return colour;
}

#if defined(HAS_TRANSPARENCY)
float getAlpha(in NodeMaterialData data, in vec2 uv) {
#   if defined(USE_OPACITY_MAP)
#       if defined(USE_OPACITY_MAP_RED_CHANNEL)
            return texture(texOpacityMap, uv).r;
#       else //USE_OPACITY_MAP_RED_CHANNEL
            return texture(texOpacityMap, uv).a;
#       endif //USE_OPACITY_MAP_RED_CHANNEL
#   elif defined(USE_ALBEDO_COLOUR_ALPHA) || defined(USE_ALBEDO_TEX_ALPHA)
            return getTextureColour(BaseColour(data), uv, dvd_texOperations(data).xy).a;
#   endif //USE_OPACITY_MAP

    return 1.f;
}

vec4 getAlbedo(in NodeMaterialData data, in vec2 uv) {
    vec4 albedo = getTextureColour(BaseColour(data), uv, dvd_texOperations(data).xy);
#   if defined(USE_OPACITY_MAP)
#       if defined(USE_OPACITY_MAP_RED_CHANNEL)
            albedo.a = texture(texOpacityMap, uv).r;
#       else //USE_OPACITY_MAP_RED_CHANNEL
            albedo.a = texture(texOpacityMap, uv).a;
#       endif //USE_OPACITY_MAP_RED_CHANNEL
#   endif //USE_OPACITY_MAP

    return albedo;
}
#else //HAS_TRANSPARENCY
#define getAlbedo(DATA, UV) getTextureColour(BaseColour(DATA), UV, dvd_texOperations(DATA).xy)
#endif //HAS_TRANSPARENCY

#endif //!PRE_PASS || HAS_TRANSPARENCY

#if defined(SAMPLER_NORMALMAP_IS_ARRAY)
#define UV_TYPE vec3
#define SamplerType sampler2DArray
#else //SAMPLER_NORMALMAP_IS_ARRAY
#define UV_TYPE vec2
#define SamplerType sampler2D
#endif //SAMPLER_NORMALMAP_IS_ARRAY

vec4 getNormalMapAndVariation(in SamplerType tex, in UV_TYPE uv) {
    const vec3 normalMap = 2.f * texture(tex, uv).rgb - 1.f;
    const float normalMap_Mip = textureQueryLod(tex, uv).x;
    const float normalMap_Length = length(normalMap);
    const float variation = 1.f - pow(normalMap_Length, 8.f);
    const float minification = saturate(normalMap_Mip - 2.f);

    const float normalVariation = variation * minification;
    const vec3 normalW = (normalMap / normalMap_Length);

    return vec4(normalW, normalVariation);
}

vec3 getNormalWV(in UV_TYPE uv, out float normalVariation) {
    normalVariation = 0.f;

    vec3 normalWV = VAR._normalWV;
#if defined(COMPUTE_TBN) && !defined(USE_CUSTOM_NORMAL_MAP)
    if (dvd_bumpMethod(MATERIAL_IDX) != BUMP_NONE) {
        const vec4 normalData = getNormalMapAndVariation(texNormalMap, uv);
        normalWV = getTBNWV() * normalData.xyz;
        normalVariation = normalData.w;
    }
#endif //COMPUTE_TBN && !USE_CUSTOM_NORMAL_MAP

    normalWV = normalize(normalWV);
#if defined (USE_DOUBLE_SIDED) && !defined(SKIP_DOUBLE_SIDED_NORMALS)
    return gl_FrontFacing ? normalWV : -normalWV;
#else //USE_DOUBLE_SIDED && !SKIP_DOUBLE_SIDED_NORMALS
    return normalWV;
#endif //USE_DOUBLE_SIDED && !SKIP_DOUBLE_SIDED_NORMALS
}

#endif //_MATERIAL_DATA_FRAG_
