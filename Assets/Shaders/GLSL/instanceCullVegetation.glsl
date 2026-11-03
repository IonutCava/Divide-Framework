--Compute

#define INVS_SQRT_3 0.57735026919f

#if defined(CULL_TREES)
uniform vec4 dvd_treeExtents;
uniform float dvd_treeVisibilityDistance;
uniform uint dvd_treeInstanceCount;

#   define MAX_INSTANCES MAX_TREE_INSTANCES
#   define Data treeData
#   define Extents dvd_treeExtents
#   define dvd_visibilityDistance dvd_treeVisibilityDistance
#   define dvd_instanceCount dvd_treeInstanceCount
#else
uniform vec4 dvd_grassExtents;
uniform float dvd_grassVisibilityDistance;
uniform uint dvd_grassInstanceCount;

#   define MAX_INSTANCES MAX_GRASS_INSTANCES
#   define Data grassData
#   define Extents dvd_grassExtents
#   define dvd_visibilityDistance dvd_grassVisibilityDistance
#   define dvd_instanceCount dvd_grassInstanceCount
#endif

#define USE_CUSTOM_EXTENTS
vec3 _private_h_extents;
#define getHalfExtents(P, R) _private_h_extents

uniform vec3 dvd_cameraPosition;

#include "HiZCullingAlgorithm.cmn"
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
    // Each chunk owns MAX_INSTANCES slots but only draws its first dvd_instanceCount; padding lanes must not touch the next chunk.
    if (ident >= min(dvd_instanceCount, uint(MAX_INSTANCES))) {
        return;
    }

    const uint nodeIndex = (dvd_terrainChunkOffset * MAX_INSTANCES) + ident;
    VegetationData instance = Data[nodeIndex];

    const float scale    = instance.positionAndScale.w;
    const vec4 extents   = Extents * scale;
    _private_h_extents = extents.xyz;

    // Bounding volume centre: half way up the (already scaled) extents, rotated into world space.
    const vec3 vert = rotate_vertex_position(vec3(0.0f, extents.y * 0.5f, 0.0f), instance.orientationQuad);
    const vec3 positionW = vert + instance.positionAndScale.xyz;

    const float dist = distance(positionW.xz, dvd_cameraPosition.xz);
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
