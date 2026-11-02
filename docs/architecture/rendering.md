# Rendering

<!-- Copyright (c) 2026 DIVIDE-Studio -->

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

Cluster conventions shared by the builder (`lightBuildClusteredAABBs.glsl`) and lookup (`Common/lightInput.cmn`):

- XY tiles are viewport-relative (`gl_FragCoord.xy - dvd_ViewPort.xy`), bottom-up on both backends, and clamped to the grid.
- Z slices are exponential in view distance: `slice = log(d) * dvd_LightScale + dvd_LightBias`, with scale/bias computed from the camera planes in `GFXDevice::renderFromCamera`. Lookup recovers `d` with `ViewDistanceFromDepth`.
- Clustered stages require finite `0 < zNear < zFar` (`Config::Lighting::ClusteredForward::IsValidClusterDepthRange`). `Renderer::prepareLighting` asserts and skips cluster building otherwise. This validates the depth range, not the camera projection type.
- Each cluster has one `MAX_LIGHTS_PER_CLUSTER` budget shared by point and spot lights; indices are written points first, then spots.
- `rendering.numLightsPerCluster` is normalized before shader and renderer startup: negative values select the engine maximum, zero warns and becomes one, and larger values are capped at the engine maximum. The effective value sizes both the shader array and the light-index buffer.
- Spot lights are culled with the minimal sphere around their cone; the slant height travels as float bits in `_options.z` (`SpotConeSlantHeight`).
- Each render stage uploads at most `MAX_ACTIVE_LIGHTS_PER_FRAME` eligible lights. A stage with no eligible lights skips the light-buffer write but still uploads scene data with zero light counts.

`HiZConstruct.glsl` max-reduces each output texel's full source footprint; push data `isBaseLevel` enables the conservative one-texel dilation for the first level only. The scene UBO (`PER_FRAME` 8) is visible to compute because vegetation culling reads water data.

`GFXDevice::initRenderingAPI` checks every fixed compute workgroup in `REQUIRED_COMPUTE_WORKGROUPS` with `SupportsComputeWorkgroup`. Device limits are inclusive maxima, both per axis and for total invocations. Unsupported devices fail with `ErrorCode::GFX_OLD_HARDWARE`. Vulkan guarantees only 128 invocations, while some groups use 256 or 512, so keep this table in sync with shader `local_size` values. Dispatches round up, so compute shaders that write images must bounds-check `gl_GlobalInvocationID` against the target size.
### Shadow maps and spotlights

Light components pass their initial range to the `Light` constructor (point 20, spot 30, directional
`g_defaultLightDistance`); serialized scene values override it on load.

The shadow pass assigns per-type property indices afresh after eligibility and
successful generation or cache reuse. A cached layer range is reusable only
while every reserved layer still belongs to that light and has the expected
content-layer count. Directional lights reserve full `MAX_CSM_SPLITS_PER_LIGHT`
blocks even when they render fewer splits, so mixed split counts cannot fragment
the configured budget. The last CSM texture layer belongs to world AO and is
never allocated to a light. The CSM resource remains available for world AO
when directional shadows are disabled but other shadow types are enabled.

Directional shadow caches depend on the player camera's clip planes, view and
projection; compare the cheaper clip planes before the matrices. Range changes,
including edits through a mutable `range()` reference, also invalidate the light's
cached shadow. Directional near-clip offsets and scene-fit toggles are tracked
as well, including direct property mutations. Each active cascade uploads its
own split distance and matrix; fragments outside the active splits are
unshadowed rather than indexing a nonexistent split.

Spotlights default to inner/outer half-angles of 15/35 degrees. XML loads
normalize legacy inverted angles; programmatic setters maintain their order,
and editor edits clamp the changed field to its counterpart so undo/redo does
not silently change both. Equal angles produce a hard cone. The CPU and GPU
both bound the attenuation's spherical sector using the same slant height.
Authored wide angles and the existing tangent-based falloff are preserved: the
slant height can become very large near 90 degrees, and the fixed 90-degree
spot shadow camera does not cover all illumination from wider cones.

### Vegetation culling data

Vegetation instance buffers are partitioned by terrain chunk, with fixed-capacity slots per chunk and separate active instance counts. Compute culling must check the active count before indexing a chunk's slots. Tree culling stores a negative `data.z` sentinel for hidden instances and positive integer-valued LOD flags for visible ones; the tree vertex shader must test the sentinel before converting the value to `uint`. LOD 3 is reserved by the vertex shader for the culled fallback.

