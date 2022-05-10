#ifndef _BONE_TRANSFORM_VERT_
#define _BONE_TRANSFORM_VERT_

vec4 transformVector(in vec4 vectorIn, in mat4[4] transformMatrix) {
    return vec4(transformMatrix[0] * vectorIn + transformMatrix[1] * vectorIn +
                transformMatrix[2] * vectorIn + transformMatrix[3] * vectorIn);
}

#if defined(HAS_VELOCITY)
DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, BUFFER_BONE_TRANSFORMS_PREV, std140) uniform dvd_BoneTransformsPrev
{
    mat4 boneTransformsPrev[MAX_BONE_COUNT_PER_NODE];
};

vec4 applyPrevBoneTransforms(in vec4 vertex) {
    const mat4 transformMatrix[4] = mat4[](
        inBoneWeightData.x * boneTransformsPrev[inBoneIndiceData.x],
        inBoneWeightData.y * boneTransformsPrev[inBoneIndiceData.y],
        inBoneWeightData.z * boneTransformsPrev[inBoneIndiceData.z],
        inBoneWeightData.w * boneTransformsPrev[inBoneIndiceData.w]
    );

    return transformVector(vertex, transformMatrix);
}
#endif //HAS_VELOCITY

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW_SET, BUFFER_BONE_TRANSFORMS, std140) uniform dvd_BoneTransforms {
    mat4 boneTransforms[MAX_BONE_COUNT_PER_NODE];
};

vec4 applyBoneTransforms(in vec4 vertex) {
    const mat4 transformMatrix[4] = mat4[](
        inBoneWeightData.x * boneTransforms[inBoneIndiceData.x],
        inBoneWeightData.y * boneTransforms[inBoneIndiceData.y],
        inBoneWeightData.z * boneTransforms[inBoneIndiceData.z],
        inBoneWeightData.w * boneTransforms[inBoneIndiceData.w]
    );

#if !defined(DEPTH_PASS)
    dvd_Normal  = transformVector(vec4(dvd_Normal, 0.f), transformMatrix).xyz;

#if defined(ENABLE_TBN)
    dvd_Tangent = transformVector(vec4(dvd_Tangent, 0.f), transformMatrix).xyz;
#endif //ENABLE_TBN

#endif //!DEPTH_PASS

    return transformVector(vertex, transformMatrix);
}

#endif //_BONE_TRANSFORM_VERT_
