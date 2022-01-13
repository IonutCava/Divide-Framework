#ifndef _VB_INPUT_DATA_VERT_
#define _VB_INPUT_DATA_VERT_

#include "velocityCheck.cmn"
#include "vertexDefault.vert"
#include "nodeBufferedInput.cmn"
#if defined(USE_GPU_SKINNING)
#include "boneTransforms.vert"
#endif // USE_GPU_SKINNING

NodeTransformData fetchInputData() {
    ComputeIndirectionData();

    VAR._texCoord = inTexCoordData;

    dvd_Vertex = vec4(inVertexData, 1.f);
    dvd_Normal = UNPACK_VEC3(inNormalData);
#if defined(COMPUTE_TBN) || defined(NEED_TANGENT)
    dvd_Tangent = UNPACK_VEC3(inTangentData);
#endif //COMPUTE_TBN || NEED_TANGENT
#if !defined(DEPTH_PASS)
    dvd_Colour = inColourData;
#endif //DEPTH_PASS

    const NodeTransformData nodeData = dvd_Transforms[TRANSFORM_IDX];
#if defined(USE_GPU_SKINNING)
    if (dvd_boneCount(nodeData) > 0) {
        dvd_Vertex = applyBoneTransforms(dvd_Vertex);
    }
#endif //USE_GPU_SKINNING
    return nodeData;
}

vec4 computeData(in NodeTransformData data) {
    VAR._LoDLevel  = dvd_LoDLevel(data);
    VAR._vertexW   = data._worldMatrix * dvd_Vertex;
    VAR._vertexWV  = dvd_ViewMatrix * VAR._vertexW;

#if defined(HAS_VELOCITY)
    vec4 inputVertex = vec4(inVertexData, 1.f);
#if defined(USE_GPU_SKINNING)
    if (dvd_frameTicked(data) && dvd_boneCount(data) > 0u) {
        inputVertex = applyPrevBoneTransforms(inputVertex);
    }
#endif //USE_GPU_SKINNING
    VAR._prevVertexWVP = dvd_PreviousProjectionMatrix * dvd_PreviousViewMatrix * data._prevWorldMatrix * inputVertex;
#endif //HAS_VELOCITY

    return dvd_ProjectionMatrix * VAR._vertexWV;
}

#endif //_VB_INPUT_DATA_VERT_
