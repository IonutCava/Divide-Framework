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

The opt-in performance mode reports frame and GPU pass timing, plus each stage
task's CPU recording time (`[PerfMetrics] cpuStageMS`), without changing
ordinary editor logging. Stage tasks run in parallel, so their sum is CPU work
rather than frame latency. Instrumentation has a cost, so use it to answer a
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

## View pass order and transparency

`RenderPassExecutor::doCustomPass` draws a non-shadow view in this order:

1. Depth pre-pass for the opaque bins. The display stage also writes normals
   and velocity.
2. Hi-Z culling, light culling, and display pre-render effects such as SSAO
   and SSR.
3. Alpha-tested pass for the `ALPHA_TESTED` bin, grouped by pipeline state and
   sorted front to back within each group. It writes depth, and on the display
   stage it writes velocity and resolves it again under MSAA. As the first
   colour pass, it applies the main pass colour clears.
4. Opaque main pass over the pre-pass bins. Its `EQUAL` depth test shades each
   visible sample once and skips samples that alpha-tested geometry covers.
5. Translucent pass: weighted, blended OIT, or sorted forward blending when the
   view has no OIT target. Weighted OIT weights fragments by linear view depth
   (McGuire and Bavoil 2013, eq. 8) so nearer layers dominate.

`RenderQueue::PopulateQueueParams::ForPass` maps each `RenderBinContent` to its
bins. Shadow passes draw every bin in one depth pass. The alpha-tested and
translucent passes are skipped when their bins are empty. Both colour passes
bind the same per-pass inputs through `bindColourPassResources`.

Materials opt in with `Material::Properties::alphaTested`; it takes effect only
when the material has transparency (`Material::isAlphaTested`). Use it for
binary cutouts such as foliage. Weighted OIT cannot make opaque layers hide one
another, so overlapping cutouts in the translucent pass blend regardless of
depth. Vegetation marks its grass and tree materials as alpha tested.

Alpha-tested materials:

- Cut at `ALPHA_DISCARD_THRESHOLD`, the same cutoff that shadow passes and
  coverage-preserving mipmaps use.
- Render without back-face culling in the colour pass and flip back-face
  normals, like blended transparency.
- Must not force early fragment tests in their colour shader. Forced early tests
  write depth before the alpha test can discard the fragment.
- Use alpha-to-coverage on multisampled targets: the display stage with screen
  MSAA, and CSM or spot shadow maps with shadow MSAA. `alphaTestCoverage()` in
  `FragmentAtoms/alphaTest.frag` sharpens alpha around the cutoff and is a plain
  alpha test elsewhere. `alphaTestFade()` fades coverage without moving the
  cutoff. Shaders and pipelines must agree, so turning MSAA on or off for those
  targets recompiles material shaders.

Alpha-tested geometry is absent from the pre-pass depth and normals, so Hi-Z
occlusion, SSAO, and SSR do not see it. SSR is skipped on alpha-tested
fragments; SSAO still samples the surface behind them.

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

Node culling runs first, on one box per grass chunk and one per tree species in
each chunk. `Vegetation::updateChunkBounds()` sets these boxes through
`BoundsComponent::setWorldBoundsOverride()` when a chunk is finalized. Each box
holds the chunk's instance origins, padded by the reach the cull shader tests
(`VegetationGrassReach()`, `VegetationTreeReach()`). The padding grows only when
stronger wind needs more. Nodes outside a view or the vegetation range are not
prepared, and a chunk dispatches its per-instance compute cull only when its
grass or one of its tree nodes is visible. Range tests on vegetation nodes use
horizontal distance, as the cull shader does. Grass package LoD selects the
billboard-plane partition (6 → 3 → 2) from chunk distance; the cull shader's
per-instance fade thins coverage at the visibility edge so distant grass does
not hard-cut. Tree nodes carry `VEGETATION_TREE` for vegetation policy
(horizontal range, reflection filters, cascade fit). Shadow passes still
frustum-cull them like other casters; only trees that intersect a cascade are
prepared. The nearest CSM cascade may include those trees in its scene-AABB
fit; farther cascades omit vegetation from the fit so chunk-sized boxes do
not step cascade size and rebuild the static shadow cache. Alpha-tested grass
and canopies skip screen-space SSAO and SSR; they are absent from the depth
pre-pass, so sampling those buffers would apply occlusion or reflections from
surfaces behind them.

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

## Planned/incomplete feature integration

Treat planned or partially implemented features as unsupported until the full
path exists in current source. Shader-stage mapping or extension detection
alone does not establish a usable runtime feature.

Mesh shaders, hardware ray tracing, and clustered geometry require complete
capability checks, pipeline creation, dispatch/submission, resource lifetime,
and unsupported-device behavior before clients can depend on them.
