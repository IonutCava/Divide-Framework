# Rendering

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and ownership

- `Source/Core/Kernel.cpp` registers render stages and their dependencies.
- `Source/Managers/Headers/RenderPassManager.h` owns stage passes.
- `Source/Rendering/RenderPass/RenderPassExecutor.cpp` prepares render data and records draws.
- `Source/Platform/Video/GFXDevice.cpp` owns shared GPU state and submits command buffers.
- `Source/Rendering/Renderer.cpp` prepares clustered lighting.
- `Source/Rendering/PostFX/` contains post-processing and pre-render operators.

## Cross-system boundaries

Rendering owns generic mechanisms, not scene or game policy. Features submit
bounds, masks, capabilities, and invalidation requests through shared
interfaces. Do not add feature-specific names or policies to backend APIs,
lights, or render-pass infrastructure.

Shadow invalidation follows renderable caster bounds and visibility changes.
Feature systems translate their own changes into those generic requests.
Preserve pending invalidations until the affected maps have been refreshed.

## Command flow

Systems record backend-neutral commands. `GFXDevice::flushCommandBuffer()`
queues them for a window; `flushWindow()` translates and submits them through
the active backend. Recording and GPU execution are separate phases.

Window flushing and command translation run on the application main thread.
Render workers prepare data and record into their stage-owned command buffers.
See [Threading](threading.md) for task ownership and [GPU resources](gpu-resources.md)
for resource lifetime.

Vulkan may combine adjacent compatible indirect draws at submission. The draws
must preserve buffer, pipeline, render-state, and per-draw data requirements.
Batching must not reorder commands or change indirect records. OpenGL keeps its
backend submission path.

Hi-Z culling is optional. Reject padded workgroups before indexing commands,
and keep out-of-range or unsupported workloads on the normal draw path rather
than issuing invalid GPU work.

## Performance diagnosis

Compare the same scene, camera, GPU, resolution, settings, and cache state.
Measure Debug and optimized builds separately. CPU profiler scopes include
waits, validation, and driver work; they do not measure GPU execution.

Use CPU profiling and GPU timestamps as separate measurements. Do not sum
overlapping scopes or treat individual captured frames as independent runs.
Keep validation and profiling settings consistent within a comparison, and
account for warmup and capture overhead.

The opt-in performance mode reports frame and pass timing without changing
ordinary editor logging. Instrumentation has a cost, so use it to answer a
specific question and compare repeated runs against a non-instrumented baseline.

For scene startup, separate cold and warm resource loading from steady-state
frame time. A resource load may include synchronous work and waits that do not
appear in GPU timing.

## Framework smoke diagnosis

`Tools/FrameworkSmokeTest.ps1` runs a supervised local build, focused tests,
and one renderer attempt. It keeps logs and captures outside the checkout.
Smoke mode does not change ordinary editor behavior or persist diagnostic
configuration changes.

Vulkan validation failures stop the attempt and preserve the first failure.
The runner owns and bounds the renderer process; it must not terminate
unrelated applications. Captures and crash evidence remain local.

After device loss or a suspected GPU hang, retain the evidence and verify that
a fresh device on the same GPU can submit work and complete a fence before
starting another renderer attempt. A present count or a completed capture does
not prove that the rendered image is correct; inspect the captured output.

