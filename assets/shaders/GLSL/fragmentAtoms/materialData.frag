#ifndef _MATERIAL_DATA_FRAG_
#define _MATERIAL_DATA_FRAG_

#if defined(SHADING_MODE_PBR_MR) || defined(SHADING_MODE_PBR_SG)
#define SHADING_MODE_PBR
#endif //SHADING_MODE_PBR_MR || SHADING_MODE_PBR_SG
#if defined(NO_POST_FX)
#if !defined(NO_FOG)
#define NO_FOG
#endif //!NO_FOG
#endif //NO_POST_FX

struct PBRMaterial
{
    vec4  _specular;
    vec3  _diffuseColour;
    vec3 _F0;
    vec3  _emissive;
    float _occlusion;
    float _metallic;
    float _roughness;
#if defined(SHADING_MODE_PBR)
    float _a;
#endif //SHADING_MODE_PBR
};

#if !defined(MAIN_DISPLAY_PASS)

#if !defined(NO_SSAO)
#define NO_SSAO
#endif //!NO_SSAO

#if !defined(NO_FOG)
#define NO_FOG
#endif //!NO_FOG
#else//!MAIN_DISPLAY_PASS
#if !defined(PRE_PASS)
#if defined(MSAA_SCREEN_TARGET)
DESCRIPTOR_SET_RESOURCE(PER_PASS, 0) uniform sampler2DMS texSceneNormals;
#else//MSAA_SCREEN_TARGET
DESCRIPTOR_SET_RESOURCE(PER_PASS, 0) uniform sampler2D texSceneNormals;
#endif //MSAA_SCREEN_TARGET
#endif //PRE_PASS
#endif //!MAIN_DISPLAY_PASS

#include "utility.frag"
#include "texturing.frag"

#if defined(COMPUTE_TBN)
#include "bumpMapping.frag"
#endif //COMPUTE_TBN

#if !defined(NO_SSAO)
DESCRIPTOR_SET_RESOURCE(PER_PASS, 4) uniform sampler2D texSSAO;
#endif //NO_SSAO

#if !defined(PRE_PASS)

#if defined(MSAA_SCREEN_TARGET)
#define sampleTexSceneNormals() texelFetch(texSceneNormals, ivec2(gl_FragCoord.xy), gl_SampleID)
#else  //MSAA_SCREEN_TARGET
#define sampleTexSceneNormals() texture(texSceneNormals, dvd_screenPositionNormalised)
#endif //MSAA_SCREEN_TARGET
#endif //!PRE_PASS

#if defined(USE_CUSTOM_TEXTURE_OMR)
void getTextureOMR(in bool usePacked, in vec3 uv, in uvec3 texOps, inout vec3 OMR);
#else //USE_CUSTOM_TEXTURE_OMR
#if defined(NO_OMR_TEX)
#define getTextureOMR(usePacked, uv, texOps, OMR)
#else //NO_OMR_TEX
void getTextureOMR(in bool usePacked, in vec3 uv, in uvec3 texOps, inout vec3 OMR) {
#if defined(USE_METALNESS_TEXTURE)
    if (usePacked) {
        OMR = texture(texMetalness, uv).rgb;
    }
    else 
#endif //USE_METALNESS_TEXTURE
    {
#if !defined(NO_OCCLUSION_TEX) && defined(USE_OCCLUSION_TEXTURE)
        if (texOps.x != TEX_NONE) {
            OMR.r = texture(texOcclusion, uv).r;
        }
#endif //!NO_OCCLUSION_TEX && USE_OCCLUSION_TEXTURE
#if !defined(NO_METALNESS_TEX) && defined(USE_METALNESS_TEXTURE)
        if (texOps.y != TEX_NONE) {
            OMR.g = texture(texMetalness, uv).r;
        }
#endif //NO_METALNESS_TEX && USE_METALNESS_TEXTURE
#if !defined(NO_ROUGHNESS_TEX) && defined(USE_ROUGHNESS_TEXTURE)
        if (texOps.z != TEX_NONE) {
            OMR.b = texture(texRoughness, uv).r;
        }
#endif //!NO_ROUGHNESS_TEX && USE_ROUGHNESS_TEXTURE
    }
}
#endif //NO_OMR_TEX
#endif //USE_CUSTOM_TEXTURE_OMR