Tree mesh draws use separate child `RenderingComponent` packages. Each child carries its terrain chunk ID in `dataFlag`, which must be supplied as `dvd_terrainChunkOffset` for every draw package; the tree shader uses it to select that chunk's fixed-capacity block. The uniform's GLSL initializer is not a fallback because shader preprocessing strips loose-uniform initializers.

Materials that require instanced indirection must mark their rendering component instanced even for a single instance. Instanced draws upload `INDIRECT_DATA_IDX` and use base instance zero; ordinary draws encode the node indirection index plus one in the base instance. Grass and tree shaders use a draw-local instance index: OpenGL's `gl_InstanceID` is already local, while Vulkan's `gl_InstanceIndex` includes the base instance. Vegetation buffers accept CPU writes, and empty chunks do not issue zero-length uploads. A terrain can omit tree meshes when it only needs grass.

Hi-Z culling queries select a mip level and use nearest texel and nearest-mip sampling with a minimum LOD of zero. This preserves the max-reduced depths used for conservative visibility; the reduction itself reads single-mip views with `texelFetch`. Vegetation culling records a buffer dependency for each written buffer after the compute dispatches and before the subsequent draw commands.
Nonempty indirect-command lists are culled even when they occupy less than one
workgroup; padding lanes never address a draw command.

Water positions are surface centers, with full X/Z dimensions and a positive
depth. CPU camera tests and GPU vegetation/terrain queries use the same
position-relative footprint. The scene buffer supports
`GLOBAL_WATER_BODIES_COUNT` bodies; unused slots are cleared each update.

### Custom-pass Hi-Z

Custom passes build Hi-Z from their own depth target and camera. Main, reflection, and refraction use separate stage-level Hi-Z scratch targets; reflection and refraction passes share their stage target across reflectors rather than retaining per-reflector history. The render command stream rebuilds the target as each pass executes. Vegetation remains excluded from reflection and refraction by default; if that changes and vegetation needs prior-view Hi-Z per reflector, those passes will need distinct history targets.

For stage-specific lighting failures, check stage buffer allocation, bindings, dispatch dimensions, and shader-header cluster/thread-group constants. See [shaders and descriptors](shader-descriptors.md).

## Render targets and feature insertion points

Shared targets are created in `GFXDevice.cpp`: `SCREEN`, `SCREEN_PREV`, `NORMALS_RESOLVED`, `SSAO_RESULT`, `SSR_RESULT`, `BLOOM_RESULT`, `OIT`, and utilities such as Hi-Z/blur targets.
Feature-local scratch targets often belong to `PreRenderBatch` or individual PostFX operators.
The exposure histogram has 256 bins and receives explicit zero initial data on
every buffer creation, including reuse of an OpenGL allocation. Vulkan flushes
non-coherent initial uploads and orders staged storage-buffer uploads before
shader reads and writes. The histogram and averaging dispatches use buffer
dependencies covering both reads and writes; the averaging shader resets the
bins for the next pass. Workgroup barriers synchronize shared histogram
initialization, accumulation and reduction. The weighted reduction uses
floating-point sums to avoid 32-bit integer overflow at large resolutions.
The averaging luminance image retains sampled and storage creation capabilities.
Each averaging pass transitions it from `SHADER_READ` to `SHADER_READ_WRITE` and back;
creation usage flags must not be confused with the current image layout.
Post-AA quality level zero disables the filter; SMAA accepts levels 1-4 and
FXAA accepts levels 1-5. The host normalizes configured levels and both shader
paths clamp their table indices. Half-resolution SSAO selects one of its four
sampled depths deterministically, including ties, before copying that sample's
normal.
Octahedral normal encoding uses a nonzero sign when folding the negative
hemisphere, including normals aligned with the negative Z axis.
Bloom downsampling derives texel spacing from the bound source view. Its
threshold uses a soft knee, or a hard threshold for a zero/negative knee, and
every upsample pass rebinds its pipeline and radius data.
Tone mapping handles black without dividing by zero, and every exposed tone-map
function has a distinct selector. Infinite-grid depth uses the same zero-to-one
projection convention as the render backends. DoF uses a symmetric depth blur
and orders vignette edges; equal vignette edges produce a hard cutoff.
Framebuffer debug views initialize all shared preview uniforms per view.
Numerical render targets bypass sRGB decoding; signed normals and velocities
are remapped for display, and zero-length normalization is defined.
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
