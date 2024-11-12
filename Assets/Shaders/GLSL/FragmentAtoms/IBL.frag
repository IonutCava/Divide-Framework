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

#include "shadowMapping.frag"

//Global sky light layer index: SKY_LIGHT_LAYER_IDX
// eg: vec4 skyIrradiance = texture(texEnvIrradiance, vec4(coords, SKY_LIGHT_LAYER_IDX);
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 0) uniform samplerCubeArray texEnvPrefiltered;
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 1) uniform samplerCubeArray texEnvIrradiance;
DESCRIPTOR_SET_RESOURCE(PER_FRAME, 2) uniform sampler2D texBRDFLut;

DESCRIPTOR_SET_RESOURCE(PER_PASS, 3) uniform sampler2D texSSR;

struct ProbeData
{
    vec4 _positionW;
    vec4 _halfExtents;
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_FRAME, 7, std140) uniform dvd_ProbeBlock
{
    ProbeData dvd_Probes[GLOBAL_PROBE_COUNT];
};


#define IsProbeEnabled(P) (uint(P._positionW.w) == 1u)

// A Fresnel term that dampens rough specular reflections.
// https://seblagarde.wordpress.com/2011/08/17/hello-world/

vec3 computeFresnelSchlickRoughness(in float cosTheta, in vec3 F0, in float roughness)
{
    return F0 + (max(vec3(1.f - roughness), F0) - F0) * pow(Saturate(1.f - cosTheta), 5.f);
}

vec3 computeFresnelSchlickRoughness(in vec3 H, in vec3 V, in vec3 F0, in float roughness)
{
    return computeFresnelSchlickRoughness( Saturate( dot( H, V ) ), F0, roughness);
}

//ref: https://github.com/urho3d/Urho3D/blob/master/bin/CoreData/Shaders/GLSL/IBL.glsl
vec3 GetSpecularDominantDir(in vec3 normal, in vec3 reflection, in float roughness)
{
    const float smoothness = 1.f - roughness;
    const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    return mix(normal, reflection, lerpFactor);
}

vec3 FixCubeLookup(in vec3 v)
{
    const float M = max(max(abs(v.x), abs(v.y)), abs(v.z));
    const float scale = (REFLECTION_PROBE_RESOLUTION - 1) / REFLECTION_PROBE_RESOLUTION;

    if (abs(v.x) != M)
    {
        v.x += scale;
    }

    if (abs(v.y) != M)
    {
        v.y += scale;
    }

    if (abs(v.z) != M)
    {
        v.z += scale;
    }

    return v;
}

vec3 GetCubeReflectionDirection(in vec3 viewDirectionWV, in vec3 normalWV, in vec3 positionW, in uint probeID, in float roughness)
{
    const vec3 reflectionWV = reflect(-viewDirectionWV, normalWV);

    if (probeID != SKY_LIGHT_LAYER_IDX) 
    {
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

vec3 getIBLAmbient(in PBRMaterial material)
{
    return (dvd_AmbientColour.rgb * material._diffuseColour * material._occlusion);
}

#if !defined(NO_ENV_MAPPING) && !defined(NO_IBL)
vec4 getReflectionLookup(in float roughness, in vec3 viewDirectionWV, in vec3 normalWV, in vec3 positionW, in uint probeID)
{
    return vec4(GetCubeReflectionDirection(viewDirectionWV, normalWV, positionW, probeID, roughness), float(probeID));
}


#if defined(SHADING_MODE_PBR)

vec3 ApplyIBL(in PBRMaterial material, in vec3 viewDirectionWV, in vec3 normalWV, in float NdotV, in vec3 positionW, in uint probeID)
{
    if ( probeID == SKY_LIGHT_LAYER_IDX && getWorldAO() < 1.f )
    {
        return vec3( 0.f );
    }

    const vec4 reflectionLookup = getReflectionLookup(material._roughness, viewDirectionWV, normalWV, positionW, probeID);

    const vec3 irradiance = texture(texEnvIrradiance, reflectionLookup).rgb;
    const vec3 prefiltered = textureLod(texEnvPrefiltered, reflectionLookup, (material._roughness * REFLECTION_PROBE_MIP_COUNT)).rgb;
    const vec2 envBRDF = texture(texBRDFLut, vec2(NdotV, material._roughness)).rg;
    const vec3 diffuse = irradiance * material._diffuseColour;
    const vec3 ambient = getIBLAmbient(material);

    const vec3 F = computeFresnelSchlickRoughness(NdotV, material._F0, material._roughness);
    const vec3 kS = F;
    const vec3 kD = (1.f - kS) * (1.f - material._metallic);

    const vec3 specular = prefiltered * (F * envBRDF.x + envBRDF.y);
    return (kD * diffuse + specular) * material._occlusion + ambient;
}

#else //SHADING_MODE_PBR

vec3 ApplyIBL(in PBRMaterial material, in vec3 viewDirectionWV, in vec3 normalWV, in float NdotV, in vec3 positionW, in uint probeID)
{
    if ( probeID == SKY_LIGHT_LAYER_IDX && getWorldAO() < 1.f )
    {
        return vec3(0.f);
    }

    const vec4 reflectionLookup = getReflectionLookup(material._roughness, viewDirectionWV, normalWV, positionW, probeID);
    const vec3 prefiltered = textureLod(texEnvPrefiltered, reflectionLookup, (material._roughness * REFLECTION_PROBE_MIP_COUNT)).rgb;
    const vec3 ambient = getIBLAmbient(material);

    // Don't know. Shinnier => more reflective?
    const float kD = material._specular.a / MAX_SHININESS;
    return (easeInQuint(kD) * prefiltered) * material._occlusion + ambient;
}

#endif //SHADING_MODE_PBR

#else //!NO_ENV_MAPPING && !NO_IBL

#define ApplyIBL(M,V,N,nDv,P,ID) getIBLAmbient(M)

#endif //!NO_ENV_MAPPING && !NO_IBL

#if defined(NO_SSR) || defined(NO_IBL)

#define ApplySSR(Mr,R) (R)

#else  //NO_SSR || NO_IBL

vec3 ApplySSR(in float roughness, in vec3 radianceIn)
{
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
