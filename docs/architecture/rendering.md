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

FontStash text rendering is owned by `GUI` in `Source/GUI/GUI.cpp`. Its atlas
creation and resize callbacks must return nonzero only when the backing texture
is shader-readable; failed creation aborts GUI initialization rather than leaving
a context with a released texture. FontStash owns the callback cleanup during
failed creation and normal context destruction. `CreateFontVertexFormat` uses
the `FONSvert` layout for position, UV and normalized RGBA bytes; all three
attributes must be supplied to the GUI text shader on both backends.
Buttons retain independent references to their event sounds, release replaced
sounds and release their remaining references on destruction. Callers keep
ownership of the handles passed to `GUIButton::setEventSound`.

## Cross-system boundaries

Rendering owns generic mechanisms, not scene/game policy. Callers translate
feature-specific changes into bounds, layer masks and pass capabilities; do not
add feature names or feature-specific dirty reasons to lights, render passes or
backend APIs.

`LightPool::invalidateShadowLayers` accepts a caster volume, layer mask, light
type and optional influence volume. Matching requests accumulate in
`Light::shadowLayersDirty`; the existing movement snapshot preserves requests
for lights skipped this frame. Bits index CSM cascades; point and spot lights
each have one logical layer (a point layer contains all six cube faces).
Missing cache content or ordinary camera/light/geometry invalidation overrides
the mask with a complete refresh. CSM rendering updates only selected matrices
and layers, and blur/mipmap commands use contiguous selected-layer ranges.

`RenderPassParams::_enableDeformation` supplies `dvd_enableDeformation` to
shaders opting into optional procedural deformation. It defaults to enabled;
CSM enables it only for cascade 0, and world AO disables it. This does not
disable skeletal animation or implicitly classify objects by scene type.

## Command flow

Systems record backend-neutral rendering commands, then `GFXDevice` translates them through the active backend wrapper at flush time.

`GenericDrawCommand` is a CPU record, not a GPU wire layout. It uses natural
alignment with its buffer pointer first to retain a 40-byte size on 64-bit targets
without misaligned pointer members. Only its `_cmd` indirect record is uploaded;
keep the indirect GPU layout unchanged when adjusting CPU command storage.

- `GFXDevice::flushCommandBuffer()` appends buffers to the active window queue; it does not immediately execute GPU commands.
- `GFXDevice::flushWindow()` executes queued buffers through `flushCommandBufferInternal()` before backend window flush.
- `flushCommandBufferInternal()` walks commands, uploads descriptor state at submission boundaries, and dispatches backend operations.

Vulkan submission can combine adjacent indexed indirect draws that use the same
vertex/index buffer handles and consecutive records in the shared indirect
buffer. Each draw must describe one non-instanced geometry command, with the
same render options and byte-identical fast constants and loose-uniform values.
The bound graphics pipeline and actual buffer usages must permit the batch;
shaders that may read `DrawIndex` are excluded. The comparison occurs at
submission because uniform storage can change after recording. Batches do not
cross other commands or alter the original GPU records, and respect the
backend's strict indirect-draw count limit. OpenGL keeps its existing submission
path. This is not Vulkan geometry-buffer pooling: separate mesh buffers cannot
be combined by this optimization. For `BY_STATE` render bins, matching shader,
state and texture keys are secondarily ordered by the first draw command's
vertex/index buffer handles before the distance tie-breaker. This keeps repeated
meshes adjacent where their other sort keys match, without changing explicit
depth or back-to-front ordering.

Hi-Z indirect-command culling uses a bounded two-dimensional compute grid when
one workgroup row is insufficient. Its padded groups return before forming a
32-bit command index. A list exceeding the device's two-dimensional dispatch
capacity is reported and drawn without this optional occlusion cull rather than
issuing an invalid dispatch.

For bugs before submission, inspect shared command generation and ordering first. Window/frame boundaries matter for missing output or stale state.
Loose-uniform CPU updates are committed at draw/dispatch boundaries, including
consumers without a new uniform command. Their immutable cross-frame lifetime
and per-window completion contract are described in [shaders and descriptors](shader-descriptors.md).
`AnimationSystem::Update` skips components without an animator individually;
later animated components must still advance. Animation update events track
changes to the rendered frame, not the evaluator's neighbouring frame index.
Component or global pause/resume transitions also report a change when they
switch the rendered frame between the current animation frame and frame zero.

## Performance diagnosis

