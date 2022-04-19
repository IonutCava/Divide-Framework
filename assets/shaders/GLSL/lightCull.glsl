--Compute.ResetCounter

DESCRIPTOR_SET_RESOURCE_LAYOUT(0, BUFFER_LIGHT_INDEX_COUNT, std430) COMP_ONLY_W buffer globalIndexCountSSBO {
    uint globalIndexCount[];
};

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x == 0)
    {
        // reset the atomic counter for the light grid generation
        // writable compute buffers can't be updated by CPU so do it here
        globalIndexCount[0] = 0;
    }
}

--Compute

//Most of the stuff is from here:https://github.com/Angelo1211/HybridRenderingEngine/blob/master/assets/shaders/ComputeShaders/clusterCullLightShader.comp
//With a lot of : https://github.com/pezcode/Cluster :

// compute shader to cull lights against cluster bounds
// builds a light grid that holds indices of lights for each cluster
// largely inspired by http://www.aortiz.me/2018/12/21/CG.html

#define USE_LIGHT_CLUSTERS
#include "lightInput.cmn"

DESCRIPTOR_SET_RESOURCE_LAYOUT(0, BUFFER_LIGHT_INDEX_COUNT, std430) COMP_ONLY_RW buffer globalIndexCountSSBO {
    uint globalIndexCount[];
};

#define GROUP_SIZE (CLUSTERS_X_THREADS * CLUSTERS_Y_THREADS * CLUSTERS_Z_THREADS)

// check if light radius extends into the cluster
bool lightIntersectsCluster(in vec3 clusterMin, in vec3 clusterMax, in vec4 light) {
    // NOTE: expects light.position to be in view space like the cluster bounds
    // global light list has world space coordinates, but we transform the
    // coordinates in the shared array of lights after copying

    // get closest point to sphere center
    const vec3 dist = max(clusterMin, min(light.xyz, clusterMax)) - light.xyz;
    // check if point is inside the sphere
    return dot(dist, dist) <= Squared(light.w);
}

// light cache for the current workgroup
// group shared memory has lower latency than global memory

// there's no guarantee on the available shared memory
// as a guideline the minimum value of GL_MAX_COMPUTE_SHARED_MEMORY_SIZE is 32KB
// with a workgroup size of 16*8*4 this is 64 bytes per light
// however, using all available memory would limit the compute shader invocation to only 1 workgroup
shared vec4 sharedLights[GROUP_SIZE];

// each thread handles one cluster
layout(local_size_x = CLUSTERS_X_THREADS, local_size_y = CLUSTERS_Y_THREADS, local_size_z = CLUSTERS_Z_THREADS) in;
void main() {
    // the way we calculate the index doesn't really matter here since we write to the same index in the light grid as we read from the cluster buffer
    const uint clusterIndex = gl_GlobalInvocationID.z * gl_WorkGroupSize.x * gl_WorkGroupSize.y +
                              gl_GlobalInvocationID.y * gl_WorkGroupSize.x +
                              gl_GlobalInvocationID.x;

    VolumeTileAABB currentCluster = lightClusterAABBs[clusterIndex];
    const vec3 clusterMin = currentCluster.minPoint.xyz;
    const vec3 clusterMax = currentCluster.maxPoint.xyz;

    // local thread variables
    // hold the result of light culling for this cluster
    uint visibleLights[LIGHT_TYPE_COUNT][MAX_LIGHTS_PER_CLUSTER];
    uint visibleCount[LIGHT_TYPE_COUNT];
    for (int i = 0; i < LIGHT_TYPE_COUNT; ++i) {
        visibleCount[i] = 0u;
    }
    uint visibleCountTotal = 0u;

    uint lightCount = POINT_LIGHT_COUNT + SPOT_LIGHT_COUNT;
    const uint localID = gl_LocalInvocationIndex;

    // we have a cache of GROUP_SIZE lights
    // have to run this loop several times if we have more than GROUP_SIZE lights
    uint lightOffset = 0u;
    while (lightOffset < lightCount)
    {
        // read GROUP_SIZE lights into shared memory
        // each thread copies one light
        const uint batchSize = min(GROUP_SIZE, lightCount - lightOffset); 

        if (localID < batchSize) {
            const Light source = dvd_LightSource[DIRECTIONAL_LIGHT_COUNT + localID + lightOffset];
            // Light position is already in view space and thus it matches our cluster format
            sharedLights[localID] = getPositionAndRangeForLight(source);
            if (source._TYPE == LIGHT_SPOT) {
                //negative range is for spot lights only
                sharedLights[localID].w *= -1.f;
            }
        }

        // wait for all threads to finish copying
        barrier();

        // each thread is one cluster and checks against all lights in the cache
        for (int i = 0; i < batchSize; ++i) {
            if (visibleCountTotal < MAX_LIGHTS_PER_CLUSTER &&
                lightIntersectsCluster(clusterMin, clusterMax, sharedLights[i]))
            {
                const uint idx = (sharedLights[i].w < 0.f ? LIGHT_SPOT_IDX : LIGHT_POINT_IDX);
                const uint count = visibleCount[idx]++;
                visibleLights[idx][count] = lightOffset + i;

                ++visibleCountTotal;
            }
        }

        lightOffset += batchSize;
    }

    // wait for all threads to finish checking lights
    barrier();
    // get a unique index into the light index list where we can write this cluster's lights
    const uint offset = atomicAdd(globalIndexCount[0], visibleCountTotal);

    // copy indices of lights
    for (int i = 0; i < visibleCount[LIGHT_POINT_IDX]; ++i) {
        globalLightIndexList[offset + i] = visibleLights[LIGHT_POINT_IDX][i];
    }
    for (int i = 0; i < visibleCount[LIGHT_SPOT_IDX]; ++i) {
        globalLightIndexList[offset + visibleCount[LIGHT_POINT_IDX] + i] = visibleLights[LIGHT_SPOT_IDX][i];
    }

    // write light grid for this cluster
    lightGrid[clusterIndex]._offset = offset;
    lightGrid[clusterIndex]._countTotal = visibleCountTotal;
    lightGrid[clusterIndex]._countPoint = visibleCount[LIGHT_POINT_IDX];
    lightGrid[clusterIndex]._countSpot  = visibleCount[LIGHT_SPOT_IDX];
}