Use the runner's preflight mode before relying on local debugger or process
supervision. See [GPU resources](gpu-resources.md#vulkan-smoke-readback-and-recovery)
for readback and recovery behavior.

## Stage dependencies

`Kernel.cpp` registers the coarse stage graph. `RenderPassManager::setRenderPass`
accepts GPU submission dependencies and CPU recording dependencies. Recording
dependencies must be a subset of submission dependencies.

Stage tasks record into their own command buffers and never wait for other
stages. After recording work joins, `RenderPassManager::render` submits buffers
in topological GPU order, applies deferred light data, and uploads prepared
buffers.

Add a CPU recording dependency only when a stage reads CPU state written by
another stage. GPU producers and consumers still require the correct submission
order and resource barriers. Audit render-target inputs, depth, lighting, and
post-processing when changing the graph.

Views that share mutable cameras, packages, buffers, or uniform data need
separate snapshots or serialized ownership. Keep view data alive through
recording, submission, and any deferred translation that retains pointers.

## Per-node GPU preparation

`RenderPassExecutor` prepares transform, material, indirection, and indirect
draw data in shared GPU buffers. Dirty-range updates allow partial uploads.
Keep CPU records aligned with their shader-visible layouts and test those
layouts when changing the records.

Repeated views or passes must not overwrite data still referenced by recorded
commands. Each consumer captures the selected frame/player slot; upload and
retirement follow that same selection. Join preparation workers before
uploading their writes.

## Clustered lighting

`Renderer` builds light data and cluster inputs for each view. The compute
stages construct cluster bounds, reset counters, and assign eligible lights.
The shading code uses the same viewport, depth, and cluster conventions as the
builder.

Validate camera depth ranges and device workgroup limits before dispatch.
Shaders that write images must reject invocations outside the target. Keep
shader limits supplied by the owning C++ subsystem rather than duplicating
feature capacities in shared shader code.

### Shadow maps and spotlights

Directional, point, and spot shadows use cached render targets. Cache keys and
invalidation must account for light/view transforms, caster bounds, render
state, and resource readiness. Do not publish a cache as valid while required
casters or shaders are unavailable.

The world-AO mask is a top-down coverage map used to prevent global sky-probe
lighting from leaking beneath overhead geometry. SSAO handles general
screen-space occlusion. World-AO eligibility uses the generic per-instance draw
mask; keep category policy in the owning feature and invalidate old coverage
when policy changes.

Camera, frustum, and light data are captured per view. Do not share mutable
utility cameras across concurrently recorded passes. Defer cache finalization
until all recording work that can update it has joined.

### Vegetation culling data

The vegetation subsystem owns instance generation, visibility policy, and
shader inputs. Render infrastructure sees generic buffers, bounds, and draw
commands rather than tree- or grass-specific policy.

Compute culling reads source instances and writes compacted visible instances
and counts. Each draw uses the matching compacted range and count. An in-place
compaction pass must finish reading a source range before overwriting it.
Synchronize compute writes before draws consume them.

Frustum, distance, and Hi-Z culling operate per view. A later view must not
reuse another view's visibility results. Shadow, reflection, refraction, and
display passes may use different policies, but those policies belong to the
vegetation subsystem.

### GPU instance-count patch

Some backends reject zero-instance indirect draws before GPU execution. When
GPU culling determines the visible count, preserve a valid CPU-side draw record
and apply the count before submission. If optional patch storage is exhausted,
fall back to the full count rather than corrupting or dropping the draw.

### Custom-pass Hi-Z

Custom passes build Hi-Z from their own depth target and camera. Keep Hi-Z
history and scratch resources scoped to the views that produce them. Bounds
that cross the near plane or viewport remain visible unless the full projected
footprint can be tested conservatively.

## Render targets and feature insertion points

`GFXDevice` owns targets shared across rendering stages. `PreRenderBatch` and
PostFX operators own feature-local scratch targets. Add a shared target only
when multiple stages need the same persistent output.

Keep target formats, sample counts, usage flags, and transitions consistent
with their consumers. MSAA resolve and retained attachment contents are
separate choices. Synchronize writes before later passes sample or copy the
resource.

Screen-space effects that use existing inputs generally belong in PostFX or
pre-render operators. Stage-specific lighting and visibility preparation belong
in renderer or render-pass code. Preserve the shared
[coordinate and colour-space conventions](gpu-resources.md) across backends.

Screen-space god rays (`FILTER_GOD_RAYS`, `GodRaysPreRenderOperator`) build a
depth occlusion mask and radial-blur toward each directional light's projected
screen position. Per-light gating uses `DirectionalLightComponent::enableGodRays`
(default on). The half-res result (`GOD_RAYS_RESULT`) composites additively in
`toneMap.glsl` before the tonemap curve. This is independent of shadow-map
volumetric scattering.

## Planned/incomplete feature integration

Treat planned or partially implemented features as unsupported until the full
path exists in current source. Shader-stage mapping or extension detection
alone does not establish a usable runtime feature.

Mesh shaders, hardware ray tracing, and clustered geometry require complete
capability checks, pipeline creation, dispatch/submission, resource lifetime,
and unsupported-device behavior before clients can depend on them.
