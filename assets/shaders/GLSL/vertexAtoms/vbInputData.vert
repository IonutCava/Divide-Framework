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
    dvd_Normal = UnpackVec3(inNormalData);
#if defined(ENABLE_TBN)
    dvd_Tangent = UnpackVec3(inTangentData);
#endif //ENABLE_TBN
#if !defined(DEPTH_PASS)
    dvd_Colour = inColourData;
#endif //DEPTH_PASS

    const NodeTransformData nodeData = dvd_Transforms[TRANSFORM_IDX];
#if defined(USE_GPU_SKINNING)
    if (dvd_BoneCount(nodeData) > 0) {
        dvd_Vertex = applyBoneTransforms(dvd_Vertex);
    }
#endif //USE_GPU_SKINNING
    return nodeData;
}

vec4 computeData(in NodeTransformData data) {
#if defined(ENABLE_LOD)
    VAR._LoDLevel  = dvd_LoDLevel(data);
#endif //ENABLE_LOD

    VAR._vertexW   = data._worldMatrix * dvd_Vertex;
#if 0
    VAR._vertexWV  = dvd_ViewMatrix * VAR._vertexW;
#else
    // Higher precision
    VAR._vertexWV = (dvd_ViewMatrix * data._worldMatrix) * dvd_Vertex;
#endif

#if defined(HAS_VELOCITY)
    vec4 inputVertex = vec4(inVertexData, 1.f);
#if defined(USE_GPU_SKINNING)
    if (dvd_BoneCount(data) > 0u && dvd_FrameTicked(data)) {
        inputVertex = applyPrevBoneTransforms(inputVertex);
    }
#endif //USE_GPU_SKINNING
    VAR._prevVertexWVP = (dvd_PreviousViewProjectionMatrix * data._prevWorldMatrix) * inputVertex;
#endif //HAS_VELOCITY

    return dvd_ProjectionMatrix * VAR._vertexWV;
}

#endif //_VB_INPUT_DATA_VERT_
