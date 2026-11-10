--Compute

// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#if defined(CULL_TREES)
uniform float dvd_treeVisibilityDistance;
uniform uint dvd_treeInstanceCount;
// 1 when trees sway in this pass. Written into each visible draw entry's data.x.
uniform uint dvd_treeWindAnimated;

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
#include "grassInstanceMetadata.cmn"
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
#define DrawData treeDrawData
#define VisibleCounts treeVisibleCounts
#else
#define DrawData grassDrawData
#define VisibleCounts grassVisibleCounts
#endif

shared uint s_sourceCount;
shared uint s_visibleCount;

// One workgroup per tree species or grass chunk. Occlusion recompacts the previous result in place.
void main(void) {
    const uint localIndex = gl_LocalInvocationIndex;
#if defined(CULL_TREES)
    const uint species = gl_WorkGroupID.x;
    const uint rangeOffset = dvd_treeRangeOffsets[species];
    const uint countIndex = (dvd_terrainChunkOffset * VEGETATION_TREE_SPECIES_COUNT) + species;
    const uint rangeCount = dvd_treeRangeCounts[species];
#else
    const uint rangeOffset = 0u;
    const uint countIndex = dvd_terrainChunkOffset;
    const uint rangeCount = dvd_grassInstanceCount;
#endif
    const uint drawBase = (dvd_terrainChunkOffset * MAX_INSTANCES) + rangeOffset;

    if (localIndex == 0u) {
#if defined(OCCLUSION_PASS)
        s_sourceCount = min(VisibleCounts[countIndex], rangeCount);
#else
        s_sourceCount = rangeCount;
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
            instance = DrawData[drawBase + idx];
            if (instance.data.z >= 0.f) {
#if defined(CULL_TREES)
                const float treeRadius = instance.data.y * abs(instance.positionAndScale.w);
                const float radius = treeRadius + TREE_WIND_PADDING(treeRadius, length(dvd_windDetails.xz));
#else
                const float scale = abs(instance.positionAndScale.w);
                const vec4 extents = Extents * scale;
                const float windPadding = 0.075f * (3.f * abs(dvd_windDetails.x) + 1.5f * abs(dvd_windDetails.z) + 0.128f);
                const float radius = extents.w + extents.w * windPadding + 0.5f + 0.25f * (1.f + scale);
#endif
                visible = !HiZCull(instance.positionAndScale.xyz, radius);
            }
#else
            instance = Data[drawBase + idx];
            if (instance.positionAndScale.y >= dvd_globalWaterHeight) {
#if defined(CULL_TREES)
                const float treeRadius = instance.data.y * abs(instance.positionAndScale.w);
                const float radius = treeRadius + TREE_WIND_PADDING(treeRadius, length(dvd_windDetails.xz));
                const vec3 positionW = instance.positionAndScale.xyz;
                const float dist = distance(positionW.xz, dvd_cameraPosition.xz);
                if (dvd_visibilityDistance > 0.f && dist <= dvd_visibilityDistance && SphereInFrustum(positionW, radius)) {
                    instance.data.x = float(dvd_treeWindAnimated);
                    instance.data.z = dist > (dvd_visibilityDistance * 0.33f) ? 2.0f : 1.0f;
                    instance.data.w = dist;
                    visible = true;
                }
#else
                const vec4 extents = Extents * abs(instance.positionAndScale.w);
                const vec3 positionW = rotate_vertex_position(vec3(0.f, extents.y * 0.5f, 0.f), instance.orientationQuad) + instance.positionAndScale.xyz;
                const float dist = distance(positionW.xz, dvd_cameraPosition.xz);
                if (dvd_visibilityDistance > 0.f && dist <= dvd_visibilityDistance && SphereInFrustum(positionW, extents.w)) {
                    instance.data.z = max(1.f - smoothstep(dvd_visibilityDistance * 0.85f, dvd_visibilityDistance * 0.995f, dist), 0.05f);
                    // Preserve the original modulo-3/modulo-4 colour variation after reordering.
                    instance.data.y = float(GRASS_PACK_METADATA(uint(getLoD(dist)), idx));
                    instance.data.w = dist;
                    visible = true;
                }
#endif
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
            DrawData[drawBase + slot] = instance;
        }
    }

    barrier();
    const uint visibleCount = s_visibleCount;
    // Draws without a patched instance count still see the stale tail as culled.
    for (uint idx = visibleCount + localIndex; idx < sourceCount; idx += WORK_GROUP_SIZE) {
        DrawData[drawBase + idx].data.z = -1.f;
    }
    if (localIndex == 0u) {
        VisibleCounts[countIndex] = visibleCount;
    }
}
