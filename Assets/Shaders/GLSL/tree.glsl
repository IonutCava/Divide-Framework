--Vertex

// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "vbInputData.vert"
#define VEGETATION_TREE_DRAW
#include "vegetationData.cmn"
#include "lightingDefaults.vert"
#include "treeWind.cmn"

VegetationData TreeData(in uint instanceID) {
    return treeData[dvd_instanceDataIndex + instanceID];
}

vec3 rotate_vertex_position(vec3 position, vec4 q) {
    const vec3 v = position.xyz;
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main(void){

    const NodeTransformData nodeData = fetchInputData();
    VAR._SelectionFlag = dvd_SelectionFlag(nodeData);

    const VegetationData data = TreeData(VEGETATION_INSTANCE_INDEX);

    float scale = data.positionAndScale.w;
    const bool isCulled = data.data.z < 0.f;
    VAR._LoDLevel = isCulled ? 3u : uint(data.data.z);

#if defined(HAS_CULLING_OUT)
    gl_CullDistance[0] = isCulled ? -0.01f : 0.01f;
#else
    if (isCulled) {
        scale = 0.f;
    }
#endif


    dvd_Vertex.xyz = rotate_vertex_position(dvd_Vertex.xyz * scale, data.orientationQuad);
#if !defined(VEGETATION_WIND_DISABLED)
    if (TREE_WIND_ENABLED(data.data.x) && TREE_WIND_ANIMATED(VAR._LoDLevel, dvd_Vertex.y)) {
        const float time = dvd_windDetails.w * MSToSeconds(dvd_GameTimeMS) * 0.25f;
        const float amplitude = TREE_WIND_AMPLITUDE(dvd_Vertex.x, dvd_Vertex.y);
        dvd_Vertex.xz += amplitude * vec2(cos(time) * dvd_windDetails.x, sin(time) * dvd_windDetails.z);
    }
#endif //!VEGETATION_WIND_DISABLED

    VAR._vertexW = (dvd_Vertex + vec4(data.positionAndScale.xyz, 0.0f));

    VAR._vertexWV = dvd_ViewMatrix * VAR._vertexW;

#if !defined(SHADOW_PASS)
    computeLightVectors(data.orientationQuad, vec3(scale));
    setClipPlanes();
#endif

    //Compute the final vert position
    const vec4 clipPosition = dvd_ProjectionMatrix * VAR._vertexWV;
    gl_Position = clipPosition;
#if defined(HAS_VELOCITY)
    // STUBBED: TODO: Implement vegetation velocity support with previous instance and deformation state.
    VAR._prevVertexWVP = dvd_PrevViewProjectionMatrix * VAR._vertexW;
#endif //HAS_VELOCITY
}
