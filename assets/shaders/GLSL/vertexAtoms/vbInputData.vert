#ifndef _VB_INPUT_DATA_VERT_
#define _VB_INPUT_DATA_VERT_

#include "vertexDefault.vert"
#include "nodeBufferedInput.cmn"
#if defined(USE_GPU_SKINNING)
#include "boneTransforms.vert"
#endif // USE_GPU_SKINNING

#define UnpackVec3(VAL) (2.f * fract(vec3(1.f, 256.f, 65536.f) * VAL) - 1.f)

NodeTransformData fetchInputData() {
    ComputeIndirectionData();
    const NodeTransformData nodeData = dvd_Transforms[TRANSFORM_IDX];

#if defined(HAS_TEXCOORD_ATTRIBUTE)
    VAR._texCoord = inTexCoordData;
#else //HAS_TEXCOORD_ATTRIBUTE
    VAR._texCoord = vec2(0, 0);
#endif //HAS_TEXCOORD_ATTRIBUTE

#if defined(HAS_POSITION_ATTRIBUTE)
    dvd_Vertex = vec4(inVertexData, 1.f);
#else //HAS_POSITION_ATTRIBUTE
    dvd_Vertex = vec4(0.f, 0.f, 0.f, 1.f);
#endif //HAS_POSITION_ATTRIBUTE

#if defined(HAS_NORMAL_ATTRIBUTE)
    dvd_Normal = UnpackVec3(inNormalData);
#else //HAS_NORMAL_ATTRIBUTE
    dvd_Normal = vec3(0.f, 0.f, 1.f);
#endif //HAS_NORMAL_ATTRIBUTE

#if defined(ENABLE_TBN)
#if defined(HAS_TANGENT_ATTRIBUTE)
    dvd_Tangent = UnpackVec3(inTangentData);
#else //HAS_TANGENT_ATTRIBUTE
    dvd_Tangent = vec3(1.f, 0.f, 0.f);
#endif //HAS_TANGENT_ATTRIBUTE
#endif //ENABLE_TBN
#if !defined(DEPTH_PASS)
#if defined(HAS_COLOR_ATTRIBUTE)
    dvd_Colour = inColourData;
#else //HAS_COLOR_ATTRIBUTE
    dvd_Colour = vec4(1.f);
#endif //HAS_COLOR_ATTRIBUTE
#endif //!DEPTH_PASS && HAS_COLOR_ATTRIBUTE

#if defined(USE_GPU_SKINNING)

#if defined(HAS_BONE_WEIGHT_ATTRIBUTE)
    dvd_BoneWeight = inBoneWeightData;
#else //HAS_BONE_WEIGHT_ATTRIBUTE
    dvd_BoneWeight = vec4(0.0);
#endif //HAS_BONE_WEIGHT_ATTRIBUTE

#if defined(HAS_BONE_INDICE_ATTRIBUTE) 
    const uint boneCount = dvd_BoneCount( nodeData ); 
    const uint dvd_CurrentAnimationFrame = dvd_AnimationFrame( nodeData );
    const uint dvd_PreviousAnimationFrame = dvd_CurrentAnimationFrame > 0 ? dvd_CurrentAnimationFrame - 1 : 0;
    dvd_CurrentAnimationOffset  = (boneCount * dvd_CurrentAnimationFrame); 
    dvd_PreviousAnimationOffset = (boneCount * dvd_PreviousAnimationFrame);
    dvd_BoneIndices = inBoneIndiceData;
#else //HAS_BONE_INDICE_ATTRIBUTE
    dvd_CurrentAnimationOffset = 0u;
    dvd_PreviousAnimationOffset = 0u; 
    dvd_BoneIndices = uvec4(0);
#endif //HAS_BONE_INDICE_ATTRIBUTE

    applyBoneTransforms( dvd_CurrentAnimationOffset );
#endif //USE_GPU_SKINNING

    return nodeData;
}

vec4 computeData(in NodeTransformData data) {
    VAR._LoDLevel  = dvd_LoDLevel(data);
    VAR._vertexW   = data._worldMatrix * dvd_Vertex;
    VAR._vertexWV  = dvd_ViewMatrix * VAR._vertexW;

#if defined(HAS_VELOCITY)
#if defined(HAS_POSITION_ATTRIBUTE)
    vec4 inputVertex = vec4( inVertexData, 1.f );
#else //HAS_POSITION_ATTRIBUTE
    vec4 inputVertex = vec4( 0.f, 0.f, 0.f, 1.f );
#endif //HAS_POSITION_ATTRIBUTE

#if defined(USE_GPU_SKINNING)
    inputVertex = applyBoneTransforms( inputVertex, dvd_PreviousAnimationOffset );
#endif //USE_GPU_SKINNING

    VAR._prevVertexWVP = data._prevWVPMatrix * inputVertex;
#endif //HAS_VELOCITY

    return dvd_ProjectionMatrix * VAR._vertexWV;
}

#endif //_VB_INPUT_DATA_VERT_
