#ifndef _IMAGE_BASED_LIGHTING_FRAG_
#define _IMAGE_BASED_LIGHTING_FRAG_

layout(binding = TEXTURE_REFLECTION_ENV) uniform samplerCubeArray texEnvironmentCube;
layout(binding = TEXTURE_REFLECTION_SKY) uniform samplerCubeArray texEnvironmentSky;

//x = screen, y = env, z = sky
uniform uvec3 mipCounts;
uniform uint skyLayer;

#define MAX_SCREEN_MIPS (mipCounts.x - 1)
#define MAX_ENV_MIPS (mipCounts.y - 1)
#define MAX_SKY_MIPS (mipCounts.z - 1)

struct ProbeData
{
    vec4 _positionW;
    vec4 _halfExtents;
};

layout(binding = BUFFER_PROBE_DATA, std140) uniform dvd_ProbeBlock {
    ProbeData dvd_Probes[GLOBAL_PROBE_COUNT];
};

#define IsProbeEnabled(P) (uint(P._positionW.w) == 1u)

//ref: https://github.com/urho3d/Urho3D/blob/master/bin/CoreData/Shaders/GLSL/IBL.glsl
vec3 GetSpecularDominantDir(in vec3 normal, in vec3 reflection, in float roughness) {
    const float smoothness = 1.0f - roughness;
    const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    return mix(normal, reflection, lerpFactor);
}

vec3 FixCubeLookup(in vec3 v) {
    const float M = max(max(abs(v.x), abs(v.y)), abs(v.z));
    const float scale = (REFLECTION_PROBE_RESOLUTION - 1) / REFLECTION_PROBE_RESOLUTION;

    if (abs(v.x) != M) v.x += scale;
    if (abs(v.y) != M) v.y += scale;
    if (abs(v.z) != M) v.z += scale;

    return v;
}

//Box Projected Cube Environment Mapping by Bartosz Czuba
vec3 GetAdjustedReflectionWS(in vec3 reflectionWS, in vec3 posWS, in uint probeIdx) {
    const ProbeData probe = dvd_Probes[probeIdx];
    const vec3 EnvBoxHalfSize = probe._halfExtents.xyz;
    const vec3 EnvBoxSize = EnvBoxHalfSize * 2;
    const vec3 EnvBoxStart = probe._positionW.xyz - EnvBoxHalfSize;
        
    const vec3 nrdir = normalize(reflectionWS);
    const vec3 rbmax = (EnvBoxStart + EnvBoxSize - posWS) / nrdir;
    const vec3 rbmin = (EnvBoxStart - posWS) / nrdir;

    const vec3 rbminmax = vec3(
        (nrdir.x > 0.f) ? rbmax.x : rbmin.x,
        (nrdir.y > 0.f) ? rbmax.y : rbmin.y,
        (nrdir.z > 0.f) ? rbmax.z : rbmin.z
    );

    const float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);
    return (posWS + nrdir * fa) - (EnvBoxStart + EnvBoxHalfSize);
}

/// Calculate IBL contributation
///     worldReflect: surface reflection vector in word space
///     worldNormal: surface normal in word space
///     worldPos: surface position in word space
///     probeID: probe index associated with the current environment cube map (used for BPCEM)
///     roughness: surface roughness
vec3 GetCubeReflection(in vec3 worldReflect, in vec3 worldNormal, in vec3 worldPos, in uint probeID, in float roughness) {
    if (probeID > 0u) {
        const uint probeIDX = probeID - 1u;
#if 1
        const vec3 dominantDir = GetSpecularDominantDir(worldNormal, worldReflect, roughness);
        const vec3 fixedCubeDir = FixCubeLookup(dominantDir);
        const vec3 bpcemDir = GetAdjustedReflectionWS(fixedCubeDir, worldPos, probeIDX);
#else
        const vec3 bpcemDir = GetAdjustedReflectionWS(worldReflect, worldPos, probeIDX);
#endif
        return textureLod(texEnvironmentCube, vec4(bpcemDir, float(probeIDX)), roughness * MAX_ENV_MIPS).rgb;
    }

    return textureLod(texEnvironmentSky, vec4(worldReflect, float(skyLayer)), roughness * MAX_SKY_MIPS).rgb;
}


#endif //_IMAGE_BASED_LIGHTING_FRAG_
