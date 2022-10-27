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

bool testSphereAABB( uint light, uint tile );
float sqDistPointAABB( vec3 point, uint tile );

struct SharedEntry
{
    vec3 positionWV;
    float radius;
    uint isSpot;
    uint enabled;
};

shared SharedEntry sharedLights[GROUP_SIZE];

// each thread handles one cluster
layout( local_size_x = CLUSTERS_X_THREADS, local_size_y = CLUSTERS_Y_THREADS, local_size_z = CLUSTERS_Z_THREADS ) in;
void main()
{

    const uint threadCount = gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z;
    const uint lightCount = POINT_LIGHT_COUNT + SPOT_LIGHT_COUNT;
    const uint numBatches = (lightCount + threadCount - 1) / threadCount;
    const uint tileIndex = gl_LocalInvocationIndex + gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z * gl_WorkGroupID.z;

    uvec2 visibleLightIndices[MAX_LIGHTS_PER_CLUSTER];
    uvec2 visibleLightCount = uvec2(0u, 0u);

    for ( uint batch = 0u; batch < numBatches; ++batch )
    {
        const uint lightIndex = batch * threadCount + gl_LocalInvocationIndex;

        //Prevent overflow
        if ( lightIndex < lightCount )
        {
            const Light source = dvd_LightSource[DIRECTIONAL_LIGHT_COUNT + lightIndex];
            // Light position is already in view space and thus it matches our cluster format
            const vec4 lightPos = getPositionAndRangeForLight( source );

            SharedEntry light;
            light.enabled = 1u;
            light.positionWV = lightPos.xyz;
            light.radius = lightPos.w;
            light.isSpot = source._TYPE == LIGHT_SPOT ? 1u : 0u;

            sharedLights[gl_LocalInvocationIndex] = light;
        }

        // wait for all threads to finish copying
        barrier();

        //Iterating within the current batch of lights
        for ( uint light = 0; light < threadCount; ++light )
        {
            if (sharedLights[light].enabled == 1u && testSphereAABB( light, tileIndex ) )
            {
                const uint arrayIdx = sharedLights[light].isSpot;
                visibleLightIndices[visibleLightCount[arrayIdx]++][arrayIdx] = batch * threadCount + light;
            }
        }
    }

    // wait for all threads to finish checking lights
    barrier();

    // get a unique index into the light index list where we can write this cluster's lights
    const uint offset = atomicAdd( globalIndexCount[0], visibleLightCount.x + visibleLightCount.y );
    const uint pointLightCount = visibleLightCount.x;
    // copy indices of lights
    for ( uint i = 0u; i < pointLightCount; ++i )
    {
        globalLightIndexList[offset + i] = visibleLightIndices[i].x;
    }

    for ( uint i = 0u; i < visibleLightCount.y; ++i )
    {
        globalLightIndexList[pointLightCount + offset + i] = visibleLightIndices[i].y;
    }

    // write light grid for this cluster
    LightGrid cluster;
    cluster._offset = offset;
    cluster._countPoint = visibleLightCount.x;
    cluster._countSpot  = visibleLightCount.y;
    lightGrid[tileIndex] = cluster;
}

bool testSphereAABB( uint light, uint tile )
{
    const SharedEntry lightEntry = sharedLights[light];
    const float squaredDistance = sqDistPointAABB( lightEntry.positionWV, tile );
    return squaredDistance <= Squared( lightEntry.radius );
}

float sqDistPointAABB( vec3 point, uint tile )
{
    float sqDist = 0.0;
    VolumeTileAABB currentCell = lightClusterAABBs[tile];
    currentCell.maxPoint[3] = tile;
    for ( int i = 0; i < 3; ++i )
    {
        float v = point[i];
        if ( v < currentCell.minPoint[i] )
        {
            sqDist += (currentCell.minPoint[i] - v) * (currentCell.minPoint[i] - v);
        }
        if ( v > currentCell.maxPoint[i] )
        {
            sqDist += (v - currentCell.maxPoint[i]) * (v - currentCell.maxPoint[i]);
        }
    }

    return sqDist;
}