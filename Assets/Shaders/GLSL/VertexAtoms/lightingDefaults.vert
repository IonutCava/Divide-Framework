#ifndef _LIGHTING_DEFAULTS_VERT_
#define _LIGHTING_DEFAULTS_VERT_

#if defined(COMPUTE_TBN)
mat3 computeTBN(in float4 rotation)
{
    const vec3 N = normalize(QuaternionRotate(rotation, dvd_Normal));
    vec3 T = normalize(QuaternionRotate(rotation, dvd_Tangent));
    // re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    const vec3 B = cross(N, T);
    return mat3(dvd_ViewMatrix) * mat3(T, B, N);
}
#endif //COMPUTE_TBN

void computeLightVectors(in NodeTransformData data)
{

#if defined(COMPUTE_TBN)
    VAR._tbnWV = computeTBN(data._transform._rotation);
    VAR._normalWV = VAR._tbnWV[2];
#else // COMPUTE_TBN
    VAR._normalWV = normalize(mat3(dvd_ViewMatrix) * QuaternionRotate(data._transform._rotation, dvd_Normal));
#endif // COMPUTE_TBN
}

#endif //_LIGHTING_DEFAULTS_VERT_
