#ifndef _BONE_TRANSFORM_VERT_
#define _BONE_TRANSFORM_VERT_

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 13, std430) coherent ACCESS_R buffer dvd_BoneTransforms
{
    mat4 boneTransforms[];
};

#define TRANSFORM_VECTOR(VECTOR, MATRICES)            \
    vec4( dvd_BoneWeight.x * (MATRICES[0] * VECTOR) + \
          dvd_BoneWeight.y * (MATRICES[1] * VECTOR) + \
          dvd_BoneWeight.z * (MATRICES[2] * VECTOR) + \
          dvd_BoneWeight.w * (MATRICES[3] * VECTOR) )


#define GET_BONE_MATRICES(OFFSET)                   \
    mat4[](                                         \
        boneTransforms[OFFSET + dvd_BoneIndices.x], \
        boneTransforms[OFFSET + dvd_BoneIndices.y], \
        boneTransforms[OFFSET + dvd_BoneIndices.z], \
        boneTransforms[OFFSET + dvd_BoneIndices.w])

#if defined(HAS_VELOCITY)

vec4 applyBoneTransforms(in vec4 vector, in uint animationOffset )
{
    return TRANSFORM_VECTOR( vector, GET_BONE_MATRICES( animationOffset ) );
}
#endif //HAS_VELOCITY

void applyBoneTransforms(in uint animationOffset) 
{
    const mat4 transformMatrix[4] = GET_BONE_MATRICES(animationOffset);

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

#endif //_BONE_TRANSFORM_VERT_