vec4 ApplyTexOperation(in vec4 a, in vec4 b, in uint texOperation) {
    //hot pink to easily spot it in a crowd
    vec4 retColour = a;

    // Read from the second texture (if any)
    switch (texOperation) {
        
        default             : retColour = vec4(0.7743f, 0.3188f, 0.5465f, 1.f);               break;
        case TEX_NONE       : /*NOP*/                                                         break;
        case TEX_MULTIPLY   : retColour *= b;                                                 break;
        case TEX_ADD        : retColour.rgb += b.rgb; retColour.a *= b.a;                     break;
        case TEX_SUBTRACT   : retColour -= b;                                                 break;
        case TEX_DIVIDE     : retColour /= b;                                                 break;
        case TEX_SMOOTH_ADD : retColour = (retColour + b) - (retColour * b);                  break;
        case TEX_SIGNED_ADD : retColour += b - 0.5f;                                          break;
        case TEX_DECAL      : retColour  = vec4(mix(retColour.rgb, b.rgb, b.a), retColour.a); break;
        case TEX_REPLACE    : retColour  = b;                                                 break;
    }

    return retColour;
}

#if defined(USE_CUSTOM_SPECULAR)
vec4 getSpecular(in NodeMaterialData matData, in vec3 uv);
#else //USE_CUSTOM_SPECULAR
vec4 getSpecular(in NodeMaterialData matData, in vec3 uv) {
    vec4 specData = vec4(dvd_Specular(matData), dvd_SpecularStrength(matData));

    // Specular strength is baked into the specular colour, so even if we use a texture, we still need to multiply the strength in
#if defined(USE_SPECULAR_TEXTURE)
    const uint texOp = dvd_TexOpSpecular(matData);
    if (texOp != TEX_NONE) {
        specData.rgb = ApplyTexOperation(vec4(specData.rgb, 1.f),
                                         texture(texSpecular, uv),
                                         texOp).rgb;
    }
#endif //USE_SPECULAR_TEXTURE

    return specData;
}
#endif //USE_CUSTOM_SPECULAR

#if defined(USE_CUSTOM_EMISSIVE)
vec3 getEmissiveColour(in NodeMaterialData matData, in vec3 uv);
#else //USE_CUSTOM_EMISSIVE
vec3 getEmissiveColour(in NodeMaterialData matData, in vec3 uv) {
#if defined(USE_EMISSIVE_TEXTURE)
    const uint texOp = dvd_TexOpEmissive(matData);
    if (texOp != TEX_NONE) 
    {
        return texture(texEmissive, uv).rgb;
    }
#endif //USE_EMISSIVE_TEXTURE

    return dvd_EmissiveColour(matData);
}
#endif //USE_CUSTOM_EMISSIVE

float SpecularToMetalness(in vec3 specular, in float power) {
    return 0.f; //sqrt(power / MAX_SHININESS);
}

float SpecularToRoughness(in vec3 specular, in float power) {
    const float roughnessFactor = 1.f - sqrt(power / MAX_SHININESS);
    const float luminance = Luminance(specular);
    // Specular intensity directly impacts roughness regardless of shininess
    return 1.f - Saturate(pow(roughnessFactor, 2) * luminance);
}

float getRoughness(in NodeMaterialData matData, in vec2 uv, in float normalVariation) {
    float roughness = 0.f;

#if defined(SHADING_MODE_BLINN_PHONG)
    // Deduce a roughness factor from specular colour and shininess
    const vec4 specular = getSpecular(matData, vec3(uv, 0));
    roughness = SpecularToRoughness(specular.rgb, specular.a);
#else  //SHADING_MODE_BLINN_PHONG
    vec3 OMR = dvd_OMR(matData);
    getTextureOMR(dvd_UsePackedOMR(matData), vec3(uv, 0), dvd_TexOperationsB(matData).xyz, OMR.rgb);
    roughness = mix(OMR.b, 1.f, normalVariation);
#endif //SHADING_MODE_BLINN_PHONG

    return roughness;
}

