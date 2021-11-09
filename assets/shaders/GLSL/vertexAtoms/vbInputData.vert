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

#if !defined(DEPTH_PASS)
    dvd_Normal = UNPACK_VEC3(inNormalData);
    dvd_Colour = inColourData;
#if defined(COMPUTE_TBN) || defined(NEED_TANGENT)
    dvd_Tangent = UNPACK_VEC3(inTangentData);
#endif //COMPUTE_TBN || NEED_TANGENT
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
    VAR._vertexW   = data._worldMatrix * dvd_Vertex;
    VAR._vertexWV  = dvd_ViewMatrix * VAR._vertexW;
    vec4 vertexWVP = dvd_ProjectionMatrix * VAR._vertexWV;

#if defined(HAS_VELOCITY)
    vec4 inputVertex = vec4(inVertexData, 1.f);
#if defined(USE_GPU_SKINNING)
    if (dvd_frameTicked(data) && dvd_boneCount(data) > 0u) {
        inputVertex = applyPrevBoneTransforms(inputVertex);
    }
#endif //USE_GPU_SKINNING
    VAR._prevVertexWVP = dvd_PreviousViewProjectionMatrix * data._prevWorldMatrix * inputVertex;
#endif //HAS_VELOCITY

    return vertexWVP;
}

#endif //_VB_INPUT_DATA_VERT_
