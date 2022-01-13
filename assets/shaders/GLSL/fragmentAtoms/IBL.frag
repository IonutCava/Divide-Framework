#ifndef _IMAGE_BASED_LIGHTING_FRAG_
#define _IMAGE_BASED_LIGHTING_FRAG_

#if defined(NO_POST_FX)
#if !defined(NO_SSAO)
#define NO_SSAO
#endif //!NO_SSAO

#if !defined(NO_SSR)
#define NO_SSR
#endif //!NO_SSR

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

//Global sky light layer index: SKY_LIGHT_LAYER_IDX
// eg: vec4 skyIrradiance = texture(texEnvIrradiance, vec4(coords, SKY_LIGHT_LAYER_IDX);
layout(binding = TEXTURE_REFLECTION_PREFILTERED) uniform samplerCubeArray texEnvPrefiltered;
layout(binding = TEXTURE_IRRADIANCE) uniform samplerCubeArray texEnvIrradiance;
layout(binding = TEXTURE_BRDF_LUT) uniform sampler2D texBRDFLut;
layout(binding = TEXTURE_SSR_SAMPLE) uniform sampler2D texSSR;

struct ProbeData
{
    vec4 _positionW;
    vec4 _halfExtents;
};

layout(binding = BUFFER_PROBE_DATA, std140) uniform dvd_ProbeBlock {
    ProbeData dvd_Probes[GLOBAL_PROBE_COUNT];
};

#define IsProbeEnabled(P) (uint(P._positionW.w) == 1u)

// A Fresnel term that dampens rough specular reflections.
// https://seblagarde.wordpress.com/2011/08/17/hello-world/

vec3 computeFresnelSchlickRoughness(in float cosTheta, in vec3 F0, in float roughness) {
    return F0 + (max(vec3(1.f - roughness), F0) - F0) * pow(saturate(1.f - cosTheta), 5.f);
}

vec3 computeFresnelSchlickRoughness(in vec3 H, in vec3 V, in vec3 F0, in float roughness) {
    const float cosTheta = saturate(dot(H, V));
    return computeFresnelSchlickRoughness(cosTheta, F0, roughness);
}

//ref: https://github.com/urho3d/Urho3D/blob/master/bin/CoreData/Shaders/GLSL/IBL.glsl
vec3 GetSpecularDominantDir(in vec3 normal, in vec3 reflection, in float roughness) {
    const float smoothness = 1.f - roughness;
    const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    return mix(normal, reflection, lerpFactor);
}

vec3 FixCubeLookup(in vec3 v) {
    const float M = max(max(abs(v.x), abs(v.y)), abs(v.z));
    const float scale = (REFLECTION_PROBE_RESOLUTION - 1) / REFLECTION_PROBE_RESOLUTION;

    if (abs(v.x) != M) {
        v.x += scale;
    }
    if (abs(v.y) != M) {
        v.y += scale;
    }
    if (abs(v.z) != M) {
        v.z += scale;
    }

    return v;
}

vec3 GetCubeReflectionDirection(in vec3 viewDirectionWV, in vec3 normalWV, in vec3 positionW, in uint probeID, in float roughness) {
    const vec3 reflectionWV = reflect(-viewDirectionWV, normalWV);

    if (probeID != SKY_LIGHT_LAYER_IDX) {
        const ProbeData probe = dvd_Probes[probeID];

        //Box Projected Cube Environment Mapping by Bartosz Czuba
        const vec3 dominantDirW = normalize(mat3(dvd_InverseViewMatrix) * GetSpecularDominantDir(normalWV, reflectionWV, roughness));
        const vec3 nrdir = normalize(FixCubeLookup(dominantDirW));

        const vec3 EnvBoxHalfSize = probe._halfExtents.xyz;
        const vec3 EnvBoxSize = EnvBoxHalfSize * 2;
        const vec3 EnvBoxStart = probe._positionW.xyz - EnvBoxHalfSize;
        const vec3 rbmax = (EnvBoxStart + EnvBoxSize - positionW) / nrdir;
        const vec3 rbmin = (EnvBoxStart - positionW) / nrdir;

        const vec3 rbminmax = vec3(
            (nrdir.x > 0.f) ? rbmax.x : rbmin.x,
            (nrdir.y > 0.f) ? rbmax.y : rbmin.y,
            (nrdir.z > 0.f) ? rbmax.z : rbmin.z
        );

        const float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);
        return (positionW + nrdir * fa) - (EnvBoxStart + EnvBoxHalfSize);
    }

    return reflectionWV;
}

#if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
vec3 ApplyIBL(in PBRMaterial material, in vec3 viewDirectionWV, in vec3 normalWV, in float NdotV, in vec3 positionW, in uint probeID) {
    const vec4 reflectionLookup = vec4(
        GetCubeReflectionDirection(viewDirectionWV, 
                                   normalWV,
                                   positionW,
                                   probeID,
                                   material._roughness),
        float(probeID));

    const vec3 irradiance = texture(texEnvIrradiance, reflectionLookup).rgb;
    const vec3 prefiltered = textureLod(texEnvPrefiltered, reflectionLookup, (material._roughness * REFLECTION_PROBE_MIP_COUNT)).rgb;
    const vec2 envBRDF = texture(texBRDFLut, vec2(NdotV, material._roughness)).rg;
    const vec3 diffuse = irradiance * material._diffuseColour;
    const vec3 ambient = (dvd_AmbientColour.rgb * material._diffuseColour * material._occlusion);

    const vec3 F = computeFresnelSchlickRoughness(NdotV, material._F0, material._roughness);
#if defined(SHADING_MODE_BLINN_PHONG)
#define DielectricSpecular vec3(0.04f)
    const vec3 kS = saturate(F - DielectricSpecular);
    const vec3 kD = (1.f - kS) * (sqrt(Luminance(material._specular.rgb)));
#undef DielectricSpecular
#else //SHADING_MODE_BLINN_PHONG
    const vec3 kS = F;
    const vec3 kD = (1.f - kS) * (1.f - material._metallic);
#endif //SHADING_MODE_BLINN_PHONG

    const vec3 specular = prefiltered * (F * envBRDF.x + envBRDF.y);
    return (kD * diffuse + specular) * material._occlusion + ambient;
}
#else //!NO_ENV_MAPPING && !NO_IBL
#define ApplyIBL(M,V,N,nDv,P,ID) (dvd_AmbientColour.rgb * M._diffuseColour * M._occlusion)
#endif //!NO_ENV_MAPPING && !NO_IBL

#if defined(NO_SSR) || defined(NO_IBL)
#define ApplySSR(Mr,R) (R)
#else  //NO_SSR || NO_IBL
vec3 ApplySSR(in float roughness, in vec3 radianceIn) {
    const vec4 ssr = texture(texSSR, dvd_screenPositionNormalised);
    return mix(mix(radianceIn, ssr.rgb, ssr.a),
               radianceIn,
               roughness);
}
#endif  //NO_SSR || NO_IBL

#if defined(NO_SSAO)
#define ApplySSAO(R) (R)
#else  //NO_SSAO
#define ApplySSAO(R) (R * texture(texSSAO, dvd_screenPositionNormalised).r)
#endif  //NO_SSAO

#endif //_IMAGE_BASED_LIGHTING_FRAG_
