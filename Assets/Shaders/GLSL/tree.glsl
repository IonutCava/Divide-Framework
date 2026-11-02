--Vertex

// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "vbInputData.vert"
#include "vegetationData.cmn"
#include "lightingDefaults.vert"

VegetationData TreeData(in uint instanceID) {
    return treeData[dvd_terrainChunkOffset * MAX_TREE_INSTANCES + instanceID];
}

vec3 rotate_vertex_position(vec3 position, vec4 q) {
    const vec3 v = position.xyz;
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void computeFoliageMovementTree(inout vec4 vertex, in float heightExtent) {
    float time = dvd_windDetails.w * MSToSeconds(dvd_GameTimeMS) * 0.25f;
    float cosX = cos(vertex.x);
    float sinX = sin(vertex.x);
    float halfScale = 0.5f * heightExtent;
    vertex.x += (halfScale * cos(time) * cosX * sinX) * dvd_windDetails.x;
    vertex.z += (halfScale * sin(time) * cosX * sinX) * dvd_windDetails.z;
}

void main(void){

    const NodeTransformData nodeData = fetchInputData();

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
    if (VAR._LoDLevel < 2u && dvd_Vertex.y * scale > 0.85f) {
        //computeFoliageMovementTree(dvd_Vertex, dvd_Vertex.y * 0.5f);
    }

    VAR._vertexW = (dvd_Vertex + vec4(data.positionAndScale.xyz, 0.0f));

    VAR._vertexWV = dvd_ViewMatrix * VAR._vertexW;

#if !defined(SHADOW_PASS)
    computeLightVectors(nodeData);
    setClipPlanes();
#endif

    //Compute the final vert position
    gl_Position = dvd_ProjectionMatrix * VAR._vertexWV;
}