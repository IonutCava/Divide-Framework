#ifndef _LIGHTING_DEFAULTS_VERT_
#define _LIGHTING_DEFAULTS_VERT_

// Copyright (c) 2026 DIVIDE-Studio

vec3 TransformNormal(in vec3 normal, in vec4 rotation, in vec3 scale)
{
    const vec3 scaleSign = mix(vec3(-1.f), vec3(1.f), greaterThanEqual(scale, vec3(0.f)));
    const vec3 safeScale = scaleSign * max(abs(scale), vec3(1e-8f));
    return normalize(QuaternionRotate(rotation, normal / safeScale));
}

#if defined(COMPUTE_TBN)
mat3 computeTBN(in vec4 rotation, in vec3 scale)
{
    const vec3 N = TransformNormal(dvd_Normal, rotation, scale);
    vec3 T = QuaternionRotate(rotation, dvd_Tangent * scale);
    T -= dot(T, N) * N;
    if (dot(T, T) < 1e-12f) {
        const vec3 axis = abs(N.y) < 0.999f ? vec3(0.f, 1.f, 0.f) : vec3(1.f, 0.f, 0.f);
        T = cross(axis, N);
    }
    T = normalize(T);
    const float handedness = scale.x * scale.y * scale.z < 0.f ? -1.f : 1.f;
    const vec3 B = cross(N, T) * handedness;
    return mat3(dvd_ViewMatrix) * mat3(T, B, N);
}
#endif //COMPUTE_TBN

void computeLightVectors(in vec4 rotation, in vec3 scale)
{
#if defined(COMPUTE_TBN)
    VAR._tbnWV = computeTBN(rotation, scale);
    VAR._normalWV = VAR._tbnWV[2];
#else // COMPUTE_TBN
    VAR._normalWV = normalize(mat3(dvd_ViewMatrix) * TransformNormal(dvd_Normal, rotation, scale));
#endif // COMPUTE_TBN
}

void computeLightVectors(in NodeTransformData data)
{
    computeLightVectors(data._transform._rotation, data._transform._scale.xyz);
}

#endif //_LIGHTING_DEFAULTS_VERT_
