--Vertex

#define NEED_SCENE_DATA
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
    float time = dvd_windDetails.w * MSToSeconds(dvd_TimeMS) * 0.25f;
    float cosX = cos(vertex.x);
    float sinX = sin(vertex.x);
    float halfScale = 0.5f * heightExtent;
    vertex.x += (halfScale * cos(time) * cosX * sinX) * dvd_windDetails.x;
    vertex.z += (halfScale * sin(time) * cosX * sinX) * dvd_windDetails.z;
}

void main(void){

    const NodeTransformData nodeData = fetchInputData();

    const VegetationData data = TreeData(gl_InstanceID);

    float scale = data.positionAndScale.w;
    VAR._LoDLevel = data.data.z;

#if defined(HAS_CULLING_OUT)
    gl_CullDistance[0] = -0.01f * when_gt(VAR._LoDLevel, 2.1f);
#else
    scale -= scale * when_gt(VAR._LoDLevel, 2.1f);
#endif


    dvd_Vertex.xyz = rotate_vertex_position(dvd_Vertex.xyz * scale, data.orientationQuad);
    if (VAR._LoDLevel < 1.1f && dvd_Vertex.y * scale > 0.85f) {
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