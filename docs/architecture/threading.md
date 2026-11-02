# Threading and synchronization

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and task pools

- `Source/Core/PlatformContext.cpp` and `Source/Core/Headers/PlatformContext.h`: task-pool ownership.
- `Source/Rendering/RenderPass/RenderPassExecutor.cpp`: partitioned GPU preparation and dirty ranges.
- `Source/Utility/ImageTools.cpp`: background DDS cache generation.
- `Source/Platform/Video/GFXDevice.cpp`: command-buffer queues and frame/window flush.
- GL buffer allocator/lock implementations and Vulkan buffer/texture implementations: resource synchronization.

`PlatformContext` owns `HIGH_PRIORITY`, `LOW_PRIORITY`, `RENDERER`, and `ASSET_LOADER` pools.
Reuse these semantics for asynchronous asset/GPU preparation rather than introducing ad-hoc threads.
DDS/NVTT cache generation uses `HIGH_PRIORITY` when available.
Render preparation uses partitions (`g_nodesPerPrepareDrawPartition`) and shared locks around dirty ranges.

## CPU and resource lifetime checks

Audit dirty-range locks, allocator free-list locks, command-buffer queue locks, and background asset work that can race render/main-thread resource finalization.
Separate CPU work completion from GPU submission and completion: recorded commands execute later at window flush, as described in [rendering](rendering.md).
For changes to mapped writes or staging uploads, also read [GPU resources](gpu-resources.md).

## Vulkan queues and async compute

`Source/Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h` exposes `GRAPHICS`, `COMPUTE`, and `TRANSFER` queue types.
Selection and immediate-command use live in `vkDevice.cpp` and `VKWrapper.cpp`.
Several upload paths still use the graphics immediate-command context; queue enums are not evidence that async compute is wired end-to-end.

Any actual async-compute/transfer feature requires an audit of queue ownership, resource transitions/barriers, completion/lifetime synchronization, and render-stage dependencies.
Do not infer synchronization from descriptor binding type: indirect command-buffer hazards follow `BufferUsageType::COMMAND_BUFFER`; see [shaders and descriptors](shader-descriptors.md).
