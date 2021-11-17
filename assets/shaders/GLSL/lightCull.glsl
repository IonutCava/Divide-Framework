--Compute

//Most of the stuff is from here:https://github.com/Angelo1211/HybridRenderingEngine/blob/master/assets/shaders/ComputeShaders/clusterCullLightShader.comp
//With a lot of : https://github.com/pezcode/Cluster
#define USE_LIGHT_CLUSTERS
#include "lightInput.cmn"

layout(binding = BUFFER_LIGHT_INDEX_COUNT, std430) COMP_ONLY_RW buffer globalIndexCountSSBO {
    uint globalIndexCount[];
};

#define GROUP_SIZE (CLUSTERS_X_THREADS * CLUSTERS_Y_THREADS * CLUSTERS_Z_THREADS)

struct TempLight
{
    vec3 position;
    float radius;
    uint type;
};

// check if light radius extends into the cluster
bool lightIntersectsCluster(TempLight light, VolumeTileAABB cluster) {

    // NOTE: expects light.position to be in view space like the cluster bounds
    // global light list has world space coordinates, but we transform the
    // coordinates in the shared array of lights after copying

    // only add distance in either dimension if it's outside the bounding box
    const vec3 belowDist = cluster.minPoint.xyz - light.position;
    const vec3 aboveDist = light.position - cluster.maxPoint.xyz;

    const vec3 isBelow = vec3(greaterThan(belowDist, vec3(0.0)));
    const vec3 isAbove = vec3(greaterThan(aboveDist, vec3(0.0)));

    const vec3 distSqVec = (isBelow * belowDist) + (isAbove * aboveDist);
    const float distsq = dot(distSqVec, distSqVec);

    return distsq <= SQUARED(light.radius);
}

void clearLightListForCluster(in uint clusterIndex)
{
    lightGrid[clusterIndex].offset = 0u;
    lightGrid[clusterIndex].countPoint = 0u;
    lightGrid[clusterIndex].countSpot = 0u;
}

shared TempLight sharedLights[GROUP_SIZE];
void computeLightList(in uint clusterIndex, in uint lightCount)
{
    uint visibleLights[2][MAX_LIGHTS_PER_CLUSTER];
    uint visibleCount[2];

    visibleCount[0] = visibleCount[1] = 0u;

    VolumeTileAABB cluster = cluster[clusterIndex];

    uint lightOffset = 0u;
    while (lightOffset < lightCount)
    {
        // read GROUP_SIZE lights into shared memory
        // each thread copies one light
        const uint batchSize = min(GROUP_SIZE, lightCount - lightOffset); 

        if (uint(gl_LocalInvocationIndex) < batchSize) {
            const uint lightIndex = lightOffset + gl_LocalInvocationIndex;
            const Light source = dvd_LightSource[lightIndex + DIRECTIONAL_LIGHT_COUNT];
            if (source._TYPE == LIGHT_SPOT) {
                const vec3 position = source._positionWV.xyz;
                const float range = source._SPOT_CONE_SLANT_HEIGHT * 0.5f;//range to radius conversion
                sharedLights[gl_LocalInvocationIndex].position = position + source._directionWV.xyz * range;
                sharedLights[gl_LocalInvocationIndex].radius   = range;
                sharedLights[gl_LocalInvocationIndex].type = 1u;
            } else {
                sharedLights[gl_LocalInvocationIndex].position = source._positionWV.xyz;
                sharedLights[gl_LocalInvocationIndex].radius   = source._positionWV.w;
                sharedLights[gl_LocalInvocationIndex].type = 0u;
            }
        }

        // wait for all threads to finish copying
        barrier();

        // each thread is one cluster and checks against all lights in the cache
        uint lightsPerCluster = 0u;
        for (uint i = 0u; i < batchSize; ++i) {
            if (lightsPerCluster >= MAX_LIGHTS_PER_CLUSTER) {
                break;
            }
            if (lightIntersectsCluster(sharedLights[i], cluster)) {
                ++lightsPerCluster;

                const uint type = sharedLights[i].type;
                visibleLights[type][visibleCount[type]++] = lightOffset + i;
            }
        }

        lightOffset += batchSize;
    }

    // wait for all threads to finish checking lights
    barrier();

    // get a unique index into the light index list where we can write this cluster's lights
    const uint offset = atomicAdd(globalIndexCount[0], visibleCount[0] + visibleCount[1]);

    // copy indices of lights
    for (uint i = 0u; i < visibleCount[0]; ++i) {
        globalLightIndexList[offset + i] = visibleLights[0][i];
    }
    for (uint i = 0u; i < visibleCount[1]; ++i) {
        globalLightIndexList[visibleCount[0] + offset + i] = visibleLights[1][i];
    }
    lightGrid[clusterIndex].offset = offset;
    lightGrid[clusterIndex].countPoint = visibleCount[0];
    lightGrid[clusterIndex].countSpot = visibleCount[1];
}

layout(local_size_x = CLUSTERS_X_THREADS, local_size_y = CLUSTERS_Y_THREADS, local_size_z = CLUSTERS_Z_THREADS) in;
void main() {
    // the way we calculate the index doesn't really matter here since we write to the same index in the light grid as we read from the cluster buffer
    const uint clusterIndex = gl_GlobalInvocationID.z * gl_WorkGroupSize.x * gl_WorkGroupSize.y +
                              gl_GlobalInvocationID.y * gl_WorkGroupSize.x +
                              gl_GlobalInvocationID.x;

    // reset the atomic counter
    // writable compute buffers can't be updated by CPU so do it here
    if (clusterIndex == 0u) {
        globalIndexCount[0] = 0u;
    }
    barrier();

    const uint lightCount = POINT_LIGHT_COUNT + SPOT_LIGHT_COUNT;
    if (lightCount > 0u) {
        computeLightList(clusterIndex, lightCount);
    } else {
        clearLightListForCluster(clusterIndex);
    }
}