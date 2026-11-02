--Compute.ResetCounter

#include "lightInput.cmn"

layout( local_size_x = 1, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
    if ( gl_GlobalInvocationID.x == 0 )
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

#include "lightInput.cmn"

#define GROUP_SIZE (CLUSTERS_X_THREADS * CLUSTERS_Y_THREADS * CLUSTERS_Z_THREADS)

struct SharedEntry
{
    vec3 positionWV;
    float radius;
    uint isSpot;
};

shared SharedEntry sharedLights[GROUP_SIZE];

bool SphereIntersectsAABB( in vec3 center, in float radius, in VolumeTileAABB cell )
{
    // Squared distance from the sphere centre to the closest point of the box
    const vec3 delta = center - clamp( center, cell.minPoint.xyz, cell.maxPoint.xyz );
    return dot( delta, delta ) <= radius * radius;
}

// each thread handles one cluster
layout( local_size_x = CLUSTERS_X_THREADS, local_size_y = CLUSTERS_Y_THREADS, local_size_z = CLUSTERS_Z_THREADS ) in;
void main()
{
    // Uniform across the dispatch, so the barriers inside the batch loop are in uniform control flow
    const uint lightCount = POINT_LIGHT_COUNT + SPOT_LIGHT_COUNT;
    const uint numBatches = (lightCount + GROUP_SIZE - 1u) / GROUP_SIZE;
    const uint tileIndex = gl_GlobalInvocationID.x +
                           gl_GlobalInvocationID.y * CLUSTERS_X +
                           gl_GlobalInvocationID.z * (CLUSTERS_X * CLUSTERS_Y);

    const VolumeTileAABB cell = lightClusterAABBs[tileIndex];

    // One budget shared by both light types (the global index buffer holds MAX_LIGHTS_PER_CLUSTER entries per cluster).
    // Point lights fill from the front, spot lights from the back. Lights past the budget are dropped in buffer order.
    uint visibleLightIndices[MAX_LIGHTS_PER_CLUSTER];
    uint pointCount = 0u;
    uint spotCount = 0u;

    for ( uint batch = 0u; batch < numBatches; ++batch )
    {
        const uint batchStart = batch * GROUP_SIZE;
        const uint lightIndex = batchStart + gl_LocalInvocationIndex;

        if ( lightIndex < lightCount )
        {
            const Light source = dvd_LightSource[DIRECTIONAL_LIGHT_COUNT + lightIndex];
            // Light position is already in view space and thus it matches our cluster format
            const vec4 bounds = getPositionAndRangeForLight( source );

            SharedEntry light;
            light.positionWV = bounds.xyz;
            light.radius = bounds.w;
            light.isSpot = source._TYPE == LIGHT_SPOT ? 1u : 0u;
            sharedLights[gl_LocalInvocationIndex] = light;
        }

        // wait for the whole batch to be copied
        barrier();

        // only the entries written for this batch are valid
        const uint batchSize = min( GROUP_SIZE, lightCount - batchStart );
        for ( uint i = 0u; i < batchSize; ++i )
        {
            const SharedEntry light = sharedLights[i];
            if ( pointCount + spotCount < MAX_LIGHTS_PER_CLUSTER && SphereIntersectsAABB( light.positionWV, light.radius, cell ) )
            {
                if ( light.isSpot == 1u )
                {
                    visibleLightIndices[MAX_LIGHTS_PER_CLUSTER - 1u - spotCount] = batchStart + i;
                    ++spotCount;
                }
                else
                {
                    visibleLightIndices[pointCount] = batchStart + i;
                    ++pointCount;
                }
            }
        }

        // wait for every thread to finish reading before the next batch overwrites the shared entries
        barrier();
    }

    // get a unique index into the light index list where we can write this cluster's lights
    const uint offset = atomicAdd( globalIndexCount[0], pointCount + spotCount );

    // point lights first, then spot lights (in discovery order)
    for ( uint i = 0u; i < pointCount; ++i )
    {
        globalLightIndexList[offset + i] = visibleLightIndices[i];
    }
    for ( uint i = 0u; i < spotCount; ++i )
    {
        globalLightIndexList[offset + pointCount + i] = visibleLightIndices[MAX_LIGHTS_PER_CLUSTER - 1u - i];
    }

    // write light grid for this cluster
    LightGrid cluster;
    cluster._offset = offset;
    cluster._countPoint = pointCount;
    cluster._countSpot = spotCount;
    cluster._unused = 0u;
    lightGrid[tileIndex] = cluster;
}