Instrumented Debug performance is an intentional target. Compare backends with
the same scene, camera, GPU, resolution, settings and cache warmth, and measure
other build configurations separately. CPU profiling scopes report elapsed wall
time, including waits, validation and driver calls; overlapping stage scopes
must not be added, and `TaskPool::wait` can execute work and callbacks. Measure
GPU execution separately before attributing a CPU scope to GPU cost.
`VKDevice::submitToQueue` separates queue-lock acquisition from `vkQueueSubmit`;
shader loading also exposes worker-side scopes, as described in
[Shaders and descriptors](shader-descriptors.md#shared-shader-flow).
Each render-stage task opens a `Record stage: <STAGE>` scope and
`GFXDevice::flushWindow` opens a `Submit: <command buffer name>` scope per
queued buffer (`PROFILE_SCOPE_DYNAMIC`, compiled out without Optick).
`Tools/Profiling/optick_capture.py` requests a CPU capture from a running smoke
attempt (`--attempt <dir> --output <file.opt> --start-after <present> --frames N`);
`Tools/Profiling/optick_summary.py <file.opt> [--json out.json]` prints frame
statistics, per-thread-group self/inclusive tables (pool idle scopes hidden
unless `--include-idle`) and the per-stage record/submit tables. Pair them with
the harness `-PerfMode` switch for validation-free timings. Repeat
`--callers <substring>` to report immediate parent scopes and per-frame call
counts for matching events.

Perf mode enables `GFXDevice::queryPerformanceStats()` every frame, and
`RenderPassManager::logPerfMetrics` logs two `[PerfMetrics]` lines every 64
frames. The first gives the frame GPU time, backend draws, HiZ counters
(`lastCullCount`: culled, overflow, skipped) and per-stage draws/bin items.
The second (`gpuPasses`) gives the average GPU time per queued main-window
command buffer, summed by name. GPU frame time comes from a per-frame-slot
timestamp pool on Vulkan and from `GPU_TIME` queries on OpenGL. Per-pass
times come from `RenderAPIWrapper::writePassTimestamp`, which `flushWindow`
calls before each queued buffer and after the last one (up to
`GPU_PASS_TIMESTAMP_CAPACITY`). `readPassTimestamps` returns results several
frames later: Vulkan after the frame-slot fence, OpenGL from a six-frame query
ring. `GFXDevice` keeps the buffer names per frame and accumulates results
only when the frame numbers and counts match. Immediate and transfer submits
are not covered. Pipeline-statistics queries are opt-in
(`queryPipelineStatistics`): on AMD OpenGL they cost about 10x the frame time.

The `[PerfMetrics]` print path runs only with the explicit smoke/performance
flag, not during ordinary editor rendering. It formats cached CPU counters and
accumulated timing data; printing does not issue GPU queries or wait for the GPU.
Timestamp collection itself still has nonzero driver/GPU overhead: Vulkan reads
an already-fenced frame slot without `VK_QUERY_RESULT_WAIT_BIT`, and OpenGL
checks the old ring slot's last timestamp for availability before reading its
results. Unavailable results are skipped rather than waited for. The global GL
frame-time query uses `GL_QUERY_RESULT_NO_WAIT`. Keep pipeline statistics and
detailed scope timing opt-in, and use the `logPerfMetrics` CPU scope when
checking logging overhead; nonblocking instrumentation is not zero-cost.

`-GpuScopeTimings` (requires `-PerfMode`; engine flag `--smokeGpuScopeTimings`,
`GFXDevice::queryGPUScopeTimings`) also writes timestamps at debug-scope
boundaries inside each buffer. `gpuPasses` then reports exclusive time per
`buffer/innermost scope` label, with names normalized by
`NormalizeGPUScopeName` so per-instance suffixes merge. Each buffer gets an
even share of the free slots (capacity 512), and the next buffer's boundary
slot is always reserved. A closed scope's parent timestamp is deferred until
the next non-barrier command, so repeated sibling scopes (per-chunk vegetation
culling, for example) merge. Past its budget, a buffer's remaining time goes to
its last timed label. On WarScene the overhead was within noise.

For startup, measure cold and warm scene readiness separately from frame time.
`ResourceCache::Build` includes the resource `load` call and synchronous waits,
not task-queue delay or GPU frame execution. Preserve shader-cache invalidation
while investigating loading.

With the Optick revision pinned in `Source/ThirdParty/CMakeHelpers/ImportPackages.cmake`,
a Vulkan GPU capture has triggered timestamp-query validation
`VUID-vkCmdWriteTimestamp-None-00830`. Its full query-reset, reuse and
synchronization behavior remains unverified. CPU-only captures are a
diagnostic workaround, not a GPU-profiler fix; do not suppress validation to
make a capture succeed. Recheck this limitation when the dependency or its
integration changes.

## Framework smoke diagnosis

`RenderSmokeTest`, `Kernel`, the Vulkan and OpenGL `flushWindow` implementations, and
`Tools/FrameworkSmokeTest.ps1` provide an opt-in local Windows diagnostic loop.
The runner builds the instrumented editor, runs focused engine tests and
launches one protected attempt; a coding agent diagnoses and changes code
between invocations. It snapshots logs and source state, uses an owned Windows
job for bounded process cleanup, and records each attempt outside the
checkout. The installed CDB runtime is copied into each attempt because the
packaged executable may escape the job. A controlled preflight checks that CDB
and its child inherit the job; the actual renderer waits before platform/GPU
initialization until its identity, parent and job membership are verified.
CDB captures the first validation failure when available; without it, the
process still terminates after flushing the failure. Smoke mode redirects MSVC
CRT abort reports to stderr without a modal dialog; CDB also breaks on CRT
`abort` to capture its originating stack. Normal interactive runs are unchanged.
For a Vulkan diagnostic, optional `-RenderDocInjectExe` injects the named
`renderdoccmd.exe` into the verified renderer while it is still at the pre-GPU
gate. The attempt's `renderdoc/target.json` records its PID and target ID for
a separate bounded target-control client, which must verify the PID and retain
the capture. Injection failure stops the attempt before releasing the gate;
capture instrumentation is not part of the 300-present acceptance contract.
The supervised capture skips only the optional Vulkan tool-property listing:
the installed RenderDoc/validation combination produced an out-of-bounds write
while enumerating it. This skip is recorded in the event and console logs;
Vulkan validation and all recovery/watchdog checks remain enabled. Ordinary
runs still enumerate tools. This workaround does not establish which layer
caused the inconsistent count or make other RenderDoc configurations safe.
Minidumps include
executable data segments and indirectly referenced memory; inspect
`GFXDevice::s_debugScope` and `s_debugScopeDepth` for the active replay scopes,
alongside the validation callback's command-buffer labels. No dump is uploaded.

Vulkan mode forces Vulkan, the editor, core and synchronization validation
without saving scene, editor layout or configuration changes. It uses the
Default project unless `-Project` selects another; `-Scene` selects that
project's scene. See [Scene loading](scene-loading.md) for the selection and
loading flow. Both smoke backends start the editor in Play mode so game time,
animation and wind advance, and the player camera supplies the display view.
Ordinary editor startup remains unchanged. Pausing switches to the independent
editor camera. Use `-EditorPaused` with `-DiagnosticPresents` to inspect the
ordinary paused-editor startup and saved dock layout without changing them;
this diagnostic does not count as Play-mode acceptance.
Diagnostic runs can select an existing material debug view with
`-MaterialDebugView` (for example `SHADOW_MAPS` or `CSM_SPLITS`); the runner
records it and requires `-DiagnosticPresents`. Diagnostic runs may request
more than 300 presents to let asynchronous scene assets load, but debug-only
captures never count as normal acceptance. Every Vulkan warning/error is fatal before ordinary interactive
filters except the user-approved `BestPractices-specialuse-extension` warning
specifically for `VK_EXT_custom_border_color`, which is recorded without
terminating. Errors are never exempt. Unrelated TODO/STUBBED messages are not
failure triggers.
Vulkan diagnostic runs can opt into `-TracePipelines` to flush each pipeline
creation's program, stage names, generations, and module handles to `events.log`
before the driver call. The trace distinguishes normal and wireframe pipelines
and is disabled for ordinary acceptance and profiling runs.
`-PerfMode` (engine `--smokePerfMode`) is an opt-in profiling run: it requires
`-DiagnosticPresents`, launches the renderer without a debugger, and disables
render API debugging and Vulkan validation/BestPractices so timings reflect the
engine rather than the debug heap or layers. It cannot be combined with
pipeline tracing, RenderDoc or AMD captures, and never counts as acceptance;
validated runs remain the correctness gate.
`Kernel` marks scene readiness after warmup and
marks a frame only when scene render commands were recorded. Both backends
count only successful main-window presents of those frames; Vulkan excludes
swapchain recreation. Each eligible present is recorded so the 30-second
no-progress watchdog works even for slow instrumented scenes. The overall
render deadline defaults to 120 seconds and can be extended with
`-RenderTimeoutSeconds` (up to 7200 seconds), without changing the no-progress
limit. Normal runs require at least 300 presents; `-MinimumPresents` can
increase that target for longer visual checks while keeping the same watchdogs
and success criteria. Success additionally requires the requested presents, a
completed scene image, the final main editor framebuffer, clean shutdown and
matching process/result evidence. The editor image is read back after ImGui
rendering and promoted from `editor.pending.png` to `editor.png` only after a
successful exit. The supervisor allows 60 seconds after capture for shutdown,
including completion of in-flight asynchronous shader loads; a timeout still
requires GPU recovery before another attempt. Inspect both images visually; a
completed present or image does not prove correct pixels. See
[GPU resources](gpu-resources.md#vulkan-smoke-readback-and-recovery) for
readback and device-loss handling.

The runner selects its backend with `--frameworkSmokeTest=Vulkan` or
`--frameworkSmokeTest=OpenGL`; the older backend-specific switches remain valid.
The editor image contains the main framebuffer, not OS window decorations or
detached editor windows. The backend switch overrides the configured rendering
API in memory only; `XML/config.xml` is not modified. OpenGL mode requires
`-ExpectedGPU` to match a device UUID exposed by the active GL
context; unsupported or mismatched identities fail explicitly. Use a separate
`-OutputRoot` for `-Backend OpenGL`, and retain the UUID reported by the Vulkan
run. OpenGL reads the scene texture and default back buffer before swapping,
then accepts them only after a successful swap. Its smoke mode does not turn
ordinary GL debug messages into the Vulkan validation failure policy.
Match framebuffer size, DPI, editor layout, scene, camera and timing when
comparing backends. A capture is not an automatic parity check, and UI animation
and presentation colour space can still differ.

The runner waits for a bounded fresh-device submission and fence on the same GPU
after a device loss or suspected GPU hang; waiting does not consume an iteration.
It recognizes `VK_ERROR_DEVICE_LOST` even when the validation callback reports
the result before the backend can classify it, and resumes an unverified
recovery before starting another renderer.
Observed display-reset evidence and the need for recovery are persisted, so an
interrupted supervisor still requires a successful probe even when the first
failure record does not mention `VK_ERROR_DEVICE_LOST`. Device-loss signatures
identify the failing Vulkan operation without incorporating volatile fault data.
Pass `-AmdCaptureExe` with the absolute path to RadeonDeveloperPanelCLI.exe to
start a separately owned crash capture before the renderer. Its adjacent
`rgd.exe` is required. The runner waits up to 20 seconds for late reports after
the renderer exits, then stops the owned capture tree. A nonempty
attempt-specific `amd-crash.rgd` takes precedence over downstream validation
errors and requires recovery; when available, the local text report supplies its
shader hash for repeated-failure accounting. If capture did not finish
successfully after a failed run that selected a GPU, recovery is verified even
without a report. An unexpected exit after selecting a GPU also requires the
same recovery probe, including when Radeon capture is disabled.
After three same-signature repairs without increased scene-present progress, a
device loss requires explicit user approval to continue; other repeated
no-progress failures stop. With explicit user approval,
`-ApproveStalledNonDeviceLoss` permits one supervised diagnostic attempt after
a non-device stop without resetting the recorded failures; an unsuccessful
attempt stops again. A completed diagnostic records presentation progress and
breaks the repeated-failure sequence but does not satisfy the 300-present
contract. When no engine failure record exists, a captured
debugger event and fault symbol distinguish unexpected exits; assertions also
include their logged source and message. Unexpected-call failures abort even
when ordinary Debug assertions continue; their diagnostics also reach stderr.
The repair budget defaults to 25
iterations; after explicit user approval, `-MaxAttempts` can extend it or set
it to `0` for no overall attempt limit. This does not reset recorded attempts
or the repeated-failure guards.
The watchdog cannot guarantee prevention or recovery of a system-wide GPU hang; do not
alter Windows TDR settings or deliberately trigger a reset.
Build failures and interrupted builds preserve their logs without consuming an
iteration or changing the renderer's no-progress signature. Each retry uses
a distinct artifact directory; successful builds and failed tests still count.
The debugger fixture publishes its PID only after closing the marker file.
The supervisor retries transient marker sharing and lock violations within its
existing readiness deadline; persistent and unrelated errors remain fatal, and
the PID must still pass the parent, process identity and job-membership checks.
On a startup, rendering, presentation-progress, or shutdown timeout, the
supervisor rechecks the debuggee's identity and job membership, requests a
bounded all-thread debugger capture, and then terminates the owned process.
An interrupted preflight can resume after confirming that no renderer was launched
and no helper from that attempt remains; its successful build remains counted.
The runner reuses an active matching x64 Visual Studio developer environment;
it rejects a mismatched one instead of silently switching compilers.
Use `-SkipUnitTests` to defer focused engine tests while repairing the smoke
run; run the focused tests and full internal suite after the smoke test passes.
After further changes, `-Revalidate` starts another attempt in the same recorded
repair budget without overwriting the successful evidence.

## Stage dependencies

`Kernel.cpp` registers the coarse render graph:

- `SHADOW` precedes `REFLECTION` and `REFRACTION` in GPU submission order only.
- `DISPLAY` and `NODE_PREVIEW` depend on both `REFLECTION` and `REFRACTION`,
  for both submission and recording.

`RenderPassManager::setRenderPass(stage, dependencies, recordingDependencies)` takes
two lists:

- `dependencies` sets the GPU submission order.
- `recordingDependencies` lists the CPU data dependencies. It must be a subset of
  `dependencies`, and this is asserted.

Each stage task is precreated and queued as soon as its recording dependencies
finish (`RenderStageSchedule`). It only records into its own command buffer
(`RenderPassData::_cmdBuffer`).

After `TaskPool::wait`, `RenderPassManager::render` runs on the main thread:

1. It flushes the stage buffers in `_submissionOrder`, which is a topological
   order of `dependencies` (`RenderStageSchedule::topologicalOrder`).
2. It calls `LightPool::patchShadowIndices`.
3. It calls `RenderPassExecutor::FlushBuffersToGPU`.

Host writes after the wait are safe on both backends, because
`flushCommandBuffer` only queues the buffer; translation happens later, in
`GFXDevice::flushWindow`.

`uploadLightList` writes `options.y = -1` and records the light behind each
slot. `patchShadowIndices` fills in the shadow-map index after SHADOW has
recorded. This removes the audited shadow-index read from concurrent stage
recording; it does not establish that every other shared-state access is safe.

Before changing ordering, audit render-target producers/consumers, depth and
clustered-light availability, and PostFX assumptions. Promote an edge to
`recordingDependencies` only when a stage reads CPU state written by another
stage while recording. `RenderPassManager` owns passes; `RenderPassExecutor`
performs per-stage preparation and draw execution. Waiting workers help execute
eligible ordinary renderer-pool tasks; see
[Threading](threading.md#entry-points-and-task-pools).

Each `uploadLightData` call acquires its own reusable `LightPool::ViewLightData`
snapshot. `LightDataBinding` carries that view's light and scene buffers through
`ProjectManager` and `RenderPassExecutor` to `Renderer::prepareLighting`.
Later cube faces, probes and planar views cannot overwrite an earlier view's
camera-space positions, directions, counts or ambient colour. Shadow indices
are patched into every snapshot used by the current player after the stage wait.
CPU view records are pre-sized contiguous values at light-pool construction,
bounded by `TotalPassCountForStage`; acquiring a view cannot allocate or move
earlier records. Light GPU buffers are created on first use and retained;
their capacity grows geometrically from 32 lights up to
`MAX_ACTIVE_LIGHTS_PER_FRAME`, rather than allocating the maximum for every view.
Existing frame/player ring lengths and deferred GPU-resource retirement apply.
Buffer bindings cover the allocated capacity; shader scene counts bound access
to eligible uploaded lights, including a zero-light view.
HiZ culling also records a separate `UniformDataSnapshots` entry for every
view, including every local player in the same frame. `SendPushConstantsCommand`
retains a pointer until translation, so reusing an executor's uniform block directly
would make earlier dispatches use the final view's entity count and frustum.
The same reusable snapshot storage backs immediate primitives; it resets only
when the engine frame changes, not between views or players.
Parallel reflection and refraction stages publish their texture selections under
the rendering component's package lock; display preparation snapshots those
selections after both producer stages complete.

`GFXDevice::generateCubeMap` and `generateDualParaboloidMap` acquire utility
cameras by render stage. These mutable cameras must not be shared between the
concurrently recording shadow and reflection/refraction stages.

Shadow, reflection and refraction select their views serially inside
`RenderPassManager::beginViewBatch` / `finishViewBatch`. `doCustomPass` captures
the camera snapshot, frustum, parameters and exact light binding before a
generator reuses its camera. Generators still own deterministic light/layer
selection, budgets, matrices, blur constants and shadow-property slots.
`RenderViewSchedule` assigns each stage-pass identity to one of two reusable
recording lanes. Each lane exclusively owns its executor queues, counters,
uniforms and indirect pages; repeated identities stay ordered on the same lane.
Indirect page ranges are lane-local, so the lanes divide rather than multiply
the regular stage allocation. Pages are created on first use and retain their
existing player/frame ring lifetime.

Rendering-component packages retain their own LoD selection. Distinct full
stage/pass/variant/index identities have separate preparation packages and
uniforms. Independent lighting records always rebuild their own grid inputs;
CPU completion order cannot determine which grid is needed in GPU order.
The GPU grid buffers themselves remain shared and sequential.

After all lane tasks join, completed view command pointers are transferred into
their original positions, in reverse insertion order. Surrounding blits, blur,
mipmap, probe-processing and vegetation barriers retain their exact GPU order;
no API translation or GPU scratch execution is parallelized here. Pending
shader feedback is collected per view and reduced serially. Shadow refresh
records outlive the batch, and light dirty flags are finalized only after it
joins. Callers must keep destination buffers and feedback targets alive through
`finishViewBatch`; nested batches and different destinations are rejected.
`-SerialViewRecording` on the smoke runner uses the same snapshots and slots
but records views in their original order, for controlled CPU comparisons.

Batch view records occupy one contiguous array allocated when `setRenderPass`
registers the stage, and lane executors and schedule capacity are also created
there, not lazily during recording. One lane executes on the calling worker;
a single-lane batch dispatches no extra task. The two-lane limit avoids the
measured contention from four lanes in the six-worker renderer pool.
View barrier lists clear without releasing their high-water capacity.
HiZ uniform snapshots use stable-address value storage pre-sized for the
stage/local-player view bound; immediate primitives use the same storage with
chunked growth for their unbounded draw counts, not one heap owner per snapshot.

This is not a claim that the renderer is allocation-free. Command/payload
storage and indirect pages retain workload-dependent high-water growth.
Per-view GPU light buffers are first-use/growing resources so an unused view
does not reserve a maximum-light, all-player ring; multiplying that maximum by
every possible view would waste substantial memory. These remaining growth
paths must be distinguished from recurring per-frame allocations. Follow
`AGENTS.md` for render allocation policy and restricted `Attorney` access;
the lighting regressions use `Attorney::LightPoolTestAccess`.
Dynamic commands already use the thread-local C-11 `MemoryPool` through
`GFX::CmdAllocator`; command buffers use `CommandBufferPool`, and GPU loose
uniform uploads use `UniformUploadArena`. Reuse these facilities where their
ownership/lifetime fits instead of adding another pool. CPU uniform snapshots
use an EASTL deque of values for stable addresses and chunked growth; their
retained payload capacity still needs to warm up for new uniform layouts.

Frustum traversal uses each pass camera, while scene visibility ranges and LoD
use the active player/editor camera. Node previews use their own camera.
Draw sorting retains the pass-camera distance. Visibility-locked nodes,
including global water and scattered tree meshes, bypass spatial culling;
vegetation compute still applies the active-camera range and the pass frustum.
The far plane is enforced by the pass frustum, not a radial distance test, so
orthographic world-AO coverage is not truncated by its vertical depth range.
Coarse state culling preserves the current stage, variant, light/target index,
and subpass; it only applies exclusions that cover every pass type. Exact
pass-type exclusions are applied when drawing each queue. Reflection and
refraction sources are selected against the active display view, independently
of exclusions from their own auxiliary rendering passes.

The display depth prepass writes velocity and normals. Reflection, refraction,
probe and node-preview prepasses are depth-only; their material shaders retain
alpha discard without emitting the display G-buffer outputs. View prepasses
exclude the translucent bin so opaque geometry behind translucent surfaces
supplies the depth and colour used by forward or OIT compositing. Shadow passes
still include translucent casters and use their alpha-discard shadow shaders.

SSR is applied only to the opaque surface represented by the display depth and
normal prepass. OIT and sorted-transparent material variants retain probe/sky
IBL and direct lighting but do not reuse that opaque surface's SSR result.
Applying it to overlapping transparent layers introduces incorrect reflections
and temporal feedback through the previous composited screen, including black
foliage and water pixels. Transparent SSR requires its own compatible depth and
normal representation; it is not implemented.
Hi-Z, SSAO and other prepass consumers therefore see opaque depth, not a
depth-correct representation of transparent surfaces. Translucent grass can
receive the resulting screen-space occlusion, but does not contribute its
own depth to that SSAO calculation.
Water's reflection and refraction passes clear depth in the prepass and retain
it for the colour pass, whose opaque materials use an equal-depth test.
Terrain deliberately uses lower tessellation in secondary passes; matching the
display pass is only appropriate for temporary diagnosis. Investigate clipping
and camera alignment rather than retaining equal tessellation to hide seams.
The infinite plane is a fallback and remains excluded from refraction so it
cannot obscure overlapping terrain.
Water's planar targets contain linear HDR scene radiance, not albedo. Its
display shader mixes them once with air/water Fresnel, applies screen-space
reflections (SSR) to the reflection component when available, and preserves
opaque coverage when adding the sun highlight. Missing planar variants use
the available input rather than mixing in black; underwater water uses
refraction without the intentionally skipped planar reflection. The water
prepass writes the material's explicit roughness so SSR sees the intended
surface.

## Per-node GPU preparation

`RenderPassExecutor` compacts scene data into buffers for transforms, materials, indirection, and indirect draw commands. Relevant data structures include `NodeTransformData`, `NodeMaterialData`, and `NodeIndirectionData`.

Node, camera (`GFXShaderData::CamData`), and shadow (`LightPool::ShadowProperties`)
GPU records must keep their SIMD members naturally aligned, including when nested
or stored in arrays. Do not pack their enclosing structs: this can make Clang
emit aligned vector accesses to unaligned addresses. Renderer unit tests check
their sizes and offsets against the existing shader-visible layouts.

Instanced nodes set the `INDIRECT_DATA_IDX` loose uniform through the render
package's `pushConstantsCmd()._uniformData`; preprocessing packs it into a
per-draw uniform block, not the `PushData0`/`PushData1` fast constants. Ordinary
draws use `baseInstance - 1` for the indirection index. `RenderingComponent`
considers the scene-node instance count before material readiness and also
detects instancing when commands are built. Command collection and subsequent
pass-package initialization both retain the component's actual indirection
index; a new prepass, transparency, or OIT package must not replace it with a
placeholder.
Shader-defining changes defer drawing until the scene
update invalidates the old shader and the replacement is ready. A shader
discovered to be pending during preparation also keeps shadow casters dirty,
so a partial shadow map is not cached. Main-pass preparation removes nodes
whose draw packages could not be prepared before building indirect records;
later passes must not submit those nodes using stale package offsets.

Reflection cube pass indices reserve the environment-probe and skylight cubes
before custom cube reflectors. Planar reflection and refraction indices are
local to their respective budgets; `IndexForStage` applies the face and target
offsets once when selecting an indirect-command buffer region.

Dirty ranges permit partial uploads. Preparation is partitioned with `g_nodesPerPrepareDrawPartition`; see [threading](threading.md) before changing parallel preparation or locking.
Start here for CPU render scalability, draw-call buildup, or stale per-node data.

Material cache hits refresh the entry's lifetime under the shared lookup lock;
parallel hits use an atomic lifetime update. A matched material index must be
written to both the component and the GPU indirection data.

Per-node buffer slots are selected before recording the skylight or any stage
commands; descriptors capture that selection, and uploads target the same slot
after preparation finishes. Each ring slot accumulates all changes since its
previous use. New or resized buffers mark every slot for a full upload, including
on OpenGL, and initial-data sizes are bytes. The ring grows with the local player
count so queued player passes and in-flight frames do not reuse the same slot.

Indirect command buffers use a separate frame ring per player and render stage.
Every pass for that player records its buffer slot before submission; distinct
stage-pass indices use separate ranges, and repeated indices use reusable
additional pages. Command lists larger than a regular range use separate
high-water overflow pages, because multiple pass records can be generated per
visible node. An overflow list must still fit the device's maximum storage-buffer
range. The slot advances once per player frame, not once per pass, so a run of
shadow or reflection passes cannot wrap onto in-flight commands. The indirect
binding and the Hi-Z compute binding capture the same page and slot.

The camera block's previous view-projection is the selected player's previous
projection multiplied by its previous view. It is refreshed when switching
players even if that player's cached matrices did not change.

## Clustered lighting

`Renderer.cpp` allocates stage-scoped light indices, cluster AABBs, light grids, and global index counts. Compute pipelines generate AABBs, reset counters, and cull lights.
Their resources are registered in `PER_PASS` slots by `GFXDevice::initDescriptorSets()`.

Cluster conventions shared by the builder (`lightBuildClusteredAABBs.glsl`) and lookup (`Common/lightInput.cmn`):

- XY tiles are viewport-relative (`gl_FragCoord.xy - dvd_ViewPort.xy`), bottom-up on both backends, and clamped to the grid.
- Z slices are exponential in view distance: `slice = log(d) * dvd_LightScale + dvd_LightBias`, with scale/bias computed from the camera planes in `GFXDevice::renderFromCamera`. Lookup recovers `d` with `ViewDistanceFromDepth`.
- Clustered stages require finite `0 < zNear < zFar` (`ClusteredLighting::IsValidClusterDepthRange`). `Renderer::prepareLighting` asserts and skips cluster building otherwise. This validates the depth range, not the camera projection type.
- Each cluster has one `MAX_LIGHTS_PER_CLUSTER` budget shared by point and spot lights; indices are written points first, then spots.
- `rendering.numLightsPerCluster` is normalized before shader and renderer startup: negative values select the engine maximum, zero warns and becomes one, and larger values are capped at the engine maximum. The effective value sizes both the shader array and the light-index buffer.
- Spot lights are culled with the minimal sphere around their cone; the slant height travels as float bits in `_options.z` (`SpotConeSlantHeight`).
- Each recorded view uploads at most `MAX_ACTIVE_LIGHTS_PER_FRAME` eligible lights. A view with no eligible lights skips the light-buffer write but still uploads its own scene data with zero light counts.

`HiZConstruct.glsl` max-reduces each output texel's full source footprint; push data `isBaseLevel` enables the conservative one-texel dilation for the first level only.

`GFXDevice::initRenderingAPI` checks every fixed compute workgroup in `REQUIRED_COMPUTE_WORKGROUPS` with `SupportsComputeWorkgroup`. Device limits are inclusive maxima, both per axis and for total invocations. Unsupported devices fail with `ErrorCode::GFX_OLD_HARDWARE`. Vulkan guarantees only 128 invocations, while some groups use 256 or 512, so keep this table in sync with shader `local_size` values. Dispatches round up, so compute shaders that write images must bounds-check `gl_GlobalInvocationID` against the target size.
### Shadow maps and spotlights

`Camera::lookAt` accepts a world-space eye and reconstructs the camera
orientation from the inverse view rotation. CSM, spot and cubemap shadow
cameras, as well as world AO, depend on this shared camera transform.
The mutable `viewProjectionMatrix()` accessor also refreshes the combined
matrix when the frustum is dirty, even before a full `updateLookAt()`.
`ShadowMap::BiasedViewProjection` applies the XY bias after projection for
directional, spot, point and world-AO shadow matrices.
Point-shadow sampling currently uses a cube direction, not these stored matrices.
Frustum extraction uses the shared zero-to-one depth convention. Sphere and box
classification checks every remaining plane after an intersection; the cached
plane is only an ordering hint and concurrent display passes update it atomically.
Directional, point and spot shadow strength scales occlusion rather than total
light visibility: zero removes shadowing, one preserves the sampled result, and
values above one deepen partial shadows without dimming fully lit pixels.
Variance-shadow light-bleed reduction has a defined limit at the editor's
maximum bias of one: probabilities below one are rejected and a probability of
one remains lit, without dividing by zero.
Spot shadow lookups reject fragments behind or outside the XY-biased,
zero-to-one-depth shadow clip volume before sampling its clamped texture.
Directional, point, spot and world-AO lookups use explicit texture gradients
derived from world-position derivatives captured at fragment entry before alpha
discard, water branches, clustered-light loops and shadow coverage tests, so
different lights within a derivative group do not supply one another's
projection.
Variance shadow moments are calculated before alpha discard in material,
vegetation and particle shadow fragments, so discarded neighbors do not make
the depth derivatives undefined.
IBL irradiance and BRDF-LUT gradients are captured from the prepared surface
before alpha discard and before selection or debug branches, then passed to
the probe lookup; explicit gradients preserve mipmapped filtering. Terrain's
`NO_REFLECTIONS` disables IBL and uses zero gradient placeholders. Enabling
terrain IBL would require derivative-safe gradients for its derivative-built
geometric basis. Screen-space normal, SSAO and SSR lookups use viewport-derived
gradients, and gradients of water's wave-distorted reflection coordinates are
captured before its debug and underwater branches.

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
If a visible shadow caster's material shader is still pending, its shadow map
stays dirty and is regenerated until the shader is ready. Hidden nodes and
permanently failed shader variants do not force repeated shadow rendering.
Terrain and instanced trees participate in directional cascades. Grass is
excluded from every shadow pass, including the world-AO pass; SSAO provides
general screen-space occlusion and screen-space grass shadows are planned.
Cascade extent thresholds intentionally increase with distance. The intended
far cascade prioritizes terrain and very large geometry, while nearer cascades
retain progressively smaller casters; the exact size policy is not final.
Visibility-locked scattered trees currently bypass coarse extent filtering.

Directional shadow caches depend on the active camera's clip planes, view and
projection; compare the cheaper clip planes before the matrices. Range changes,
including edits through a mutable `range()` reference, also invalidate the light's
cached shadow. Directional near-clip offsets and scene-fit toggles are tracked
as well, including direct property mutations. Each active cascade uploads its
own split distance and matrix; fragments outside the active splits are
unshadowed rather than indexing a nonexistent split. Each farther frustum
overlaps the last 10% of the preceding split, where directional visibility
smoothly blends to the next active cascade. The final split does not fade to
an absent cascade, and an unavailable adjacent sample is not blended in.
Directional VSM minimum variance and depth bias are scaled from cascade 0 by
the current light-projection depth gradient, preserving their world-space scale.
Point, spot and world-AO variance policies are unchanged. Different caster
coverage and filter footprints can still change individual shadows; blending
does not add casters to a farther cascade.
Directional caches conservatively invalidate for moved scene volumes even
outside the light's local bounding sphere, which does not bound the cascades.
Changed caster bounds publish their previous and current spheres together;
removal publishes the last sphere, and static/dynamic changes invalidate both
classifications. Bounds update events are emitted only for components whose
dirty bounds were recomputed; a new dirty mark during recomputation remains
pending for the next update. Cascade scene-fit feedback stores per-split bounds snapshots,
not scene-node pointers, and excludes unready casters.
Cascade frusta reconstruct their near plane at zero-to-one depth. The shadow
buffer stores the active camera's view-depth row alongside its cascade data,
so reflection and refraction fragments select the same cascade as the camera
that generated the maps. CSM and world AO share a single-layer draw target;
each cascade is resolved and copied to its reserved shadow-array layer before
the target is reused. MSAA remains enabled when configured. Layered MSAA
resolve behavior outside this path remains unverified. Layered shadow blurs use
invocation-relative kernel sizes and preserve other lights' layers. Default
shadow render states honor a material's double-sided setting. Light, scene and
shadow buffer rings reserve enough slots for every local player across the
in-flight frame limit; per-player descriptor bindings retain their selected slot.

The scene and shadow passes use the active player camera while playing and the
editor camera while editing. A focused node preview routes camera input to its
own camera without replacing the scene camera. Underwater post-processing is
updated from the active camera after its view is refreshed, including camera
switches and changes to the water bodies.

World AO is a top-down mask protecting indoor areas from incorrect global sky
probe samples; SSAO handles general-purpose ambient occlusion. When the mask is
below 1, the global sky probe is not sampled, preventing sky-colour leakage
beneath overhead geometry; local probes, material ambient and direct lighting
remain independent. When shadow mapping is disabled globally, the mask is
unavailable and treated as unoccluded. Disabling only directional shadows does
not disable world AO. Its view-projection is biased in XY for texture lookup
and samples outside the volume are unoccluded. A dedicated camera renders the
mask; its matrix is uploaded with the player's shadow properties and captured
by the existing per-frame descriptor binding, rather than shared camera state.
The shadow block remains available to materials that disable direct shadow
mapping but still use world AO. Its
1000-by-1000 horizontal footprint follows the active camera. Its height comes
from the scene graph's cached static-geometry AABB with 50 units of margin above
and below; an empty scene uses the world origin and reports a warning. The
separate static AABB excludes sky, water, infinite planes and dynamic nodes
without changing the general scene-graph root bounds. It is refreshed after
static geometry is added, removed, transformed or changes its source bounds,
not by scanning the scene every frame. Vegetation contributes conservative
world-space height bounds for terrain-rooted instances; grass wind and camera
displacement are lateral and are not included in its bounds padding. Dynamic
terrain transforms remain unimplemented. This height selection does not filter
dynamic occluders from the world-AO render pass.

The editor's `Debug > BRDF Settings > WORLD_AO` view displays the sampled mask
on standard lit materials: white is unoccluded, darker values suppress the
global sky probe, and disabled world AO displays white. Grass uses the same
lighting and debug views for opaque and partially transparent texels. The sky
is white for this view. HDR/LDR filter operators and bloom are bypassed while
it is active so their effects do not obscure the mask; other material debug
views retain their existing post-processing behavior.

Spotlights default to inner/outer half-angles of 15/35 degrees. XML loads
normalize legacy inverted angles; programmatic setters maintain their order,
and editor edits clamp the changed field to its counterpart so undo/redo does
not silently change both. Equal angles produce a hard cone. The CPU and GPU
both bound the attenuation's spherical sector using the same slant height.
Authored wide angles and the existing tangent-based falloff are preserved: the
slant height can become very large near 90 degrees, and the fixed 90-degree
spot shadow camera does not cover all illumination from wider cones.

### Vegetation culling data

Vegetation instance buffers are partitioned by terrain chunk, with fixed-capacity
slots per chunk and separate active instance counts. Compute culling must check
the active count before indexing a chunk's slots. Tree culling compacts visible
instances with positive integer-valued LOD flags and marks the unused draw-buffer
tail with a negative `data.z` sentinel for the full-count fallback. The tree
vertex shader tests the sentinel before converting it to `uint`; LOD 3 is
reserved for that culled fallback.

Tree mesh draws use separate child `RenderingComponent` packages per species.
Each child binds its own instance buffer and supplies `dvd_instanceDataIndex`
as the absolute offset of its species range in the chunk's fixed-capacity block.
The uniform's GLSL initializer is not a fallback because shader preprocessing
strips loose-uniform initializers.

Materials that require instanced indirection must mark their rendering component instanced even for a single instance. Instanced draws upload `INDIRECT_DATA_IDX` and use base instance zero; ordinary draws encode the node indirection index plus one in the base instance. Grass and tree shaders use a draw-local instance index: OpenGL's `gl_InstanceID` is already local, while Vulkan's `gl_InstanceIndex` includes the base instance. Vegetation buffers accept CPU writes, and empty chunks do not issue zero-length uploads. A terrain can omit tree meshes when it only needs grass.

Tree culling compacts visible instances. One workgroup per species writes visible entries into the species' range of `_treeDrawData` (`PER_PASS` 14, `COMPACTED_INSTANCE_DATA`) and the visible count to `_treeVisibleCounts` (`PER_PASS` 15, `VISIBLE_INSTANCE_COUNTS`, element `chunkID * MAX_VEGETATION_TREE_MESHES + speciesID`). Ranges come from the `dvd_treeRangeOffsets`/`dvd_treeRangeCounts` uniforms. Each tree `RenderingComponent` reads instances from the compacted buffer (`setInstanceShaderBuffer`) and names its count element with `setInstanceCountSource`.

Grass uses the same compaction loop, with one workgroup per chunk and separate
`_grassDrawData`/`_grassVisibleCounts` buffers. Slots 14/15 are rebound before
each grass or tree dispatch; draws read compacted data at `PER_DRAW` 13.
Frustum/range culling always reads immutable source instances, while Hi-Z
recompacts the current visible prefix in place. Each round finishes reading
before overwriting that prefix. The unused tail retains negative `data.z`
sentinels for the full-count fallback. Grass retains its distance fade and
packs LOD plus the original instance index modulo 12 in `data.y`, preserving
the modulo-3/modulo-4 colour variation despite compaction reordering. The
shared expressions are in `grassInstanceMetadata.cmn`. Grass remains excluded
from shadow passes. This adds one instance-sized draw buffer and one U32
count per chunk; it does not reduce visible grass transparency overdraw.

### GPU instance-count patch

The CPU `instanceCount` in indirect commands stays at the full instance count: both backends reject zero-instance draws on the CPU. `RenderPassExecutor` appends `InstanceCountPatch` records (command index, count element) after the pass's draw commands in the indirect buffer. After command preparation and the occlusion cull, `GFXDevice::patchInstanceCounts` dispatches `instanceCountPatch.glsl`. It sets `instanceCount = min(instanceCount, counts[element])` using the count buffer bound at `PER_DRAW` 13. Records are grouped by count buffer. If a regular page fits its draw commands but not its patch records, the patches are dropped with a one-time warning and the draw falls back to the full count, which is still correct.
Hi-Z culling queries select a mip level and use nearest texel and nearest-mip sampling with a minimum LOD of zero. This preserves the max-reduced depths used for conservative visibility; the reduction itself reads single-mip views with `texelFetch`. Vegetation culling records a buffer dependency for each written buffer after the compute dispatches and before the subsequent draw commands.
Independent vegetation packages can combine their boundary barriers within one
view when their written buffer ranges do not overlap. Their dispatch order is
unchanged; overlapping or non-independent packages retain their original
barriers. Preparation and occlusion are batched separately, on opposite sides
of the same view's depth and Hi-Z work. Opt-in packages must declare every
written range and have no hidden inter-package dependencies.
Nonempty indirect-command lists are culled even when they occupy less than one
workgroup; padding lanes never address a draw command.

Water positions are surface centers, with full X/Z dimensions and a positive
depth. CPU camera tests and GPU terrain queries use the same position-relative
footprint. The scene-designated global water is always body 0; optional local
bodies follow it. The scene buffer supports `GLOBAL_WATER_BODIES_COUNT` bodies;
unused slots are cleared each update. Vegetation culling uses only body 0's
surface height, independent of its finite footprint or depth.
Water materials default to planar reflection and refraction, matching their
callbacks. The water shader samples these targets only when the corresponding
planar material variant is enabled. Reflection and refraction offset screen-space
UVs by the wave normal's view-space deviation from the flat surface normal,
scaled by the water noise factor and clamped to the target. A zero noise factor
keeps both samples undistorted. This is an artistic approximation rather than a
depth-aware physical model.

Material colour and depth passes use the same selected alpha source. Phong
shininess maps monotonically to perceptual roughness. Metallic-roughness direct
lighting uses the material F0; diffuse IBL does not apply the metal fraction a
second time. Irradiance samples the world-space surface normal, while specular
samples use world-space reflection directions and local probe box projection.
Blinn-Phong materials receive diffuse irradiance as well as their existing
specular environment and material ambient terms; sky-probe irradiance follows
the world-AO mask, while local probes remain independent.
SSAO attenuates the indirect IBL and material ambient contribution, including
environment specular as an approximation. Screen-space reflections are mixed
afterward; direct lighting, emission, editor selection highlights, and fog are
not multiplied by SSAO. Secondary views disable SSAO as before.
OpenGL enables seamless cubemap sampling and Vulkan samplers use the default
seamless behavior. Source environment mips are generated before convolution;
the explicitly prefiltered destination mips must not be regenerated afterward.
Normals use inverse scale and node rotation; tangents use forward scale with
orthogonalization and mirrored-scale handedness. Vegetation instances are
already positioned in world space, so their normal/tangent basis uses the
instance orientation rather than the parent node transform. Grass and tree
velocity currently projects the current instance geometry through the previous
camera; per-instance deformation, LOD and prior animation are not buffered.
Terrain's normal-debug geometry derives height gradients using the heightmap
texel spacing, vertical amplitude and the terrain/node transforms. Terrain
shading itself uses geometric screen-space derivatives for its tangent frame.
Particle geometry without indices uses an equivalent sequential index buffer,
and each emitter has its own material template using instanced indirection.
Particle preparation sorts and uploads live particles once per distinct eye
position per frame. Depth, colour and transparency passes retain the same
immutable upload through `RenderPackage::drawCommandsOverride`, including the
instance counts used to build indirect commands. Different views cannot change
queued buffer handles or select a later mutable ring index. Geometry and indices
are shared; position/colour storage grows only to the per-frame view high-water mark.
Particle bounds enclose the world-space centres and billboard extents, including
under singular node transforms. Particle emitters require nonzero capacity and
position, acceleration, colour and misc arrays; empty geometry skips geometry
buffer allocation and drawing. Explicit geometry indices must reference a
vertex or use the `U32_MAX` primitive-restart marker with primitive restart
enabled. Simulation uses the elapsed update time, and
each source tracks fractional emission separately for each particle-data
instance without accumulating whole-particle backlog while full.

### Custom-pass Hi-Z

Custom passes build Hi-Z from their own depth target and camera. Main, reflection, and refraction use separate stage-level Hi-Z scratch targets; reflection and refraction passes share their stage target across reflectors rather than retaining per-reflector history. The render command stream rebuilds the target as each pass executes.

Each nonempty vegetation chunk supplies its own counts to the cull dispatch;
tree instances carry their species' conservative local culling radius.
Preparation commands execute before each view's depth and colour
passes, outside render-pass scope. The shared instance buffers are synchronized
between views, and culling uses the current camera's frustum and distance rather
than another reflector's Hi-Z history or an unconditional underwater exclusion.
After the same view's depth prepass and Hi-Z construction, a separate package
phase performs per-instance occlusion before colour/transparency draws. It only
removes already prepared instances and preserves their LoD/fade data; the next
view resets visibility in its normal preparation phase. Views without a depth
prepass/Hi-Z, including shadows, retain frustum/distance culling only. Bounds
crossing the near plane or viewport remain visible. Four exact max-depth texels
must cover the entire projected footprint, and equal-depth bounds remain visible
to prevent self-occlusion. Grass bounds include wind, camera displacement and
fade offsets; tree bounds use the species radius plus the maximum wind offset.
Near-tree vertices above the scaled 0.85-unit trunk threshold receive the
original inexpensive sinusoidal wind sway in depth and colour, the nearest CSM
cascade (index 0), and close-up local shadow views. Farther CSM cascades and
world AO use undeformed silhouettes.
The height check uses the already scaled/rotated vertex, not a second scale.
The existing far-tree LoD cutoff remains. Wind and culling read the same
frame's scene wind data; vegetation motion vectors are still not implemented.
Tree placements remain static, but their materials are dynamic. Vegetation
coalesces wind changes once per resource per frame, including the transition to
still air, and submits generic shadow invalidations with mask `1` (cascade 0).
For local lights it supplies both the vegetation bounds and a camera-centred
influence sphere with 33% of the effective tree visibility radius; this policy
stays in `Vegetation::sceneUpdate`, not the light pool. No new distance setting
is introduced. Ordinary invalidations and pending casters still force complete
updates. Paused, unchanged wind does not force shadow refreshes. Aggregate
vegetation bounds retain high-water wind padding.
Tree-only chunks still run culling, and chunks with trees remain eligible through
the larger of the grass and tree visibility distances. Visible tree submeshes
schedule their owning chunk's preparation even when that chunk is absent from
the view, including previews and shadow passes with different exclusion masks.
Shadow preparation culls tree instances but does not dispatch grass culling.
Each chunk is prepared once per view. Tree submeshes bind their own instance
buffer and species-range offset. Vegetation's CPU range check uses horizontal
distance, matching the GPU check even for elevated cameras. Tree graph nodes bypass their single-mesh CPU frustum
bounds because those bounds do not cover the scattered instances; per-instance
GPU culling uses an origin-centred radius covering every submesh's local bounds.
`rendering.reflection.includeGrass`, `includeTrees`, and
`includeDynamicObjects` independently control scene content in planar
reflections and environment probes. Defaults include grass and trees but
exclude other dynamic objects. Tree submeshes carry a vegetation-tree flag so
they are not mistaken for ordinary meshes. The sky-only probe keeps its own
draw mask. Refraction and the display pass are unaffected; grass is composited
in the translucent pass with distance-faded alpha, and grass shadow rendering
retains billboard alpha discard. SSR is independent of these options and only
supplies reflections for suitable on-screen hits.

For stage-specific lighting failures, check stage buffer allocation, bindings, dispatch dimensions, and shader-header cluster/thread-group constants. See [shaders and descriptors](shader-descriptors.md).

## Render targets and feature insertion points

Shared targets are created in `GFXDevice.cpp`: `SCREEN`, `SCREEN_PREV`, `NORMALS_RESOLVED`, `SSAO_RESULT`, `SSR_RESULT`, `BLOOM_RESULT`, `OIT`, and utilities such as Hi-Z/blur targets.
Feature-local scratch targets often belong to `PreRenderBatch` or individual PostFX operators.
Both passes of `GFXDevice::blurTarget` record their own push constants before
the first draw; layered box blur updates the layer before every draw.
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
paths clamp their table indices. Half-resolution SSAO groups source pixels into
aligned 2x2 blocks, clamping incomplete blocks at the image edge. It selects one
depth deterministically, including ties, and copies that sample's normal.
Upsampling compares depth and normals across the same clamped texels used by
the bilinear AO footprint. Its full-resolution output and blur scratch target
resize together; the half-resolution output and depth/normal target round up
odd dimensions, with a minimum of one pixel, so no source edge is omitted.
Downsampling stores the selected source offset alongside depth and normal.
Half-resolution generation reconstructs that full-resolution pixel center and
maps projected samples back to their actual 2x2 blocks. Upsampling uses the
same block grid for its depth/normal footprint and linear AO lookup, including
the shorter final interval at odd image edges; it does not stretch the grid
over the rounded-up half-resolution target.
Generation noise scaling uses the actual target dimensions, and full- and
half-resolution modes retain their configured blur
kernel sizes independently. Each mode also owns a separate generation uniform
payload sized to its compiled sample count; upload the kernel elements, not the
dynamic vector object. This keeps quality-mode switches between different
sample counts within the shader's reflected array bounds.
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
Shadow previews retain one shader reference per layered/cubemap variant until
shadow-map shutdown. Removing a selected light unregisters its views; layer
and face uniforms use 32-bit integer values. Removed debug views remain alive
through the main-window command flush because recorded commands reference their
uniform data.
SSR traces view-space ray segments in either depth direction, clips them to the
near plane, ray endpoint and screen rectangle, and reconstructs hit positions
with the same perspective-correct progress used by the march. It reconstructs
scene depth from the current projection and uses a parallel camera direction for
orthographic views. Previous-frame colour is sampled only for valid clip-space
reprojections whose expected view depth matches the valid previous-depth
history within a distance-dependent tolerance. The colour is stored unweighted;
the material pass applies the SSR blend weight once. The depth test reduces
disocclusion artifacts but does not track motion of individual objects, so
temporal reflections remain approximate. SSR is applied only in the display pass; other
views always sample an explicitly initialized transparent-black texture.
The display pass and PostFX debug view use the same fallback when SSR is disabled,
and `ApplySSR` returns its input unchanged for zero confidence. Scene and editor
filter sources are tracked independently; operators are toggled only when the
combined state changes. Editor controls disable all active sources.
OIT composition selects a colour-only shader when its destination has no normal
attachment, as in planar reflection and refraction targets. Destinations with a
normal attachment use the colour-and-normal variant for either MSAA mode.
It preserves the existing normal/roughness RGB for revealage at or above 0.5.
Below that threshold it selects the normalized coverage-weighted sum of OIT
normals and zero roughness, unless the sum is degenerate. Transparent materials
compute their own normal and roughness rather than sampling the opaque prepass.
The weighted normal is still an approximation rather than a depth-correct
transparent G-buffer. OIT modulation and composition preserve destination alpha;
the modulation output alpha is initialized even though it does not contribute.
Grass and general transparent material shaders discard zero-coverage texels
before normal/BRDF evaluation. Zero alpha must not enter weighted accumulation:
non-finite lighting multiplied by zero can still contaminate the target.
Positive fractional coverage is retained, including grass distance fade and
filtered canopy edges; this is not the opaque alpha-discard threshold.
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