PBRMaterial initMaterialProperties(in NodeMaterialData matData, in vec3 albedo, in vec2 uv, in vec3 N, in float nDotV) {
    PBRMaterial material;
    material._emissive = getEmissiveColour(matData, vec3(uv, 0));
    material._specular = getSpecular(matData, vec3(uv, 0));

    vec3 OMR = dvd_OMR(matData);

    getTextureOMR(dvd_UsePackedOMR(matData), vec3(uv, 0), dvd_TexOperationsB(matData).xyz, OMR.rgb);
    material._occlusion = OMR.r;

#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    material._roughness = sampleTexSceneNormals().b;
#else //MAIN_DISPLAY_PASS && !PRE_PASS
#if defined(SHADING_MODE_BLINN_PHONG)
    material._roughness = SpecularToRoughness(material._specular.rgb, material._specular.a);
#else //SHADING_MODE_BLINN_PHONG
    material._roughness = OMR.b;
#endif //SHADING_MODE_BLINN_PHONG
#endif //MAIN_DISPLAY_PASS && !PRE_PASS

#if defined(SHADING_MODE_BLINN_PHONG)
    material._metallic = SpecularToMetalness(material._specular.rgb, material._specular.a);
#else //SHADING_MODE_BLINN_PHONG
    material._metallic = OMR.g;
#endif //SHADING_MODE_BLINN_PHONG
    const vec3 albedoIn = albedo + dvd_Ambient(matData);

    const vec3 dielectricSpecular = vec3(0.04f);
    const vec3 black = vec3(0.f);

    material._diffuseColour = mix(albedoIn * (vec3(1.f) - dielectricSpecular), black, material._metallic);
    material._F0 = mix(dielectricSpecular, albedoIn, material._metallic);

    return material;
}


#if defined(ENABLE_TBN)
#define getTBNWV() VAR._tbnWV
#else //ENABLE_TBN
#define getTBNWV() mat3(WORLD_X_AXIS, WORLD_Z_AXIS, WORLD_Y_AXIS)
#endif //ENABLE_TBN

#if !defined(PRE_PASS)
// Reduce specular aliasing by producing a modified roughness value
// Tokuyoshi et al. 2019. Improved Geometric Specular Antialiasing.
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
float specularAntiAliasing(in vec3 N, in float a) {
    // normal-based isotropic filtering
    // this is originally meant for deferred rendering but is a bit simpler to implement than the forward version
    // saves us from calculating uv offsets and sampling textures for every light

     // squared std dev of pixel filter kernel (in pixels)
    #define SIGMA2 0.25f
    // clamping threshold
    #define KAPPA  0.18f

    const vec3 dndu = dFdx(N);
    const vec3 dndv = dFdy(N);
    const float variance = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
    const float kernelRoughness2 = min(2.f * variance, KAPPA);
    return Saturate(a + kernelRoughness2);
}
#endif //!PRE_PASS

vec4 getTextureColour(in NodeMaterialData data, in vec3 uv) {
    vec4 colour = dvd_BaseColour(data);

    const uvec2 texOps = dvd_TexOperationsA(data).xy;

#if defined(USE_UNIT0_TEXTURE)
    if (texOps.x != TEX_NONE) {
        colour = ApplyTexOperation(colour, texture(texDiffuse0, uv), texOps.x);
    }
#endif //USE_UNIT0_TEXTURE
#if defined(USE_UNIT1_TEXTURE)
    if (texOps.y != TEX_NONE) {
        colour = ApplyTexOperation(colour, texture(texDiffuse1, uv), texOps.y);
    }
#endif //USE_UNIT1_TEXTURE

    return colour;
}

#if defined(HAS_TRANSPARENCY)

