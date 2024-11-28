#ifndef _BONE_TRANSFORM_VERT_
#define _BONE_TRANSFORM_VERT_

#if defined(USE_DUAL_QUAT_SKINNING)

struct DualQuaternion
{
    vec4 a;
    vec4 b;
};

#define transform_t DualQuaternion

#else //USE_DUAL_QUAT_SKINNING

#define transform_t mat4
#endif //USE_DUAL_QUAT_SKINNING

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 13, std430) coherent ACCESS_R buffer dvd_BoneTransforms
{
    transform_t boneTransforms[];
};

#define GET_BONE_TRANSFORMS(OFFSET)                 \
    transform_t[](                                  \
        boneTransforms[OFFSET + dvd_BoneIndices.x], \
        boneTransforms[OFFSET + dvd_BoneIndices.y], \
        boneTransforms[OFFSET + dvd_BoneIndices.z], \
        boneTransforms[OFFSET + dvd_BoneIndices.w])

#if defined(USE_DUAL_QUAT_SKINNING)

mat2x4 getBoneTransform(in DualQuaternion[4] quats)
{
    vec4 weights = dvd_BoneWeight;

    const mat2x4 dq0 = mat2x4(quats[0].a, quats[0].b);
    const mat2x4 dq1 = mat2x4(quats[1].a, quats[1].b);
    const mat2x4 dq2 = mat2x4(quats[2].a, quats[2].b);
    const mat2x4 dq3 = mat2x4(quats[3].a, quats[3].b);

    const float sumWeight = weights.x + weights.y + weights.z + weights.w;

    weights.y *= sign(dot(dq0[0], dq1[0]));
    weights.z *= sign(dot(dq0[0], dq2[0]));
    weights.w *= sign(dot(dq0[0], dq3[0]));


    mat2x4 result = weights.x * dq0 + weights.y * dq1 + weights.z * dq2 + weights.w * dq3;

    result[0][3] += int(sumWeight < 1.f) * (1.f - sumWeight);

    // Normalise
    const float norm = length(result[0]);
    return result / norm;
}

mat4 getBoneMatrix(in DualQuaternion[4] quats)
{
    const mat2x4 bone = getBoneTransform(quats);

    const vec4 r = bone[0];
    const vec4 t = bone[1];

    return mat4(             1. - (2. * r.y * r.y) - (2. * r.z * r.z),                   (2. * r.x * r.y) + (2. * r.w * r.z),                   (2. * r.x * r.z) - (2. * r.w * r.y), 0.,
                                  (2. * r.x * r.y) - (2. * r.w * r.z),              1. - (2. * r.x * r.x) - (2. * r.z * r.z),                   (2. * r.y * r.z) + (2. * r.w * r.x), 0.,
                                  (2. * r.x * r.z) + (2. * r.w * r.y),                   (2. * r.y * r.z) - (2. * r.w * r.x),              1. - (2. * r.x * r.x) - (2. * r.y * r.y), 0.,
                2. * (-t.w * r.x + t.x * r.w - t.y * r.z + t.z * r.y), 2. * (-t.w * r.y + t.x * r.z + t.y * r.w - t.z * r.x), 2. * (-t.w * r.z - t.x * r.y + t.y * r.x + t.z * r.w), 1.);
}

vec4 transformDualQuat(in vec4 vector, in DualQuaternion[4] quats)
{
    return getBoneMatrix(quats) * vector;
}

#define TRANSFORM_VECTOR(VECTOR, QUATS)            \
    transformDualQuat(VECTOR, QUATS)

#else //USE_DUAL_QUAT_SKINNING

#define TRANSFORM_VECTOR(VECTOR, MATRICES)            \
    vec4( dvd_BoneWeight.x * (MATRICES[0] * VECTOR) + \
          dvd_BoneWeight.y * (MATRICES[1] * VECTOR) + \
          dvd_BoneWeight.z * (MATRICES[2] * VECTOR) + \
          dvd_BoneWeight.w * (MATRICES[3] * VECTOR) )

#endif //USE_DUAL_QUAT_SKINNING

void applyBoneTransforms(in uint animationOffset) 
{
    const transform_t transformMatrix[4] = GET_BONE_TRANSFORMS(animationOffset);

    dvd_Vertex = TRANSFORM_VECTOR(dvd_Vertex, transformMatrix);

#if !defined(DEPTH_PASS)

#if defined(HAS_NORMAL_ATTRIBUTE)
        dvd_Normal = TRANSFORM_VECTOR( vec4(dvd_Normal.xyz, 0.f), transformMatrix ).xyz;
#endif //HAS_NORMAL_ATTRIBUTE

#if defined(ENABLE_TBN) && defined(HAS_TANGENT_ATTRIBUTE)
        dvd_Tangent = TRANSFORM_VECTOR( vec4(dvd_Tangent.xyz, 0.f), transformMatrix ).xyz;
#endif //ENABLE_TBN && HAS_TANGENT_ATTRIBUTE

#endif //!DEPTH_PASS

}

#if defined(HAS_VELOCITY)
vec4 applyBoneTransforms(in vec4 vector, in uint animationOffset)
{
    return TRANSFORM_VECTOR(vector, GET_BONE_TRANSFORMS(animationOffset));
}
#endif //HAS_VELOCITY

#endif //_BONE_TRANSFORM_VERT_
