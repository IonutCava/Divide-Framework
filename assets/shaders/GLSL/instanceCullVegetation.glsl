--Compute

#define INVS_SQRT_3 0.57735026919f

#if defined(CULL_TREES)
uniform vec4 treeExtents;
uniform float dvd_treeVisibilityDistance;

#   define MAX_INSTANCES MAX_TREE_INSTANCES
#   define Data treeData
#   define Extents treeExtents
#   define dvd_visibilityDistance dvd_treeVisibilityDistance
#else
uniform vec4 grassExtents;

#   define MAX_INSTANCES MAX_GRASS_INSTANCES
#   define Data grassData
#   define Extents grassExtents
#   define dvd_visibilityDistance dvd_grassVisibilityDistance
uniform float dvd_grassVisibilityDistance;
#endif

#define USE_CUSTOM_EXTENTS
vec3 _private_h_extents;
#define getHalfExtents(P, R) _private_h_extents

uniform vec3 cameraPosition;

#include "HiZCullingAlgorithm.cmn";
#include "vegetationData.cmn"
#include "sceneData.cmn"
#include "waterData.cmn"

vec3 rotate_vertex_position(vec3 position, vec4 q) {
    vec3 v = position.xyz;
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

#define CullItem(IDX) (Data[IDX].data.z = -1.f)

float getLoD(in float dist) {
    if (dist < dvd_visibilityDistance * 0.15f) {
        return 0.f;
    } else if (dist < dvd_visibilityDistance * 0.35f) {
        return 1.f;
    } else if (dist < dvd_visibilityDistance * 0.85f) {
        return 2.f;
    }

    return 3.f;
}

layout(local_size_x = WORK_GROUP_SIZE) in;
void main(void) {

    const uint ident = gl_GlobalInvocationID.x;
    const uint nodeIndex = (dvd_terrainChunkOffset * MAX_INSTANCES) + ident;

    if (ident >= MAX_INSTANCES) {
        CullItem(nodeIndex);
        return;
    }

    VegetationData instance = Data[nodeIndex];

    const float scale    = instance.positionAndScale.w;
    const vec4 extents   = Extents * scale;
    _private_h_extents = extents.xyz;

    vec3 vert = vec3(0.0f, extents.y * 0.5f, 0.0f);
    vert = rotate_vertex_position(vert * scale, instance.orientationQuad);
    const vec3 positionW = vert + instance.positionAndScale.xyz;

    const float dist = distance(positionW.xz, cameraPosition.xz);
    // Too far away
    if (dist > dvd_visibilityDistance) {
        CullItem(nodeIndex);
        return;
    }

    if (HiZCull(positionW, extents.w) || IsUnderWater(positionW.xyz)) {
        CullItem(nodeIndex);
    } else {
#       if defined(CULL_TREES)
            Data[nodeIndex].data.z = dist > (dvd_visibilityDistance * 0.33f) ? 2.0f : 1.0f;
#       else //CULL_TREES
            Data[nodeIndex].data.z = max(1.0f - smoothstep(dvd_visibilityDistance * 0.85f, dvd_visibilityDistance * 0.995f, dist), 0.05f);
            Data[nodeIndex].data.y = getLoD(dist);
#       endif //CULL_TREES
        Data[nodeIndex].data.w = dist;
    }
}