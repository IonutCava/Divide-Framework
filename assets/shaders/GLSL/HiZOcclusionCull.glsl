--Compute
#include "HiZCullingAlgorithm.cmn";
#include "nodeDataDefinition.cmn"

#define INVS_SQRT_3 0.57735026919f

uniform uint numEntities;
uniform uint countCulledItems;

layout(binding = BUFFER_ATOMIC_COUNTER_0, offset = 0) uniform atomic_uint culledCount;

//ref: http://malideveloper.arm.com/resources/sample-code/occlusion-culling-hierarchical-z/

struct IndirectDrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

layout(binding = BUFFER_NODE_INDIRECTION_DATA, std430) coherent COMP_ONLY_R buffer dvd_IndirectionBlock
{
    NodeIndirectionData dvd_IndirectionData[];
};

layout(binding = BUFFER_NODE_TRANSFORM_DATA, std430) coherent COMP_ONLY_R buffer dvd_TransformBlock
{
    NodeTransformData dvd_Transforms[];
};

layout(binding = BUFFER_GPU_COMMANDS, std430) coherent COMP_ONLY_RW buffer dvd_GPUCmds
{
    IndirectDrawCommand dvd_drawCommands[];
};

void CullItem(in uint idx) {
    if (countCulledItems == 1u) {
        atomicCounterIncrement(culledCount);
    }
    dvd_drawCommands[idx].instanceCount = 0u;
}

layout(local_size_x = WORK_GROUP_SIZE) in;
void main()
{
    const uint ident = gl_GlobalInvocationID.x;

    if (ident >= numEntities) {
        CullItem(ident);
        return;
    }

    const uint BASE_INSTANCE = dvd_drawCommands[ident].baseInstance;
    // We dont currently handle instanced nodes with this. We might need to in the future
    // Usually this is just terrain, vegetation and the skybox. So not that bad all in all since those have
    // their own culling routines
    if (BASE_INSTANCE == 0u) {
        return;
    }

    uint transformIdx = dvd_IndirectionData[BASE_INSTANCE - 1u]._transformIdx;
    const NodeTransformData transformData = dvd_Transforms[transformIdx];
    // Skip occlusion cull if the flag is set
    if (!dvd_CullNode(transformData)) {
        return;
    }

    const vec4 boundingSphere = dvd_BoundingSphere(transformData);
    if (HiZCull(boundingSphere.xyz, boundingSphere.w)) {
        CullItem(ident);
    }
}

