# Rendering

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and ownership

- `Source/Core/Kernel.cpp`: app-wide render-stage registration and dependencies.
- `Source/Managers/Headers/RenderPassManager.h`: stage-pass ownership.
- `Source/Rendering/RenderPass/RenderPassExecutor.cpp`: stage render queue and shared per-node GPU preparation.
- `Source/Platform/Video/GFXDevice.cpp` and `Source/Platform/Video/Headers/GFXDevice.h`: shared rendering state, descriptor registration, pipelines, command handling, frame/window flush.
- `Source/Platform/Video/Headers/RenderAPIWrapper.h`: backend execution interface.
- `Source/Rendering/Renderer.cpp`: clustered-forward lighting preparation.
- `Source/Rendering/PostFX/`: post-processing/pre-render operators, including `PreRenderBatch.cpp` and `PostFX.cpp`.

## Command flow

Systems record backend-neutral rendering commands, then `GFXDevice` translates them through the active backend wrapper at flush time.

- `GFXDevice::flushCommandBuffer()` appends buffers to the active window queue; it does not immediately execute GPU commands.
- `GFXDevice::flushWindow()` executes queued buffers through `flushCommandBufferInternal()` before backend window flush.
- `flushCommandBufferInternal()` walks commands, uploads descriptor state at submission boundaries, and dispatches backend operations.

For bugs before submission, inspect shared command generation and ordering first. Window/frame boundaries matter for missing output or stale state.

## Stage dependencies

`Kernel.cpp` registers the coarse render graph:

- `SHADOW` precedes `REFLECTION` and `REFRACTION`.
- `DISPLAY` and `NODE_PREVIEW` depend on both `REFLECTION` and `REFRACTION`.

Before changing ordering, audit render-target producers/consumers, depth and clustered-light availability, and PostFX assumptions. `RenderPassManager` owns passes; `RenderPassExecutor` performs per-stage preparation and draw execution.

## Per-node GPU preparation

`RenderPassExecutor` compacts scene data into buffers for transforms, materials, indirection, and indirect draw commands. Relevant data structures include `NodeTransformData`, `NodeMaterialData`, and `NodeIndirectionData`.

Dirty ranges permit partial uploads. Preparation is partitioned with `g_nodesPerPrepareDrawPartition`; see [threading](threading.md) before changing parallel preparation or locking.
Start here for CPU render scalability, draw-call buildup, or stale per-node data.

## Clustered lighting

`Renderer.cpp` allocates stage-scoped light indices, cluster AABBs, light grids, and global index counts. Compute pipelines generate AABBs, reset counters, and cull lights.
Their resources are registered in `PER_PASS` slots by `GFXDevice::initDescriptorSets()`.

For stage-specific lighting failures, check stage buffer allocation, bindings, dispatch dimensions, and shader-header cluster/thread-group constants. See [shaders and descriptors](shader-descriptors.md).

## Render targets and feature insertion points

Shared targets are created in `GFXDevice.cpp`: `SCREEN`, `SCREEN_PREV`, `NORMALS_RESOLVED`, `SSAO_RESULT`, `SSR_RESULT`, `BLOOM_RESULT`, `OIT`, and utilities such as Hi-Z/blur targets.
Feature-local scratch targets often belong to `PreRenderBatch` or individual PostFX operators.
Passes use `BeginRenderPassCommand` with ordinary pipeline/resource binding commands.

- Screen-space effects using existing inputs generally belong in PostFX/pre-render operators.
- Stage-specific lighting or visibility preparation generally belongs in `Renderer` or render-pass code.
- Persistent outputs consumed by multiple stages generally need a named shared target in `GFXDevice`.

Presentation and colour-space invariants are in [GPU resources](gpu-resources.md).

## Planned/incomplete feature integration

These are readiness notes, not promises of runtime support; recheck the current implementation.

### Mesh shaders and meshlets

Scaffolding exists: `ShaderType::MESH` / `TASK`, glslang stage mapping, generated `GL_EXT_mesh_shader` headers, GL/Vulkan shader-stage tables, `PrimitiveTopology::MESHLET`, and separate dispatch validation.
Vulkan detects/enables the extension when present, but its mesh/task feature-enabling block is commented out; there is no mature first-class end-to-end runtime path.

Do not route meshlets through ordinary indexed/indirect draws: classic indirect submission rejects `MESHLET`. Audit topology branches, pipeline creation, dispatch encoding, descriptor expectations, mesh/task workgroup limits, and unsupported-device fallback.

Start with `Source/Platform/Video/Headers/RenderAPIEnums.h`, `Source/Platform/Video/Shaders/ShaderProgram.cpp`, `Source/Platform/Video/Shaders/GLSLToSPIRV.cpp`, `Source/Platform/Video/CommandBuffer.cpp`, and `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp`.

### Hardware ray tracing

`Source/Platform/Video/RenderBackend/Vulkan/Headers/vkInitializers.h` has acceleration-structure/ray-tracing pipeline helper initializers, not a complete RT integration.
Missing integration includes device extensions/features, ray shader-stage enums, acceleration-structure descriptor types, BLAS/TLAS lifetime/build/compaction support, scratch/update buffers, pipeline creation, barriers, and ray dispatch.
The shared shader model currently covers raster, compute, mesh, and task stages; resources/uploads are buffer/image-centric.

Start with `vkDevice.cpp`, `vkInitializers.h`, `RenderAPIEnums.h`, `ShaderProgram.cpp`, and `RenderAPIWrapper.h`. RT requires a cross-cutting rendering-model change, not just another Vulkan source file.

### Clustered geometry and volumetrics

The clustered system is lighting-centric. Geometry clustering can reuse stage-scoped compute/buffer patterns and indirect infrastructure in `RenderPassExecutor`, but must explicitly choose `PER_BATCH`/`PER_PASS` ownership and audit backend indirect draws.

Fog is scene-state/config-driven, not a full volumetric pipeline. Choose deliberately between screen-space PostFX, clustered/froxel compute, and stage-integrated lighting. History/scattering/froxel outputs may require persistent named targets.
Start with `GFXDevice.cpp`, `PreRenderBatch.cpp`, `PostFX.cpp`, `Renderer.cpp`, and `RenderPassExecutor.cpp`.

For async compute, see [threading](threading.md); queue enums alone do not establish an async execution path.
