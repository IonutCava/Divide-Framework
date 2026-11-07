--Compute

// Copyright (c) 2026 DIVIDE-Studio

// Clamps indirect draw instance counts to counts written by GPU culling (e.g. compacted instance lists).
// Patch records live in the command buffer right after the draw commands:
// count = target command index (relative to the bound range), instanceCount = element in the count buffer.

#define PATCH_RECORD_OFFSET uint(PushData0[0].x + 0.5f)
#define PATCH_RECORD_COUNT  uint(PushData0[0].y + 0.5f)

struct IndirectIndexedDrawCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_BATCH, 2, std430) coherent COMP_ONLY_RW buffer dvd_GPUCmds
{
    IndirectIndexedDrawCommand dvd_drawCommands[];
};

DESCRIPTOR_SET_RESOURCE_LAYOUT(PER_DRAW, 13, std430) coherent COMP_ONLY_R buffer dvd_InstanceCountBlock
{
    uint dvd_instanceCounts[];
};

layout(local_size_x = WORK_GROUP_SIZE) in;
void main()
{
    const uint ident = gl_GlobalInvocationID.x;
    if (ident >= PATCH_RECORD_COUNT)
    {
        return;
    }

    const IndirectIndexedDrawCommand record = dvd_drawCommands[PATCH_RECORD_OFFSET + ident];
    // Counts only shrink within a pass (culling, then occlusion), so this also preserves earlier patches and HiZ rejections.
    dvd_drawCommands[record.count].instanceCount = min(dvd_drawCommands[record.count].instanceCount, dvd_instanceCounts[record.instanceCount]);
}
