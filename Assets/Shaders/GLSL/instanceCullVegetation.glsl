--Compute

// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#if defined(CULL_TREES)
uniform float dvd_treeVisibilityDistance;
uniform uint dvd_treeInstanceCount;

#   define MAX_INSTANCES MAX_TREE_INSTANCES
#   define Data treeData
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

uniform vec3 dvd_cameraPosition;
uniform float dvd_globalWaterHeight;

#include "frustumCulling.cmn"
#include "vegetationData.cmn"
#include "sceneData.cmn"
#include "treeWind.cmn"
#if defined(OCCLUSION_PASS)
uniform mat4 dvd_occlusionViewProjection;
#define viewProjectionMatrix dvd_occlusionViewProjection
#include "HiZCullingAlgorithm.cmn"
#endif

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
#if defined(OCCLUSION_PASS)
    if (instance.data.z < 0.f)
    {
        return;
    }
#endif
    if (instance.positionAndScale.y < dvd_globalWaterHeight) {
        CullItem(nodeIndex);
        return;
    }

    const float scale    = abs(instance.positionAndScale.w);
#if defined(CULL_TREES)
    const float treeRadius = instance.data.y * scale;
    const vec4 extents = vec4(0.f, 0.f, 0.f, treeRadius + TREE_WIND_PADDING(treeRadius, length(dvd_windDetails.xz)));
#else
    const vec4 extents = Extents * scale;
#endif

#if defined(OCCLUSION_PASS)
    float radius = extents.w;
#if !defined(CULL_TREES)
    // Bound wind, camera displacement and the fade-dependent vertical offset.
    const float windPadding = 0.075f * (3.f * abs(dvd_windDetails.x) + 1.5f * abs(dvd_windDetails.z) + 0.128f);
    radius += extents.w * windPadding + 0.5f + 0.25f * (1.f + scale);
#endif
    if (HiZCull(instance.positionAndScale.xyz, radius))
    {
        CullItem(nodeIndex);
    }
    return;
#else
    // Bounding volume centre: half way up the (already scaled) extents, rotated into world space.
    const vec3 vert = rotate_vertex_position(vec3(0.0f, extents.y * 0.5f, 0.0f), instance.orientationQuad);
    const vec3 positionW = vert + instance.positionAndScale.xyz;

    const float dist = distance(positionW.xz, dvd_cameraPosition.xz);
    // Too far away
    if (dvd_visibilityDistance <= 0.f || dist > dvd_visibilityDistance || !SphereInFrustum(positionW, extents.w)) {
        CullItem(nodeIndex);
        return;
    }

#   if defined(CULL_TREES)
        Data[nodeIndex].data.z = dist > (dvd_visibilityDistance * 0.33f) ? 2.0f : 1.0f;
#   else //CULL_TREES
        Data[nodeIndex].data.z = max(1.0f - smoothstep(dvd_visibilityDistance * 0.85f, dvd_visibilityDistance * 0.995f, dist), 0.05f);
        Data[nodeIndex].data.y = getLoD(dist);
#   endif //CULL_TREES
    Data[nodeIndex].data.w = dist;
#endif
}