#if defined(USE_ALPHA_DISCARD)
float getAlpha(in NodeMaterialData data, in vec3 uv) 
{
#if defined(USE_OPACITY_TEXTURE)
    if (dvd_TexOpOpacity(data) != TEX_NONE)
    {
        return dvd_UseOpacityAlphaChannel(data) ? texture(texOpacityMap, uv).a : texture(texOpacityMap, uv).r;
    }
#endif //USE_OPACITY_TEXTURE
#if defined(USE_UNIT0_TEXTURE)
    if (dvd_UseAlbedoTextureAlphaChannel(data) && dvd_TexOpUnit0(data) != TEX_NONE)
    {
        return texture(texDiffuse0, uv).a;
    }
#endif //USE_UNIT0_TEXTURE
    return dvd_BaseColour(data).a;
}
#endif //USE_ALPHA_DISCARD

vec4 getAlbedo(in NodeMaterialData data, in vec3 uv)
{
    vec4 albedo = getTextureColour(data, uv);
#if defined(USE_OPACITY_TEXTURE)
    if (dvd_TexOpOpacity(data) != TEX_NONE)
    {
        albedo.a = dvd_UseOpacityAlphaChannel(data) ? texture(texOpacityMap, uv).a : texture(texOpacityMap, uv).r;
    }
#endif //USE_OPACITY_TEXTURE
    if (!dvd_UseAlbedoTextureAlphaChannel(data))
    {
        albedo.a = dvd_BaseColour(data).a;
    }
    return albedo;
}
#else //HAS_TRANSPARENCY
#define getAlbedo getTextureColour
#endif //HAS_TRANSPARENCY

#if defined(MAIN_DISPLAY_PASS)
vec3 getNormalMap(in sampler2DArray tex, in vec3 uv) {
    return normalize(2.f * texture(tex, uv).rgb - 1.f);
}

vec3 getNormalMap(in sampler2DArray tex, in vec3 uv, out float normalVariation) {
    const vec3 normalMap = 2.f * texture(tex, uv).rgb - 1.f;
    const float normalMap_Mip = textureQueryLod(tex, uv.xy).x;
    const float normalMap_Length = length(normalMap);

    // Try to reduce specular aliasing by increasing roughness when minified normal maps have high variation.
    const float variation = 1.f - pow(normalMap_Length, 8.f);
    const float minification = Saturate(normalMap_Mip - 2.f);

    normalVariation = Saturate(variation * minification);

    return normalMap / normalMap_Length;
}
#else //MAIN_DISPLAY_PASS
vec3 getNormalMap(in sampler2DArray tex, in vec3 uv) {
    return normalize(2.f * texture(tex, uv).rgb - 1.f);
}
vec3 getNormalMap(in sampler2DArray tex, in vec3 uv, out float normalVariation) {
    normalVariation = 0.f;
    return normalize(2.f * texture(tex, uv).rgb - 1.f);
}
#endif //MAIN_DISPLAY_PASS

vec3 getNormalWV(in NodeMaterialData data, in vec3 uv, out float normalVariation) {
    normalVariation = 0.f;
#if defined(MAIN_DISPLAY_PASS) && !defined(PRE_PASS)
    return normalize(unpackNormal(sampleTexSceneNormals().rg));
#else //MAIN_DISPLAY_PASS && !PRE_PASS
    vec3 normalWV = VAR._normalWV;
#if defined(COMPUTE_TBN)
    if (dvd_BumpMethod(MATERIAL_IDX) != BUMP_NONE) {
        const vec3 normalData = getNormalMap(texNormalMap, uv, normalVariation);
        normalWV = getTBNWV() * normalData;
    }
#endif //COMPUTE_TBN
    return normalize(normalWV) * (dvd_IsDoubleSided(data) ? (2.f * float(gl_FrontFacing) - 1.f) : 1.f);
#endif  //MAIN_DISPLAY_PASS && !PRE_PASS
}

vec3 getNormalWV(in NodeMaterialData data, in vec3 uv) {
    float variation = 0.f;
    return getNormalWV(data, uv, variation);
}
#endif //_MATERIAL_DATA_FRAG_
