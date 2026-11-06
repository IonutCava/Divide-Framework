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

#if defined(CULL_TREES)
uniform uvec4 dvd_treeRangeOffsets;
uniform uvec4 dvd_treeRangeCounts;

shared uint s_sourceCount;
shared uint s_visibleCount;

// One workgroup per tree species. Visible instances of the species range are compacted to the front of the
// range in treeDrawData and the visible count is written to treeVisibleCounts, so indirect draws only pay
// vertex cost for visible instances. The occlusion variant recompacts the previous result in place.
void main(void) {
    const uint species = gl_WorkGroupID.x;
    const uint localIndex = gl_LocalInvocationIndex;
    const uint rangeOffset = dvd_treeRangeOffsets[species];
    const uint drawBase = (dvd_terrainChunkOffset * MAX_INSTANCES) + rangeOffset;
    const uint countIndex = (dvd_terrainChunkOffset * VEGETATION_TREE_SPECIES_COUNT) + species;

    if (localIndex == 0u) {
#if defined(OCCLUSION_PASS)
        s_sourceCount = min(treeVisibleCounts[countIndex], dvd_treeRangeCounts[species]);
#else
        s_sourceCount = dvd_treeRangeCounts[species];
#endif
        s_visibleCount = 0u;
    }
    barrier();
    const uint sourceCount = min(s_sourceCount, uint(MAX_INSTANCES) - min(rangeOffset, uint(MAX_INSTANCES)));

    for (uint roundBase = 0u; roundBase < sourceCount; roundBase += WORK_GROUP_SIZE) {
        const uint idx = roundBase + localIndex;
        bool visible = false;
        VegetationData instance;
        if (idx < sourceCount) {
#if defined(OCCLUSION_PASS)
            instance = treeDrawData[drawBase + idx];
            if (instance.data.z >= 0.f) {
                const float treeRadius = instance.data.y * abs(instance.positionAndScale.w);
                const float radius = treeRadius + TREE_WIND_PADDING(treeRadius, length(dvd_windDetails.xz));
                visible = !HiZCull(instance.positionAndScale.xyz, radius);
            }
#else
            instance = treeData[drawBase + idx];
            if (instance.positionAndScale.y >= dvd_globalWaterHeight) {
                const float treeRadius = instance.data.y * abs(instance.positionAndScale.w);
                const float radius = treeRadius + TREE_WIND_PADDING(treeRadius, length(dvd_windDetails.xz));
                const vec3 positionW = instance.positionAndScale.xyz;
                const float dist = distance(positionW.xz, dvd_cameraPosition.xz);
                if (dvd_visibilityDistance > 0.f && dist <= dvd_visibilityDistance && SphereInFrustum(positionW, radius)) {
                    instance.data.z = dist > (dvd_visibilityDistance * 0.33f) ? 2.0f : 1.0f;
                    instance.data.w = dist;
                    visible = true;
                }
            }
#endif
        }

        uint slot = 0u;
        if (visible) {
            slot = atomicAdd(s_visibleCount, 1u);
        }
        // Every read of this round completes before any write. Slots never pass the end of this round,
        // so later rounds only read entries that have not been overwritten.
        barrier();
        if (visible) {
            treeDrawData[drawBase + slot] = instance;
        }
    }

    barrier();
    const uint visibleCount = s_visibleCount;
    // Draws without a patched instance count still see the stale tail as culled.
    for (uint idx = visibleCount + localIndex; idx < sourceCount; idx += WORK_GROUP_SIZE) {
        treeDrawData[drawBase + idx].data.z = -1.f;
    }
    if (localIndex == 0u) {
        treeVisibleCounts[countIndex] = visibleCount;
    }
}

#else //CULL_TREES
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
    const vec4 extents = Extents * scale;

#if defined(OCCLUSION_PASS)
    float radius = extents.w;
    // Bound wind, camera displacement and the fade-dependent vertical offset.
    const float windPadding = 0.075f * (3.f * abs(dvd_windDetails.x) + 1.5f * abs(dvd_windDetails.z) + 0.128f);
    radius += extents.w * windPadding + 0.5f + 0.25f * (1.f + scale);
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

    Data[nodeIndex].data.z = max(1.0f - smoothstep(dvd_visibilityDistance * 0.85f, dvd_visibilityDistance * 0.995f, dist), 0.05f);
    Data[nodeIndex].data.y = getLoD(dist);
    Data[nodeIndex].data.w = dist;
#endif
}
#endif //CULL_TREES
