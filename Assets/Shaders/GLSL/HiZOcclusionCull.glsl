--Compute

#include "HiZCullingAlgorithm.cmn"
#include "nodeDataDefinition.cmn"

#define INVS_SQRT_3 0.57735026919f

uniform uint dvd_numEntities;
uniform uint dvd_countCulledItems;

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_PASS, 7, std430) coherent ACCESS_W buffer culledCountBuffer
{
    uint culledCount[];
};

//ref: http://malideveloper.arm.com/resources/sample-code/occlusion-culling-hierarchical-z/

struct IndirectIndexedDrawCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_BATCH, 4, std430) coherent COMP_ONLY_R buffer dvd_IndirectionBlock
{
    NodeIndirectionData dvd_IndirectionData[];
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_BATCH, 3, std430) coherent COMP_ONLY_R buffer dvd_TransformBlock
{
    NodeTransformData dvd_Transforms[];
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_BATCH, 2, std430) coherent COMP_ONLY_RW buffer dvd_GPUCmds
{
    IndirectIndexedDrawCommand dvd_drawCommands[];
};

void CullItem(in uint idx)
{
    if (dvd_countCulledItems == 1u)
    {
        atomicAdd(culledCount[0], 1);
    }
    dvd_drawCommands[idx].instanceCount = 0u;
}

layout(local_size_x = WORK_GROUP_SIZE) in;
void main()
{
    if (dvd_numEntities < WORK_GROUP_SIZE)
    {
        return;
    }

    const uint ident = gl_GlobalInvocationID.x;

    if (ident >= dvd_numEntities)
    {
        atomicAdd(culledCount[1], 1);
        CullItem(ident);
        return;
    }

    const uint DVD_BASE_INSTANCE = dvd_drawCommands[ident].baseInstance;

    // We don't currently handle instanced nodes with this. We might need to in the future
    // Usually this is just terrain, vegetation and the skybox. So not that bad all in all since those have
    // their own culling routines
    if (DVD_BASE_INSTANCE == 0u)
    {
        atomicAdd(culledCount[2], 1);
        return;
    }

    const NodeTransformData transformData = dvd_Transforms[dvd_IndirectionData[DVD_BASE_INSTANCE - 1u]._transformIdx];
    // Skip occlusion cull if the flag is set
    if (!dvd_CullNode(transformData))
    {
        atomicAdd(culledCount[3], 1);
        return;
    }

    const vec4 boundingSphere = dvd_BoundingSphere(transformData);
    if (HiZCull(boundingSphere.xyz, boundingSphere.w))
    {
        CullItem(ident);
    }
}

