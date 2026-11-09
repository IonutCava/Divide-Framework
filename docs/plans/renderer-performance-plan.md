<!-- Copyright (c) 2026 DIVIDE-Studio -->

> Living plan. Agents: update the status checklist below as work items complete, and keep findings in the relevant section.

# Renderer performance plan (WarScene, Divide-Framework)

## Engineering style (agreed)
- Middle ground; avoid over-engineering in either direction:
  - No ASM or compiler-trick hacks. SIMD is fine and encouraged where hot.
  - No deep OOP: avoid virtual dispatch in hot paths, smart-pointer churn and per-object heap allocations.
- Backend code (GL/VK translation, pools, allocators): go as low-level as needed while staying readable. Use flat arrays/SoA, handles instead of pointers, linear/frame allocators, and switch-based dispatch over virtuals.
- Pre-size bounded view records as contiguous values and create lane executors at initialization, not during recording. Reuse existing memory pools/arenas for genuinely dynamic storage; pool growth and synchronization are not free. Use the existing `Attorney` pattern for private/protected access, including tests. See `AGENTS.md` and the rendering guide for allocation exceptions.
- User-facing APIs (CommandBuffer, bindless handles, geometry pool): keep them as human-friendly as possible, with POD commands stored contiguously, predictable branches, `[[likely]]`/`[[unlikely]]` on hot branches and cache-coherent iteration.
- Industry patterns to borrow, where they fit:
  - **Sort-key draw buckets** (Bitsquid/Stingray, "Order your graphics draw calls around"): a 64-bit key of pass, pipeline, material/bindless set, geometry pool and depth, followed by a radix sort.
  - **GPU-driven rendering** (Ubisoft AC Unity, Frostbite, UE5 Nanite-lite subset): per-instance data in one SSBO, compute culling writing indirect args plus a draw count, `vkCmdDrawIndexedIndirectCount`/`glMultiDrawElementsIndirectCount`.
  - **Bindless:** a global descriptor heap with stable indices and deferred free (Doom Eternal, Traverse Research "Bindless Rendering", UE5 / The Forge). Material data indexes textures; no per-draw descriptor writes.
  - **Unified geometry buffer:** one large VB/IB per vertex format with an offset allocator (Sebastian Aaltonen's OffsetAllocator), as in UE5, Godot 4.x and Wicked Engine.
  - **Parallel command recording:** per-thread command pools/secondary buffers and a fixed per-pass ordering index (Frostbite frame graph, simplified; no full render graph needed).
  - **Frame-linear allocators** for command data and transient uploads (Naughty Dog's fiber/frame allocators, simplified).
  - **Shadow caching:** update static cascades and point/spot shadow maps only when lights or casters move, or amortize one cascade per frame (common in UE/Unity HDRP).
  - **Reflection/refraction LOD:** low-detail scene list (no grass, lower LOD, reduced resolution) for water/planar reflection passes.
- Every proposed borrowing is optional and is adopted only if profiling shows it pays.

## Out of scope now, but must stay possible (future-proofing)
- Not in this effort: clustered/tiled lighting rewrites, ray tracing, mesh/task shaders, upscalers (DLSS/FSR/TAA), and RT GI/reflections.
- Design constraints so these are not cut off:
  - **Bindless + unified geometry pools** must be usable for future ray tracing: keep per-mesh offsets/strides queryable, allow index/vertex buffers with the device-address/acceleration-structure-input usage flags on Vulkan, and use stable global material/texture indices (RT hit shaders need them).
  - **Per-instance data** stays in one GPU buffer with stable instance IDs. Keep previous-frame transforms available (or easily addable) for motion vectors used by TAA/DLSS/FSR.
  - Keep **sub-pixel jitter** possible in camera projection; don't bake projection assumptions into culling/HiZ code. Don't remove velocity/motion-vector paths, if any.
  - **Parallel recording/ordered slots** must allow inserting new passes (upscaler, RT dispatch, async compute later) without reworking the scheduler.
  - The **command buffer API** stays extensible for new command types (dispatch rays, build AS, mesh draw) via the same POD-command mechanism.
  - Shadow caching and cheap reflection lists must be optional per-feature policies, so RT reflections/shadows can replace them later.
  - **Async render/record pipelining** (future, not now): e.g. simulation/recording of frame N+1 overlapping API translation/submission of frame N on a dedicated render thread. Keep it possible by:
    - Recorded command buffers fully own or reference frame-snapshotted data: camera snapshots, per-frame transform/material buffers, no live SceneGraphNode pointers read during translation.
    - Arena/frame allocators are multi-buffered per frame in flight, not single global scratch.
    - API translation does not assume it runs on the main/window thread, except for genuinely thread-bound calls (GL context, swapchain present). Isolate those behind one submit point.
    - The ordered-slot queue is a self-contained per-frame object that can be handed to another thread.
    - The parallel-recording work (Phase 1/2) is the first step towards this; adding a separate render thread later should be a scheduling change, not a data-model rewrite.

## Goals (agreed)
- Reference workload: WarScene from the last saved camera: Sponza fully in view with HiZ culling active, several moving lights (light culling active), trees, grass, terrain, water, sky, PostFX, MSAA and shadows. Targets apply to this full workload, with no features disabled to reach them.
- Moving lights are non-deterministic, so object colours can legitimately differ between captures/frames. Visual comparisons must tolerate this; correctness checks focus on geometry presence, culling, shadows and validation errors, not exact pixels.
- Culling must stay correct: optimizations must not "win" by over-culling. Track visible-node counts per stage, HiZ-culled counts and light-cluster counts in the baseline, and compare them after every change.
- Primary target: **Debug instrumented editor** (`windows-msvc-debug-editor-instrumentation`), WarScene.
  - Debug: ≤ 33 ms (30 FPS) on both backends. Release/profile: < 16.6 ms (> 60 FPS).
  - Vulkan must be at least on par with, ideally faster than, OpenGL.
- Baseline (Optick CPU wall-clock medians): GL Debug 181 ms / Release ~76 ms; VK Debug 517 ms / Release ~411 ms (validation ON).
- Vulkan perf measured with **validation OFF**; validation-ON runs are correctness gates only.
- Default Vulkan MSAA shading follows OpenGL's per-pixel behavior, as explicitly requested; preserve cross-backend visual parity.
- Keep SceneGraphNode and RenderStage. Targeted rewrites of the command-buffer API are allowed where they help batching/submission.
- New engine functionality requires Catch2 coverage; Phase 5 collects final
  validation rather than delaying tests for new behavior.
- Existing code/architecture is a guide, not proven design: unusual choices may be plain mistakes. Question them, fix or replace freely when measurements or correctness justify it, and note any bugs found along the way.

## Key findings driving the plan

These are initial observations, before the completed work in the status
checklist below. Later measurements and fixes supersede some of the conclusions.

1. **Stage chain is the CPU critical path.** Kernel.cpp declares SHADOW → {REFLECTION, REFRACTION} → {DISPLAY, NODE_PREVIEW}. Main thread spends ~50 ms in `TaskPool::wait`. Stage tasks only *append* command buffers to the window queue; translation runs serially on the main thread in `GFXDevice::flushWindow`. Much of the chain enforces submission order, not data order.
2. **Real CPU data dependencies** between stages: `LightPool::generateShadowMaps` writes `_shadowBufferData`/`s_shadowBuffer` (consumed by later stages via GPU only); `sortLightData` per stage; shared static executor buffers (`s_materialBuffer`, `s_transformBuffer`, `s_indirectionBuffer`); env-probe refresh in REFLECTION; reflector budgets. These must be audited before parallel recording.
3. **Vulkan is validation-dominated** (single vkQueueSubmit ~160 ms; ~12 µs per indirect draw) → first measure validation-off.
4. **Per-draw state cost**: ~6.2k–7.8k draws, ~6.8k push-descriptor updates, ~7.3k–8.9k `bindShaderResources`, ~10.2k `ShaderBuffer::writeBytes` + `LockManager::waitForLockedRange`, ~1.8k pipeline binds, ~1.27k VB/IB binds, ~1055 compute dispatches, 296 barrier calls. Indirect batching only merges consecutive draws with identical buffers/state/push constants.
5. Shader stubbing had negligible effect → CPU/driver bound.
6. Workers show large uninstrumented time inside `runTask`; the external parser only reports top exclusive scopes, so per-stage inclusive costs are invisible.
7. `RenderQueue::sort` and draw-command preparation had an optional-parent task
   allocation leak; fixed and subsequently covered by the shared `TaskGroup`
   lifetime regressions. See Phase 1 status.
8. EASTL already in vcpkg.json and partially used.

## Phases

### Phase 0 — Measurement foundation (do first; everything else is judged against it)
- **p0-harness-perf-mode**: add opt-in Vulkan validation-off perf mode to `Tools\FrameworkSmokeTest.ps1` (default stays validation-on); support both backends selecting API without silently editing config.xml (explicit flag).
- **p0-parser**: extend Optick summary parser (kept in `Tools/` or session files, not as a test harness) to report inclusive time per scope, per thread, per frame call counts, and per-RenderStage inclusive times.
- **p0-scopes**: add missing Optick scopes: per-stage inclusive (`RenderPass::render` per stage), `RenderPassExecutor::doCustomPass` sub-phases (prepareRenderQueues, sort, buildDrawCommands, PrepareGPUBuffers, pre/main/OIT/transparent), `flushCommandBufferInternal` per command type, `CommandBuffer::batch/clean`.
- **p0-baseline**: capture Debug + Release on GL and VK (validation off and on) from the fixed reference camera → `baseline.md` in session files with per-stage and per-command tables plus workload counters (visible nodes per stage, HiZ-culled, draws, active and shadow-casting lights). Also verify that HiZ culling and light culling are actually working; a broken cull is both a bug and a perf issue. Re-run this matrix after every phase.

### Phase 1 — Critical path / scheduling (largest expected Debug win)
- **p1-prototype**: build, run, measure the ready-only scheduler prototype (`RenderStageSchedule.h`, `RenderPassManager`); check worker starvation; keep only if it helps.
- **p1-data-deps-audit**: document every CPU-side cross-stage dependency (light sorting, shadow data, static executor buffers, probe pool, reflector budgets, `s_Nodes`, `PrepareGPUBuffers/FlushBuffersToGPU`) and make shared state per-stage or pre-computed before stage tasks start (e.g. move `sortLightData` / shadow matrix computation into a pre-pass on main/worker before recording).
- **p1-ordered-slots**: pre-reserve ordered per-stage (per-pass) submission slots in the window CommandBufferQueue so stages record in parallel and submission order is preserved. Remove ordering-only edges from Kernel.cpp dependencies. (High risk → Catch2 tests for slot ordering.)
- **p1-split-shadows**: split shadow stage per light/cascade, and reflection/refraction per probe/reflector, into parallel subtasks writing into their own slots.
- **p1-sort-leak**: confirm/fix `RenderQueue::sort` task allocation leak.

### Phase 2 — Command buffer system and main-thread translation
- **p2-cmdbuf-assess**: profile `CommandBuffer::batch()`/`clean()` and command storage (allocations, polymorphic commands, per-command vectors). Decide fix/remove from data.
- **p2-cmdbuf-rewrite**: targeted rewrite of hot command paths: arena/linear-allocated command storage reused across frames, fewer per-command heap allocs, merge-friendly draw representation (multi-draw command carrying a range of indirect records).
- **p2-parallel-translate**: translate per-slot command buffers to API commands in parallel. Vulkan: per-slot secondary command buffers (or separate primaries) recorded on workers with per-thread command pools and state trackers; GL: keep serial translation but cheaper. (High risk.)
- **p2-submit-batching**: Vulkan: single submit per frame with batched barriers (`vkCmdPipelineBarrier2` coalescing in vkTexture/VKWrapper), reuse command buffers, avoid per-frame begin cost.

### Phase 3 — Draw/state batching
- **p3-buffer-writes**: coalesce the ~10k `ShaderBuffer::writeBytes`/`waitForLockedRange` calls into per-frame bulk writes (one range lock per buffer per frame; persistent mapped ring).
- **p3-bindless**: full bindless on both backends behind a simple command-buffer-facing interface (texture/sampler handles stored in material data; one global descriptor set). Vulkan: descriptor indexing, evaluate `VK_EXT_descriptor_buffer`; GL: existing bindless residency, batched `makeTextureViewResident`. Remove per-draw push descriptors for material textures. (High risk → tests for handle allocation/lifetime/deferred free.)
- **p3-geometry-pool**: shared vertex/index pools keyed by vertex layout + index width with suballocation (firstIndex/baseVertex), GPU-completion-safe free via existing deferred deletion; migrate static meshes, then dynamic. Audit `DrawIndex` semantics. (High risk → tests for allocator.)
- **p3-mdi**: once state is uniform (bindless + pooled geometry + per-draw data via indirection buffer instead of push constants), emit one multi-draw-indirect per (pipeline, pool) bucket; adjust sort keys to maximise bucket length.
- **p3-dispatch-audit**: explain the ~1055 compute dispatches per frame (HiZ per pass, culling, blur, grass/foliage, PostFX). Merge or skip redundant ones, e.g. a single-pass HiZ downsample or one shared culling dispatch per stage.
- **p3-feature-costs**: per-feature cost breakdown for terrain, grass/trees (instancing path), water (reflection/refraction passes), sky, PostFX and the MSAA resolve. Cut redundant passes, such as reflection/refraction re-rendering heavy vegetation or terrain at full detail.
- **p3-pipeline-binds**: reduce pipeline binds via 64-bit sort keys (radix sort) and pipeline dedup.
- **p3-vk-display-parity**: Vulkan DISPLAY costs ~24.8 ms GPU vs ~17 ms on OpenGL with identical draw/bin counts. Find the difference (barriers and layout transitions, MSAA resolve, load/store ops, pipeline state, descriptor updates). Use per-pass timestamps first, then RGP if needed.
- **p3-gpu-culling-count**: where HiZ culling already runs on GPU, write compacted indirect args + draw count and use DrawIndirectCount on both backends (removes CPU-side per-draw work).
- **p3-shadow-caching**: cache shadow maps for static casters and unmoved lights. **Staggered updates are allowed:** distant CSM cascades, point/spot shadows of far lights, environment probes and planar/cube reflection/refraction targets may refresh at half rate (or round-robin across frames), with near cascades and the main view kept per-frame. Make the rates configurable per feature (e.g. in config.xml/scene settings) and expose a "force every frame" debug toggle. Use low-detail scene lists for reflection/refraction passes. Make sure staggered targets stay consistent: matrices stored with the map, and no sampling of half-written layers.

### Phase 4 — Debug-build CPU cost
- **p4-debug-profile**: identify Debug-only hot spots (STL debug iterators, non-inlined helpers) from Phase 0 data.
- **p4-containers**: migrate hot-path containers to EASTL/custom containers with custom (arena/frame) allocators: render queues, command buffers, draw lists, visible-node lists.
- **p4-inlining**: enable selective inlining (/Ob1) for engine Debug or force-inline hot helpers; change `_ITERATOR_DEBUG_LEVEL` only if proven necessary and compatible with vcpkg deps.

### Phase 5 — Validation, tests, docs
- **p5-tests**: Catch2 tests for scheduler slots, geometry pool allocator, bindless handle allocator, command buffer changes.
- **p5-acceptance**: harness 300-present acceptance on both backends with validation ON; perf matrix with validation OFF; RGP capture for GPU-side bottlenecks once CPU < 33 ms.
- **p5-docs**: update docs/architecture (rendering, threading, gpu-resources, shader-descriptors), AGENTS.md as needed; decision records for bindless and geometry pooling in docs/decisions/.

## Risks / notes
- Parallel recording + parallel Vulkan translation touch global state trackers → highest risk; gated behind measured wins and tests.
- Bindless changes shader descriptor layouts → shader cache invalidation via AppendToShaderHeader / preprocessing version bump.
- Vulkan buffer, image, view and sampler retirement and shader-reload pipelines
  use transient deferred deletion; pipeline-cache teardown is different. New
  pools need a GPU-completion proof; see [GPU resources](../architecture/gpu-resources.md#vulkan-memory).
- No commits/pushes without explicit request.

## Optional tooling (for user to review/install)
- **Tracy profiler** (BSD): low-overhead CPU+GPU zones, Vulkan/GL GPU timestamps, lock contention and memory views; better than Optick for worker-thread/lock analysis. Could be added as an alternative profiler define.
- **Superluminal** (commercial): sampling profiler, excellent for Debug-build hot spots without instrumentation; ideal for Phase 4.
- **Intel VTune / AMD uProf** (free): sampling CPU profilers with call stacks; uProf fits the 9950X3D.
- **Windows Performance Recorder/Analyzer + PIX timing captures**: CPU sampling, context switches, GPU queue view (PIX supports Vulkan/GL timing only partially; WPA is generic).
- **NVIDIA Nsight** not applicable (AMD GPU); RGP/RGD already installed.

## Status

- [x] p0-harness-perf-mode — `-PerfMode`/`--smokePerfMode` added. Fixed harness bootstrap not waiting for the GUI-subsystem renderer when no debugger is attached (output is now piped). Vulkan WarScene perf run: 200 presents, clean shutdown, ~56 s total vs ~169 s validated under cdb.
- [x] p0-parser — `Tools/Profiling/optick_capture.py` (CPU capture from a running smoke attempt) and `Tools/Profiling/optick_summary.py` (frame stats, per-thread-group self/inclusive tables, per-stage record/submit tables, `--json`). Documented in `docs/architecture/rendering.md` (Performance diagnosis).
- [x] p0-callers — The Optick summary supports repeatable `--callers` filters and reports immediate parent scopes and call counts in text and JSON.
- [x] p0-scopes — most scopes already existed (per-command types, batch/clean, doCustomPass phases). Added `PROFILE_SCOPE_DYNAMIC` plus `Record stage: <STAGE>` (RenderPassManager stage task) and `Submit: <buffer>` (GFXDevice::flushWindow) scopes.
- [x] p0-baseline — Debug/Release perf mode captured on both backends (see Findings). Harness gained `-Configuration Debug|Release` (Release restricted to `-PerfMode`). The backend switch now overrides the configured API in memory for OpenGL too. Workload counters (per-stage draws/bin items, HiZ culled/overflow/skipped) and per-pass GPU times are logged as `[PerfMetrics]`. HiZ culling is active (~1000 items culled, 58 overflow). Clustered light culling is dispatched every pass. Validation-on acceptance was subsequently run as a correctness gate; see the stabilization checkpoint.
- [x] p0-gpu-timers (new) — Frame GPU time on both backends (VK timestamp pool per frame slot; GL fixed `RingBufferSeparateWrite` precedence bug and query ring writing ahead of the read slot). Per-command-buffer GPU timestamps (`writePassTimestamp`/`readPassTimestamps`, `GPUPassTimingTable`) logged as `[PerfMetrics] gpuPasses`. Pipeline-statistics queries are now opt-in (`queryPipelineStatistics`): on AMD GL they cost about 10x the frame time. Editor panel no longer overrides perf-mode stats.
- [x] p1-taskpool-wakeups (new, found during baseline) — `TaskPool::runTask` woke waiters with `notify_one()` on a single CV shared by every waiter (main thread, stage tasks, nested `Parallel_For`), so the right waiter often slept the whole 2 ms timeout. Switched to `notify_all()`. Release VK 121→54 ms, Debug VK 217→131 ms; SHADOW record 62→10 ms (Release).
- [x] p1-prototype — kept; it is now the ready-only recording schedule used by p1-ordered-slots.
- [x] p1-data-deps-audit — Cross-stage audit follow-up implemented: per-view
  immutable light/scene snapshots replace last-view-wins stage uploads, all
  used snapshots receive shadow-slot patches after the stage wait, and cube/
  paraboloid cameras are stage-owned. HiZ dispatches retain per-view uniform
  snapshots instead of referencing one mutable stage uniform block.
  Windows instrumented tests and Vulkan/OpenGL acceptance passed. Same-stage
  recording uses the bounded ownership lanes described below.
- [x] p1-ordered-slots — Stages now declare GPU submission dependencies (`RenderPass::dependencies`) separately from CPU recording dependencies (`recordingDependencies`, which must be a subset). Stage tasks only record. After `pool.wait`, the main thread flushes the stage command buffers in topological submission order (`RenderStageSchedule::topologicalOrder`), then calls `LightPool::patchShadowIndices`, then `FlushBuffersToGPU`. SHADOW is now a GPU-order-only dependency. Release: VK 41.3→31.4 ms, GL 43.6→33.5 ms. Debug VK: 133.8→112 ms. Catch2 test added.
- [x] p1-node-update-bitset experiment — An atomic bitset measured 41.3 ms (within noise) but was reverted: indirection storage can grow beyond `MAX_VISIBLE_NODES`, so the fixed bitset could access out of bounds. The original growable set is retained.
- [x] p1-render-workers-experiment (new) — Raising `RENDER_WORKERS` from 6 to 12 made frames slower. Reverted.
- [x] p1-split-shadows — Implemented and accepted on both backends. Controlled
  serial/parallel Debug measurements are recorded below. Shadow lights/cascades/faces/world AO and
  reflection/refraction probe/reflector views record into independent slots
  through two reusable executor lanes, with one lane on the caller. Generator selection, budgets, light
  uploads and constants stay serial; camera/frustum snapshots, executor scratch,
  counters/uniforms, package LoD and lane-local indirect ranges isolate recording.
  Completed command pointers merge into their original positions without
  changing GPU scratch/barrier order. The smoke runner's `-SerialViewRecording`
  enables a controlled comparison with the same snapshots and slots.
- [x] p1-sort-leak — Confirmed and fixed. `RenderQueue::sort` and `RenderPassExecutor::buildDrawCommands` allocated a parent task they only enqueued if a bin or queue was large. Pooled tasks that are never enqueued keep `_unfinishedJobs == 1`, so each such pass permanently leaked one of the 65,536 slots. `AllocateTask` then probes past more and more dead slots, and eventually spins forever. Both now use `TaskGroup`, which creates the parent only for a queued child and always joins it. Regressions cover more than one ring's worth of empty groups, nested children, repeated waits, reuse and destruction. No frame-time change in the historical short runs (32.1 ms); this is a long-session stability fix, not a new measured speedup.
- [x] p3-vk-sample-shading — Vulkan now matches OpenGL's per-pixel MSAA shading, as the user requested. It previously forced per-sample shading on every MSAA pipeline. VK GPU 31.3→21.8 ms (DISPLAY MainPass 7.2→3.4, W-OIT 7.2→4.5, PrePass 4.3→1.6); GL was 26.6 ms. No obvious reference-camera difference was observed; shader-dependent sample shading can still change. Vulkan Release is now CPU-bound (frame ~32–35 ms).
- [x] p2-cmdbuf-assess — `batch`/`clean` cost about 4–5 ms inclusive in Release and 9 ms in Debug in the earlier capture; a wholesale rewrite is not justified by those measurements alone.
- [ ] p2-cmdbuf-rewrite
- [ ] p2-parallel-translate
- [ ] p2-submit-batching
- [ ] p3-buffer-writes — The uniform upload arena reuses immutable snapshots
  for unchanged data; broader bulk-write work and its measured benefit remain open.
- [x] p3-uniform-lookup-slice — Construction-time member indexing and a validated
  descriptor index hint are implemented and correctness-checked. The first
  Release before/after captures do not establish a performance improvement;
  see the uniform preparation checkpoint below.
- [ ] p3-per-draw-uniforms — About 10k `writeBytes` calls/frame originate in uniform-block commits. A fast push-constant experiment made instanced terrain/vegetation disappear on both backends and was reverted; its root cause remains unconfirmed. Keep the existing per-draw uniforms until an alternative passes both image and performance checks.
- [ ] p3-bindless
- [ ] p3-geometry-pool
- [ ] p3-mdi — Vulkan can batch adjacent compatible, non-instanced indirect
  draws with the same buffers and state. Full pooled-geometry/bindless batching
  and OpenGL equivalents remain planned.
- [ ] p3-dispatch-audit
- [ ] p3-feature-costs
- [ ] p3-pipeline-binds
- [ ] p3-gpu-culling-count
- [ ] p3-shadow-caching
- [x] p3-vk-display-parity — Matching OpenGL's shading frequency removes much of the GPU difference; in the measured captures, VK DISPLAY GPU was at or below GL.
- [x] p0-gpu-scope-timings (new) - opt-in per-debug-scope GPU timestamps (`-GpuScopeTimings`); see 'GPU scope breakdown' in Findings.
- [x] p3-woit-cost (new) - Resolved as a misattribution: W-OIT scopes are expensive because they contain the tree geometry, not because of OIT itself (see 'Feature costs' in Findings).
- [x] p3-tree-instance-compaction (new) - Trees: the cull compacts visible instances per species into `PER_PASS` 14 and writes per-species counts to `PER_PASS` 15. A patch compute (`instanceCountPatch.glsl`) clamps indirect `instanceCount` before drawing. Release VK 53→41 ms (GPU ~33), GL 47.5→43.6 ms (GPU 49→33). See "Tree compaction results". Grass is split into p3-grass-compaction.
- [x] p3-grass-compaction (2026-11-08) — Implementation and
  engine/shader regressions are complete. Grass now uses immutable source data,
  a compacted draw buffer, and a GPU-written visible count patched into indirect
  draws on both APIs. Preserve distance fade and original colour variation.
  Supervised Vulkan/OpenGL runs and scene/editor image inspection are complete.
  Before/after performance measurement remains pending; no frame-time gain is claimed.
- [ ] p3-tree-lod (new) - Distance LoD/impostors for instanced trees; reflection/refraction draw distance as a quality setting (default = current visuals).
- [ ] p3-tree-shadow-cost (new) - Reduce tree cost in SHADOW (per-cascade compaction, distance cut-off for far cascades).
- [x] p3-gl-hiz-bug (new) - Root cause: OpenGL buffer binding aliasing (see "Tree compaction results"). GL now culls 1002 items like VK. Uniform and storage bindings have separate namespaces, while storage bindings are unique across stages; the OpenGL flat-binding limit is not applied to Vulkan's per-stage limit.
- [ ] p4-debug-profile
- [ ] p4-containers
- [ ] p4-inlining
- [ ] p5-tests — Existing scheduler and renderer regressions are covered; add
  tests for any remaining new engine functionality.
- [ ] p5-acceptance — Both backends passed supervised 301-present acceptance
  runs, but the performance goals and acceptance of future changes remain open.
- [ ] p5-docs — Current implementation is documented; update the affected guides
  alongside any further changes.

### Phase 1 ownership audit follow-up

The coarse stage scheduler and GPU submission order are unchanged. The ownership
audit repairs data lifetime, and the bounded view split extends CPU concurrency
without changing generator policy or GPU execution order.

| State / source | Current ownership and ordering |
|---|---|
| `LightPool::sortLightData`, `uploadLightData` | One recording worker owns each stage's sorted list. Every view acquires a separate reusable CPU/GPU snapshot; later views cannot overwrite earlier camera-space lighting. Buffers grow geometrically with the light count and reuse their high-water capacity. |
| Shadow selection, `Light::shadowPropertyIndex`, shadow buffer | SHADOW selects lights/layers and assigns slots serially. Other stages initially upload `-1`; the main thread patches every used view after the stage wait, before translation. |
| Cube/paraboloid utility cameras | `Camera::InitPool` creates separate mutable cameras per stage. Serial view selection captures snapshots and frusta before generator reuse; lane workers do not mutate these cameras. |
| HiZ push-constant uniforms | `RenderPassExecutor` retains one `UniformDataSnapshots` entry per dispatch until translation. Entity counts and frusta cannot be overwritten by later views or players in the same frame. The helper is shared with immediate primitives. |
| Shared executor node/material/transform buffers | `PrepareGPUBuffers` runs before stage launch; processed-node, free-list and dirty-range locks protect partitioned preparation. GPU upload follows the stage wait. Retain growable storage rather than the reverted fixed node bitset. |
| Environment probes, reflector budgets and `s_Nodes` | Probe selection and reflection/refraction budgets remain serial within their owning stage; each stage's enumeration scratch is separate. Texture selections are published under the rendering-component package lock. DISPLAY/NODE_PREVIEW wait for their CPU producers. |
| Executor queues/pages, pass counters/uniforms, component LoD/preparation, generator constants and lighting-grid caches | Two reusable executor lanes own recording scratch and lane-local indirect ranges. Full package identities own LoD/preparation state and retain lane affinity. Generators and light uploads remain serial; independent lighting commands rebuild their own grid inputs. Completed slots transfer into the original GPU order after the join. |
| Scene/camera/resource lifetime | Scene updates and shared frame preparation precede stage launch; stage tasks finish before scene teardown or frame upload. GPU completion remains separate from task completion and recording order. |

`RenderQueue::sort` and transform/material preparation now share `TaskGroup`:
no parent allocation for an empty batch, and every allocated parent is submitted
and joined. This hardens the existing leak fix; it is not a new speedup claim.
Windows/MSVC Debug editor with Optick/function profiling passed 383/383 CTest
cases, including seven new task, view-light, camera and HiZ regressions.
Sequential supervised Vulkan and OpenGL WarScene runs both completed 301 scene
presents, scene/editor captures and clean shutdown. Both image pairs were
inspected: terrain, foreground grass, trees, water and Sponza remain present,
without the earlier black transparency speckling. These are correctness runs;
no before/after frame-time gain or Linux/macOS result is established here.

The view split retains high-water storage and defers shadow dirty-flag updates
until its pending-caster feedback is reduced. Catch2 covers lane affinity,
same-lane order, nested-task progress, reuse, serial equivalence, command-slot
ownership/order and package LoD isolation. The final Windows/MSVC instrumented
build and suite pass 387/387, including caller participation, contiguous light
snapshot reuse and uniform-address stability across chunk growth. Final
Vulkan/OpenGL runs after the allocation cleanup each completed 301 scene
presents, both captures and normal shutdown. All four images were inspected:
the saved exterior/back-corner Sponza view, terrain, grass, trees and water
remain present, without new black transparency speckling. Vulkan retained
only the approved custom-border-color advisory. No Linux/macOS execution or
new Release measurement was performed.

### Phase 1 view-split measurements

Debug instrumented editor, validation off, 600-present supervised runs, 64
Optick CPU frames beginning after present 160. The serial diagnostic uses the
same snapshots/slots and preserves original view order. The current saved
camera looks at Sponza's exterior/back corner; these measurements are not
directly comparable to the historical front/arches camera or Release results.

| Backend / recording | CPU median ms | CPU p95 ms | SHADOW record ms | REFLECTION record ms | REFRACTION record ms |
|---|---:|---:|---:|---:|---:|
| Vulkan serial control, repeated | 123.42 | 130.23 | 31.66 | 16.70 | 17.59 |
| Vulkan two lanes | 123.64 | 130.19 | 27.41 | 16.64 | 17.99 |
| OpenGL serial control | 129.77 | 161.92 | 33.21 | 17.50 | 18.49 |
| OpenGL two lanes | 129.70 | 149.50 | 29.42 | 17.90 | 18.47 |

Shadow recording shortened by about 12-13%, but total CPU median frame time
is effectively unchanged. The noisy OpenGL tail improvement is not an
established speedup. Four queued lanes regressed Vulkan to 142.24 ms; caller
participation reduced that to 126.35 ms, and limiting the batch to two lanes
removed the remaining median regression. Keep the two-lane bound rather than
assuming more worker tasks help an already partitioned renderer.

At warmed frame 192 the compared runs retain stage draws/bin items:
DISPLAY 1679/1063, REFLECTION 1356/904, REFRACTION 1670/1061,
SHADOW 2899/817, with HiZ culled 1002, overflow 11, skipped 3.
Vulkan API translation remains about 71 ms inclusive per frame, with about
7090 resource binds and 6550 per-draw descriptor scope entries, not necessarily
actual Vulkan push calls. Shorter shadow work
does not remove the reflection/refraction-plus-display critical path or serial
translation. Phase 1 implementation is complete; the Debug <=33 ms and
Release <16.6 ms goals are not met.

These captures precede the final value-storage cleanup, not its performance
baseline. That cleanup removes per-view heap owners, pre-sizes bounded view
records, creates executor lanes at registration, retains barrier-list capacity,
and replaces per-snapshot heap owners with stable-address values. GPU light
buffers/indirect pages and unbounded immediate-draw payloads still have
documented first-use/high-water growth; the whole renderer is not allocation-free.
Final post-cleanup correctness runs passed as described above.

During the validation/debugger-enabled correctness runs the user observed
CPU/GPU below 10% and VRAM around one third on Vulkan, versus about 60% GPU
and 9/16 GB VRAM on OpenGL. These are user-observed Task Manager readings,
not synchronized performance captures. They motivate continued serialization/
driver analysis, but do not establish core occupancy, GPU-engine utilization,
memory-bandwidth pressure or a validation-free backend throughput comparison.

### Reusable bindings: instrumentation, ownership and compiled operations

The first three binding-proposal steps are implemented without changing the
native descriptor model. Materials publish immutable, retained texture/sampler
revisions by pass category, common-input requirement and visibility. Render
packages keep reflection/refraction and object-buffer overrides separate.
Queued commands retain old revisions through edits; replay retains owners per
merged slot, including slots omitted by later partial commands. Publication
uses the existing memory pool and allocator-backed shared control blocks;
steady command copies do not allocate new records. First use and edits can.
See [binding ownership](../architecture/shader-descriptors.md#immutable-material-binding-revisions).

Step 3 adds bounded direct-mapped caches for stable sampled-image operations.
Vulkan reuses resolved combined-image-sampler values but preserves per-slot
comparison, push writes, layout invalidation and command-buffer/bind-point
tracking. OpenGL reuses the flat usage/slot mapping and sampler handle, while
still touching each texture view and queuing every bind through the state
tracker. Fallback and mutable override slots remain uncached. Cache entries
hold no resource ownership and are cleared at backend teardown; cache
collisions are misses. Hit/miss and fixed-storage counters are included in
opt-in binding diagnostics.

Opt-in diagnostics distinguish logical commands, material/program pairs, full
effective tuples, uniform changes, fallback work, backend resolutions and
actual Vulkan push calls/writes. OpenGL visit counts are not native API call
counts. The collision-checked tuple collector is bounded and reports overflow,
storage growth and its own CPU scope; it is not free measurement overhead.
Record publication/live counts diagnose binding churn, not all engine allocations.

Runtime acceptance exposed previously retained commands in reusable command
buffers: pool deallocation/reset only invalidated handles, and pool destruction
did not destroy constructed buffers. Retained binding owners made this leave
textures alive at Vulkan allocator teardown. Clearing commands at release/reset
and destroying pool objects fixes that lifetime boundary without discarding
reusable buffer capacity. Unit coverage includes these release boundaries,
material invalidation/no-op reuse, actual texture lifetime/stale generations,
partial-slot ownership, command semantics and bounded diagnostic collisions.
The instrumented Windows suite passed 393/393. Sequential validated Vulkan and
OpenGL Games/WarScene attempts then completed 301 scene presents, scene/editor
captures and normal shutdown; all four images were inspected.

#### Binding measurements

Separate 600-present Debug instrumented perf runs completed on the same RX
9070 XT, Games/WarScene and saved exterior camera. Validation was off; each
Optick CPU capture spans 64 warmed frames after present 160. The table contains
median CPU wall times per frame. Binding subscopes are nested, not additive.
These runs include the new instrumentation and tuple collector; they do not
establish a speedup or a like-for-like regression against the earlier captures.

| CPU scope, ms/frame | Vulkan | OpenGL |
|---|---:|---:|
| Whole frame median / p95 | 134.22 / 141.28 | 137.82 / 147.51 |
| API translation, inclusive | 77.65 | 83.58 |
| Backend binding path, inclusive | 45.92 | 47.47 |
| Logical binding command merge | 2.64 | 2.60 |
| Uniform commit | 12.95 | 16.77 |
| Fallback resolution | 4.50 | 6.11 |
| Sampled-image resolution | 5.47 | 2.73 |
| Buffer resolution/binding | 2.90 | 14.21 |
| Effective-tuple measurement itself | 2.85 | 3.45 |
| Actual Vulkan push emission | 1.09 | N/A |

The snapshot logged at frame 192 describes the previous completed CPU frame:

| Binding counter | Vulkan | OpenGL |
|---|---:|---:|
| Logical commands / material references | 2447 / 1397 | 2434 / 1384 |
| Uniform commits / changed | 7097 / 6115 | 8686 / 6115 |
| Active per-draw tuple observations | 6574 | 7690 |
| Unique effective tuples | 6291 | 6291 |
| Unique material-revision/program pairs | 569 | 569 |
| Actual push calls / descriptor writes | 6550 / 12143 | N/A |
| Image / buffer resolution visits | 11846 / 21541 | 8832 / 20517 |
| Tuple / material-table overflow | 0 / 0 | 0 / 0 |

The first 4096-tuple probe overflowed, so the diagnostic bound was raised to
8192. Its retained storage is 13,074,432 bytes (12.47 MiB), only allocated for
explicit perf sampling; both final runs report zero warmed storage growth.
Published/live records stay at 1907 from the frame-64 through frame-576
snapshots on both APIs, with 3,096,968 live payload bytes (2.95 MiB), excluding
pool slack/control blocks. This demonstrates stable binding publication for
this workload, not allocation freedom throughout the engine.

The measurements favor caching stable material resource resolution separately
from dynamic uniform/view data, not caching whole effective sets as though
material identity made them reusable. Vulkan writes 10,220 buffer descriptors
out of 12,143 total at that snapshot, and actual push emission costs only about
1.09 ms/frame. Eliminating native push calls alone cannot recover the inclusive
binding-path time. Uniform preparation, fallback/resource resolution and the
remaining serial translation still need targeted work; these three steps are a
measured ownership foundation, not achievement of the frame-time targets.

#### Compiled-cache runtime check

After Step 3, validation-free Debug `-PerfMode` diagnostics completed 601
presents each on the same RX 9070 XT and Games/WarScene. The nine samples below
are per-frame snapshots logged at frames 64 through 576. Stage draw/bin counts
matched between APIs; backend draw counts did not. Cache hit rates are
`hits / (hits + misses)` averaged across those nine snapshots.

| Counter / observation | Vulkan | OpenGL |
|---|---:|---:|
| Compiled sampled-image cache hits / misses per sampled frame | 9,756 / 320 | 5,299 / 568 |
| Hit rate | 96.83% | 90.33% |
| Fixed compiled-cache storage | 192 KiB | 96 KiB |
| Image resolutions per sampled frame | 2,091 | 3,585 |
| Backend binding counters per sampled frame | 6,550 push calls / 12,132 writes | 8,884 texture visits / 20,539 buffer visits |
| Backend draw counter per sampled frame | 6,074 | 7,726 |
| GPU frame time, sample mean (range) | 33.26 ms (32.75–33.91) | 61.15 ms (60.30–62.59) |

OpenGL texture visits include the required texture-view touch on every cached
use; they are not native API call counts. These single-run timings characterize
this workload, not a before/after comparison. No frame-time improvement is
attributed to Step 3 from these samples alone; see the matched comparison below.

#### Warmed CPU profile

Fresh CPU-only Optick captures were collected during matching validation-free
Debug `-PerfMode` WarScene runs on the same RX 9070 XT. Each captured 64 frames
after present 160; both diagnostics completed 600 presents with clean shutdown,
and the internal suite passed (164 cases, 28,214 assertions). Times are
per-frame medians except the p95 row; nested scopes are not additive.

| CPU scope, ms/frame | Vulkan | OpenGL |
|---|---:|---:|
| Whole-frame wall time, median / p95 | 127.54 / 134.69 | 137.59 / 184.34 |
| `GFXDevice::flushCommandBufferInternal`, inclusive | 73.83 | 81.85 |
| `bindShaderResources`, inclusive | 42.51 | 46.51 |
| `Commit binding uniforms`, inclusive | 12.51 | 16.53 |
| Main-thread `TaskPool::wait`, self time | 28.53 | 30.37 |

These are single captures with profiling and binding diagnostics enabled, not
a matched cache-disabled baseline. The `TaskPool::wait` scope is substantial,
so wall-frame time should not be read as CPU work time; repeat captures and
worker/task-wait analysis are needed before attributing a change to the cache.

#### Matched cache-on/off comparison (2026-11-09)

Three paired runs per backend now compare the compiled sampled-image cache
against `-DisableBindingCache`. All 12 retained diagnostics used the same
instrumented Windows/MSVC Debug executable (SHA-256 recorded in the local
protocol and each attempt), RX 9070 XT, Games/WarScene, saved exterior camera,
settings and diagnostics. Each completed 600 presents and clean shutdown.
CPU-only captures contain 64 frames, requested after present 160 (actual client
connection at 160-164). Pair order alternated on/off, off/on, on/off.
An initial Vulkan pair was excluded and rerun in full because CTest overlapped
its first capture. The 394-test instrumented suite passed before the retained
captures; no tests/builds/summary processing ran concurrently with them.

The table gives the median of the three per-run CPU medians, in ms/frame.
Nested scopes are not additive. Ranges describe the three run medians, not
individual frames or confidence intervals.

| CPU scope | Vulkan off | Vulkan on | OpenGL off | OpenGL on |
|---|---:|---:|---:|---:|
| Whole frame | 129.69 | 126.76 | 134.86 | 133.89 |
| Whole-frame range | 129.54-130.03 | 126.52-127.15 | 134.50-135.69 | 133.39-133.99 |
| API translation, inclusive | 76.36 | 73.51 | 81.27 | 80.59 |
| Backend binding path, inclusive | 45.28 | 42.22 | 46.76 | 45.80 |
| Sampled-image resolution, inclusive | 5.41 | 2.57 | 3.30 | 2.17 |

Within-pair whole-frame reductions were 2.54/3.02/3.27 ms for Vulkan and
1.47/1.70/0.61 ms for OpenGL. The median paired sampled-image-scope reduction
was 2.84 ms and 1.12 ms respectively. All three pairs favor the cache on each
backend, but the OpenGL whole-frame effect is small and variable. Comparing
the aggregate medians gives about 2.3% Vulkan and 0.7% OpenGL wall-frame
reduction in this instrumented workload, not a Release or general engine
speedup. The binding path and remaining serial translation are still expensive.

The nine warmed counter snapshots per run retain identical stage draws/bin
items: DISPLAY 1679/1063, REFLECTION 1356/904, REFRACTION 1670/1061,
SHADOW 2902/817 and NODE_PREVIEW 0/0. HiZ is 1002 culled, 11 overflow and
3 skipped. Disabled runs report zero compiled hits/misses; image-resolution
visits fall from 11,846 to about 2,090 on Vulkan and 8,881 to about 3,570 on
OpenGL when enabled (medians of per-run snapshot means). Tuple-table overflows
and warmed diagnostic-storage growth are zero. Representative on/off scene
images from both APIs were inspected and retain the same camera and geometry.
GPU snapshot means remain about 34 ms Vulkan and 62 ms OpenGL; no GPU
improvement is established by this CPU-side cache.

Raw captures, run metadata, logs, summaries and `comparison.json` are retained
outside the checkout at `H:\RendererBindingAB20261109`. This measurement
isolates Step 3's compiled cache, not the combined effect of Steps 1-3.

#### Capture-completion freeze

The multi-second pause near frame/present 231 is an Optick export stall, not
a periodic binding-cache stall. Across the 12 retained runs, the supervisor
observes roughly 19-22 seconds without new presents near 232-240, in both
cache modes and APIs. The 64-frame captures are about 1.5-1.85 GiB.
`Application::step` calls `PROFILE_FRAME`; Optick's frame macro calls
`Update`, whose state transition invokes `Core::DumpFrames` synchronously.
Thread-event serialization and capture transmission therefore block the main
loop after recording stops. The capture helper also retains the received
payload until writing the file; that is profiling-tool memory, not cache memory.

Two same-binary Vulkan controls confirm the attribution: without a capture
client, the largest supervisor present-report interval was 0.55 seconds.
Moving the capture request from present 160 to 80 moved the 18.54-second
stall to presents 154-155. Both controls completed 601 presents and clean
shutdown. This is not a fixed engine FrameIndex trigger. The export pause
occurs outside the recorded CPU frames and is not included in the comparison
table; it must not be interpreted as measured steady-frame rendering cost.
No asynchronous Optick exporter or third-party profiler change was introduced.

Ordinary Vulkan set 3/dynamic offsets, shader set changes and bindless resources
remain outside these three steps. Step 3 does not eliminate Vulkan push
descriptor calls or OpenGL state-tracker binds. The measured improvement is
limited to the matched validation-free Debug workload above; hit rates alone
would not establish it. No Release, Linux or macOS result is established by
this slice.

### Uniform preparation checkpoint (2026-11-09)

This narrow follow-up leaves shader layouts, per-draw uniform commands, arena
snapshots, retirement and native descriptor emission unchanged.
`UniformBlockUploader` stable-sorts its existing reflected-member vector once
at construction, replacing a full member scan per uniform with lower-bound
lookup plus all equal hashes. A one-byte binding-index hint avoids rescanning
an unchanged descriptor set, but validates the current count and target slot
before use and retains the complete effective-binding comparison. Neither
lookup allocates in the render loop. Tests access the private CPU helpers
through the existing attorney pattern.

Release diagnostics before and after the change each completed 601 presents
on the same GPU, Games/WarScene and reference camera, with validation off.
Each CPU sample is one warmed 64-frame Optick capture requested after present
160; attachment occurred at 177 in the Vulkan baseline and at 162-166 in the
other runs. Artifacts are under `H:\RendererUniformSlice20261109`, in
`baseline-vulkan`, `baseline-opengl`, `after-vulkan` and `after-opengl`.

| CPU scope, median ms/frame | Vulkan before | Vulkan after | OpenGL before | OpenGL after |
|---|---:|---:|---:|---:|
| Whole frame | 45.35 | 41.72 | 48.99 | 48.88 |
| API translation, inclusive | 21.736 | 19.551 | 22.166 | 22.249 |
| Binding path, inclusive | 11.182 | 10.251 | 11.830 | 11.900 |
| `ShaderProgram::uploadUniformData` | 1.677 | 1.421 | 1.367 | 1.448 |
| Uniform commit, inclusive | 3.878 | 3.462 | 4.417 | 4.448 |
| Main `TaskPool::wait`, self | 16.772 | 15.937 | 14.984 | 15.069 |

These are exploratory observations, **not an established speedup**. There is
only one before/after sample per API. OpenGL is essentially unchanged and its
uniform scopes are slightly higher. Vulkan's capture has fewer writes/binds
(about 9814/7094 before versus 9765/7062 after), and time-dependent shadow
refresh changes the sampled workload: at present 192, SHADOW was 819/817
before versus 2902/817 after; both reached 2902/817 at 256. Other stage
draw/bin counts matched: DISPLAY 1679/1063, REFLECTION 1356/904, REFRACTION
1670/1061; Hi-Z was 1002 culled / 11 overflow / 3 skipped. Do not attribute
the whole-frame Vulkan reduction to lookup changes or extrapolate it to
Debug. Repeated matched comparisons are needed before crediting this slice
with a performance gain or expanding it; larger dynamic-uniform preparation
and native binding costs remain open.

Windows/MSVC Debug and Release engine/test builds completed. Focused uniform
tests passed 26/26, including partial/array writes, duplicate and absent hashes,
unchanged bytes, and descriptor replacement/reordering. Full Debug instrumented
CTest passed 396/396. Separate validation-on Debug acceptance runs for Vulkan
and OpenGL each completed 301 presents and normal shutdown; the scene and
editor captures were inspected with terrain, building, trees, grass and water
present. These checks do not establish other-platform behavior or a frame-time
target.

### Shadow-view investigation (2026-11-09)

The aggregate `SHADOW/MainPass` hid a significant distinction: a Release Vulkan
run with per-view labels showed the always-rendered world-AO view consuming
about 2.19 ms of GPU time, more than any individual directional cascade.
The three CSM views averaged about 0.52, 1.37 and 0.88 ms, respectively,
as medians of seven warmed 64-frame block means. World-AO outer-scope work
(copy/blur/mips and other commands) was about 0.23 ms separately.
Artifacts: `H:\RendererShadowSlice20261109\baseline-detail`.

The run completed 601 presents and clean shutdown without an Optick capture
client. Warmed GPU scope sums had a median block mean of 20.16 ms; the log
interval from frame 192 to 576 averaged 24.41 ms/frame. Full CSM refreshes
occurred with an unchanged camera when ordinary scene movement was pending;
other sampled frames reused all directional layers. World AO was still
rendered on those reuse frames.

A conservative light-space sphere/cascade intersection experiment did not
reduce the sampled refresh masks: ordinary movement still reached every
active cascade. Its small timing difference is not evidence of improvement.
The experiment and its temporary tests were removed. Bounded per-view
scope labels and sparse refresh-reason diagnostics remain.

The user's clarified policy is that World AO identifies interiors, should
rarely update, and defaults to excluding terrain, vegetation, skinned geometry
and translucent materials. Per-instance Automatic/Include/Exclude controls reuse
the generic draw mask, so category defaults are overridable without changing
other instances or passes. The follow-on
implementation retains its 1000-by-1000 coverage image and original matrix,
recentering only after 250 units of horizontal camera movement. Eligible rigid
geometry changes, material coverage changes, visibility, pending shaders,
scene/height/filter changes and resource recreation trigger refreshes. Neither
default-excluded vegetation animation nor skinned movement invalidates the image;
explicitly included geometry does. Participation changes also erase old coverage.
No timed
refresh or stale-image interval is used. See the world-AO contract in
`docs/architecture/rendering.md` for ownership and invalidation routes.
Performance and runtime acceptance for this follow-on must be measured
separately from the baseline above; the discarded experiment is not evidence
for it.

The settled per-instance-control slice passed Windows/MSVC instrumented Debug
CTest (405/405) and the Release engine build. Normal validation-on WarScene
acceptance completed 301 presents and normal shutdown on both Vulkan and OpenGL,
with scene/editor images inspected. Known startup foliage shader-readiness
messages, the existing OpenGL context fallback and shutdown networking messages
remain; these are not claims of completely error-free logs.

The final 600-present Vulkan Release diagnostic retained revision 17962 from
frame 64 through 576, with no pending casters or sampled refreshes. All seven
warmed GPU timing blocks ending at frames 192 through 576 omitted both World AO
main and outer scopes. Their median reported GPU-scope sum was 18.12 ms; the log
cadence over that interval was 22.58 ms/frame, not an Optick CPU-frame median.
This confirms full warmed AO-view reuse with the controls enabled, not a fresh
matched A/B frame-time speedup. The WORLD_AO debug capture showed building
coverage with exterior terrain and the standalone imported tree unoccluded.
Artifacts are under `H:\RendererWorldAO20261109` in
`vulkan-overrides-final`, `opengl-overrides-final`,
`vulkan-overrides-release-profile` and `vulkan-overrides-ao-view`.

### Per-node preparation ownership checkpoint (2026-11-09)

- [x] Replace the shared mutex/fixed-tree processed-node sets with two
  component-owned atomic stamps, retaining separate transform/material claims.
- [x] Advance the epoch once per player pass before any view recording; retain
  the join-before-upload boundary. No index-sized bitmap, frame allocation,
  global lookup lock or GPU layout change is introduced.
- [x] Cover concurrent claims, independent types/components, successive player
  passes, more than `MAX_VISIBLE_NODES` components and fresh/recycled-slot
  state. The instrumented Windows Debug suite passes 408/408.
- [x] Build Debug and Release engine/test targets. Normal validation-on Vulkan
  and OpenGL acceptance each completed 301 presents and normal shutdown;
  scene/editor images retain building, terrain, vegetation, water and shadows.
  Existing startup shader-readiness, STUBBED, OpenGL context-fallback and
  shutdown networking messages remain; logs are not entirely error-free.

The Release Vulkan comparison uses one warmed 64-frame Optick CPU capture
before and after, requested after present 160 (actual attachment at 175 and
160), with detailed GPU scopes enabled. Each run completed 601 presents.
Times below are reported per-frame scope medians; worker aggregates are not
main-thread critical-path time and must not be added to wall-frame time.

| CPU scope | Before | After |
|---|---:|---:|
| Transform duplicate lookup, aggregate worker time | 3.24 ms | combined below |
| Material duplicate lookup, aggregate worker time | 2.46 ms | combined below |
| Combined duplicate-claim scope | sum above: 5.70 ms | 0.37 ms |
| Record SHADOW, inclusive | 9.82 ms | 9.13 ms |
| Record REFRACTION, inclusive | 9.03 ms | 8.52 ms |
| Record REFLECTION, inclusive | 8.70 ms | 8.17 ms |
| Whole CPU frame median / p95 | 37.58 / 40.00 ms | 37.21 / 40.01 ms |

This removes a measured shared lookup cost, but the small whole-frame
difference from a single pair does not establish an overall speedup.
Serial translation remains about 17.4-17.7 ms in these captures.

Two subsequent capture-free Release Vulkan runs completed 601 presents each.
Their frame-192-to-576 log cadences were 20.69 and 20.52 ms/frame; the medians
of seven warmed GPU-scope block means were 17.32 and 17.29 ms. The previous
World-AO capture-free run used the identical baseline executable hash and
reported 22.57 ms/frame and 18.12 ms of GPU scopes. This is not a repeated,
alternating matched A/B: time-dependent shadow refresh, GPU scheduling and
instrumentation variation prevent crediting those differences to CPU stamps.
The CPU-captured runs include the Optick export pause outside their captured
frames and must not be used for ordinary rendering cadence.

All warmed snapshots retain DISPLAY 1679/1063, REFLECTION 1356/904,
REFRACTION 1670/1061 and Hi-Z 1002 culled / 11 overflow / 3 skipped.
World AO remains reused without sampled pending casters or refreshes.
DISPLAY W-OIT remains the largest individual warmed GPU scope at about
4.4 ms, followed by its main pass at 3.4-3.5 ms and prepass at 1.6 ms.
Neither performance target is established. The next GPU investigation should
separate W-OIT accumulation from composition and identify costly draws without
silently reducing transparency, MSAA or vegetation quality.

Artifacts: `H:\RendererNodePrep20261109` (`baseline-vulkan`, `after-vulkan`,
`after-vulkan-no-client-1`, `after-vulkan-no-client-2`,
`acceptance-vulkan`, `acceptance-opengl`). The baseline/after renderer hashes
are recorded in each attempt's `environment.json`; raw CPU captures and
summaries are alongside their attempts.

### Stabilization checkpoint

The per-draw push-constant experiment and fixed-size node-update bitset were
reverted. After the OpenGL binding-namespace adjustment, final 600-present
Release diagnostics on the same GPU and reference camera completed with the
terrain, forest, grass, building, water and shadows present. Captures used API
validation off and GPU scope timings on; each CPU result is a 64-frame Optick
median after roughly 160 presents:

| Backend | CPU median / p95 | GPU scope sum | HiZ culled / overflow / skipped |
|---|---:|---:|---:|
| Vulkan | 34.35 / 41.36 ms | 20.43 ms | 1002 / 58 / 3 |
| OpenGL | 34.58 / 38.00 ms | 25.05 ms | 1002 / 58 / 3 |

GPU scope sums are 64-frame averages of timed queued-command scopes, not full
GPU-frame times. These are individual captures, not a statistical comparison
with the earlier 31.4/33.5 ms results; run-to-run and instrumentation variation
applies. Before the final binding adjustment, CPU median/p95 was 34.66/37.51 ms
for Vulkan and 38.31/42.89 ms for OpenGL.
The final Windows/MSVC Debug build and full internal CTest preset passed
365/365, including MSAA defaults, binding namespaces and instance-count patch
shader coverage.
Validation-on WarScene acceptance completed 301 presents and normal shutdown
on OpenGL (attempt 127) and Vulkan (attempt 270). Their scene and editor
captures retained the same reference geometry, vegetation, water and shadows.
Vulkan reported the approved custom-border best-practice advisories; the logs
also retain known `STUBBED` messages and shutdown networking disconnects.
Neither performance target has been met.

### Grass compaction implementation checkpoint (2026-11-08)

- [x] Reuse the tree compaction loop for grass, one workgroup per chunk, with
  separate source/draw/count buffers and existing descriptor slots 14/15.
- [x] Recompact the visible prefix for Hi-Z and patch indirect instance counts
  with the existing backend-neutral mechanism. Keep negative tail sentinels
  so a dropped count patch still renders correctly.
- [x] Preserve grass distance fade, LoD and original modulo-3/modulo-4 colour
  variation after reordering; leave grass excluded from shadow passes.
- [x] Add shared metadata arithmetic coverage and compile/reflection coverage
  for both APIs; the Windows instrumented suite passes 374/374.
- [x] Update rendering and shader-descriptor guides.
- [x] Vulkan WarScene acceptance: 301 presents, normal shutdown, scene/editor
  images inspected (`grass-compact-vulkan`, attempt 01). Existing grass remains
  visible along the forest edge; the pre-existing bare foreground is unchanged.
- [x] OpenGL WarScene acceptance: presentation/capture contract, normal shutdown
  and scene/editor image inspection complete (`grass-compact-opengl`, attempt 01).
  Grass coverage matches Vulkan in these captures; performance is not inferred
  from the supervised runs.
- [ ] Measure current-branch before/after frame and culling GPU times. The
  historical ~4 ms grass filter cost is not a predicted compaction saving.
- [x] Foreground grass coverage fix (2026-11-08): regression reproduced and
  corrected DDS CPU inspection orientation. DevIL flips compressed upload data
  but not its decoded pixels; cached placement sampled the opposite terrain
  rows. Corrected decoded pixels and invalidated grass/tree placement caches.
  Main terrain now selects grass-only material channels (Alpha1 red/green,
  Alpha2 alpha), preserving slope/water limits. Targeted regressions pass.
  Both APIs show corrected foreground coverage, also confirmed by the user.
  Denser canopies exposed black transparency artifacts; rejecting zero-alpha
  texels before lighting removed the speckling in the loaded OpenGL capture.
  FP32 accumulation did not help and was reverted. Final Vulkan and OpenGL
  scene/editor captures retain full vegetation (1063 render-bin items) without
  the black speckling (`grass-coverage-fixed-vulkan` and
  `grass-coverage-fixed-opengl`, attempt 01), with normal shutdown.
  The instrumented Windows suite passes 376/376, including coverage arithmetic,
  DDS orientation, grass channels and shader rejection-before-lighting checks.
  The exact originating non-finite lighting operation was not captured.

### Sponza interior normal-cache correction (2026-11-08)

- [x] Inspect hanging foliage and arches from inside Sponza on both APIs.
  Foliage is present with cut-out leaves, without the earlier black speckling.
- [x] Correct cached normal-map encoding: BC3n stored X in alpha and Y in
  green, while shaders decoded RGB. NVTT also normalized biased `[0,1]`
  colours instead of signed normal vectors. Write RGB BC3, expand/normalize/
  repack each level. Keep ordinary cache filenames and remove old local normal
  caches rather than versioning them.
- [x] Reproduce the wrong decoded direction in Catch2; verify cold and warm
  loads. Windows instrumentation passes
  376/376.
- [x] Inspect corrected Vulkan normals and final Vulkan/OpenGL scene/editor
  captures (`sponza-interior-normals-fixed`, `sponza-interior-vulkan-fixed`,
  `sponza-interior-opengl-fixed`, attempt 01). The erroneous split on the
  brick wall disappears, and arch/column lighting no longer has the previous
  normal-map-induced discontinuities. Both full runs shut down normally.
  Restore the original player camera after the interior diagnostics.

One preceding Vulkan diagnostic exited during teardown with Windows heap
corruption `0xc0000374`. The next, longer run shut down cleanly after restoring
the growable node set, but the crash's root cause was not established. If it
recurs, capture a debugger stack and investigate; do not infer a GPU loss or
claim the set change proved the cause.

At that checkpoint, the restored `NodeNeedsUpdate` set remained an optimization target:
the OpenGL capture attributes approximately 3.6 ms and 2.9 ms of aggregate
worker time per frame to its transform and material lookups, respectively.
Any replacement must handle indirection indices beyond `MAX_VISIBLE_NODES`,
avoid render-loop allocation churn, and include concurrent and overflow tests.
The later component-owned preparation stamps above replace this set without
an indirection-index capacity bound.

## Findings (perf mode, Vulkan, WarScene, CPU frame median)

The following sections record successive historical captures and experiments.
Their observations and proposed next steps describe the measured state at the
time, not necessarily the current implementation; see the status above and the
[renderer handoff](../../renderer-performance-handoff.md) for the later checkpoint.

Captured with `Tools/Profiling` and the harness `-PerfMode` run (validation off, no debugger); 32-64 frames after present 150.

| Build | Change | Frame ms (median/p95) | Main `TaskPool::wait` | Main translate+submit | Record SHADOW/REFR/REFL/DISPLAY |
|---|---|---|---|---|---|
| Debug | baseline (validated + cdb, old) | 517 | | | |
| Debug | perf mode | 217 / 231 | 131 | 66 | 81 / 21 / 17.5 / 31 |
| Debug | + notify_all | 131 / 134 | 46 | 66 | 22 / 13 / 12 / 12 |
| Release | perf mode | 121 / 136 | 97 | 15 | 62 / 11 / 10 / 22 |
| Release | + notify_all | 54 / 57 | 22 | 16 | 10 / 6.5 / 6 / 5.7 |

Observations after the wake-up fix:
- Debug: translation/submission (`flushCommandBufferInternal`, serial on the main thread after recording) is now the biggest single cost (66 ms: SHADOW 23, DISPLAY 16, REFRACTION 13, REFLECTION 13). The stage-chain wait is the second (~46 ms). Overlapping submission with recording (Phase 1 ordered slots plus early submit) and parallel translation (Phase 2) target both.
- Release: main thread waits ~22 ms on the stage chain, ~16 ms translating, ~10 ms in `vkSwapChain` fence waits (GPU/present-bound portion still unmeasured; GPU timing is needed).
- Per-frame hot counts are unchanged: ~7.3k `bindShaderResources`, ~6.8k push descriptors, ~10.2k `ShaderBuffer::writeBytes`, ~6.2k indirect draws, ~11.4k `Parallel_For` calls on renderer threads (task overhead; partition sizes deserve a look).
- `CommandBuffer::batch/clean` is small (~4-5 ms inclusive in Release, ~9 ms in Debug), so a rewrite is not justified on cost alone.

### Earlier GPU timing baseline (Release, perf mode)
Before tree compaction and the Vulkan sample-shading change, Release was
**GPU-bound on both backends**: GPU time was about equal to CPU frame time.

| Backend | CPU frame | GPU frame | Backend draws | DISPLAY | SHADOW | REFLECTION | REFRACTION | PostFX | Post Render |
|---|---|---|---|---|---|---|---|---|---|
| Vulkan | 54 | 53.2 | ~6270 | 24.8 | 14.1 | 7.0 | 6.6 | 0.5 | 0.2 |
| OpenGL | 47.4 | 47.3 | ~7925 | 17.0 | 14.6 | 7.6 | 7.3 | 0.5 | 0.3 |

Per-stage draws/bin items (both backends): DISPLAY 1715/1090, REFLECTION 1392/931, REFRACTION 1706/1088, SHADOW 2994/841. HiZ culled ~1002 items per frame (58 overflow, 3 skipped).

Consequences for the plan:
- **Release > 60 FPS needs GPU work first** (~53 ms → <16 ms). CPU levers alone cannot reach it. Priority: DISPLAY GPU cost (and why Vulkan DISPLAY is ~8 ms slower than OpenGL: barriers/layout transitions, MSAA resolve, pipeline state or descriptor differences; the parity goal), SHADOW (14 ms: cascades, resolution, caching/staggering), reflection+refraction (~14 ms: half rate, lower resolution, reduced scene lists).
- These map to p3-feature-costs, p3-shadow-caching and a new **p3-vk-display-parity** item, which move ahead of Phase 1/2.
- **Debug ≤ 33 ms** remains CPU-bound (131 ms VK): Phase 1/2 CPU levers (ordered slots, parallel translation, bulk buffer writes) still apply there.
- Per-pass timings only cover the queued main-window command buffers (not immediate/transfer submits); they are only collected while `queryPerformanceStats()` is on.
### GPU scope breakdown (Release, `-PerfMode -GpuScopeTimings`)
Exclusive GPU ms per `buffer/innermost scope`. Scope runs: VK 53.1 ms, GL 49.8 ms (non-scope 53.2/47.3, so the overhead is within noise).

| Scope | Vulkan | OpenGL |
|---|---|---|
| SHADOW/MainPass | 12.97 | 13.48 |
| DISPLAY/W-OIT Pass | 10.33 | 7.70 |
| DISPLAY/MainPass | 8.23 | 4.52 |
| DISPLAY/PrePass | 5.20 | 3.34 |
| REFLECTION/W-OIT Pass | 4.20 | 4.25 |
| REFRACTION/W-OIT Pass | 3.90 | 3.90 |
| REFLECTION/MainPass + PrePass | 2.62 | 3.04 |
| REFRACTION/MainPass + PrePass | 2.56 | 3.10 |
| SHADOW/World AO + CSM blur/other | ~1.2 | ~2.3 |
| SSR + SSAO | 0.64 | 0.67 |
| Vegetation cull/occlude (all stages) | ~0.5 | ~1.2 |
| PostFX total | ~0.6 | ~0.5 |

Findings:
- **W-OIT transparency costs ~18 ms (VK) / ~16 ms (GL), the biggest lever.** Reflection/refraction W-OIT alone is ~8 ms; their opaque passes are only ~2.6 ms each. Check which items enter the transparent bin (vegetation/grass, particles, water?), the overdraw, the accumulation formats and the MSAA sample count. Consider skipping or simplifying OIT in the reflection/refraction stages (quality setting, current visuals as the default).
- **SHADOW MainPass ~13 ms** on both backends: cascade caching/staggering (p3-shadow-caching) is the lever.
- **VK DISPLAY gap is per-pass GPU work, not barriers between passes:** MainPass +3.7, W-OIT +2.6, PrePass +1.9 ms over GL, even though VK culls ~1000 items with HiZ. Suspects: MSAA/attachment load-store ops, render-target formats, shader codegen differences (SPIR-V vs GLSL), descriptor indexing, or depth-test state (early-Z disabled by discard/depth writes in the main pass).
- **GL HiZ culling does nothing:** `hiZCulled=0 hiZSkipped=294` on OpenGL vs `1002/3` on Vulkan. This is a bug to investigate (p3-dispatch-audit).
- Light culling, HiZ construction, Post Render and PostFX are each <0.5 ms: not worth optimizing now.

### Feature costs and failed OIT experiments (Release Vulkan, GPU frame, baseline 53.09 ms)
Measured by disabling one render filter at a time:

| Filter disabled | GPU ms | Saved |
|---|---|---|
| `treeInstances` | 21.25 | ~31.8 |
| `meshes` | 22.32 | ~30.8 |
| `water` | 40.26 | ~12.8 |
| `grassInstances` | 49.04 | ~4.1 |

- Experiment: route grass to the opaque bin instead of W-OIT: 53.09 → 56.36 ms (worse). Reverted.
- Experiment: route opacity-map materials to the opaque bin: saved only ~0.8 ms. Reverted.
- **Root cause at that baseline:** trees cost ~32 ms. `tree.glsl` drew every instance in every pass; the vegetation cull only marked culled instances (`data.z = -1`) and the vertex shader collapsed them, so culled instances still paid the full vertex cost (and `meshes` overlaps because tree submeshes are meshes). GPU instance compaction with GPU-written instance counts was subsequently implemented (p3-tree-instance-compaction).

### Tree compaction results (Release, perf mode)
| Backend | CPU frame (median/p95) | GPU frame | DISPLAY | SHADOW | REFLECTION | REFRACTION |
|---|---|---|---|---|---|---|
| Vulkan before | 53.1 | 53.2 | 24.8 | 14.1 | 7.0 | 6.6 |
| Vulkan after | 41.0 / 43.5 | ~33 | 19.7 | 7.3 | 3.0 | 2.4 |
| OpenGL before | 47.4 | 47.3 | 17.0 | 14.6 | 7.6 | 7.3 |
| OpenGL after | 43.6 / 46.5 | ~31-33 | 12.3 | 9.1 | 5.0 | 3.7 |

- **OpenGL binding-aliasing bug (fixed):** `ShaderProgram::RegisterSetLayoutBinding` gave `COMPUTE`-only buffers their own counter starting at 0, so they shared OpenGL binding points with graphics buffers. GL binding state is global, and `GetDescriptorSlotForGLBinding` returned the first match. Effects:
  - `GPU_COMMANDS` (`PER_BATCH` 2) aliased the `PER_DRAW` 13 SSBO. This broke the HiZ cull on GL (the old p3-gl-hiz-bug) and the instance-count patch, which bound both buffers.
  - The new `PER_PASS` 14/15 aliased `NODE_TRANSFORM_DATA` and `NODE_INDIRECTION_DATA`, which produced a blue screen with only grass and sky.
  - Fix: storage buffer bindings share one counter across all shader stages; uniform buffers use their own OpenGL namespace and counter. The current layout needs 16 SSBO and 6 UBO binding points rather than assigning 22 distinct indices against the SSBO limit. The earlier capture used the single-counter version on the test GPU (96 SSBO and 84 UBO slots).
- OpenGL is now CPU-bound in Release (CPU 43.6 ms vs GPU ~33 ms). Vulkan is closer to balanced: its DISPLAY is still 19.7 ms vs 12.3 ms on GL (p3-vk-display-parity).
- An earlier GL measurement of 44.7 ms came from the broken render and is invalid.
- Debug Vulkan: 133.8 ms median (p95 150), GPU ~42 ms. Debug is CPU-bound and unchanged by compaction, so CPU levers (Phases 1/2/4) are next for the Debug goal. Release is now near CPU/GPU balance on Vulkan and CPU-bound on OpenGL, so CPU levers also matter there.

### Ordered slots results (perf mode)
| Build | Backend | Before | After (median) |
|---|---|---|---|
| Release | Vulkan | 41.3 | 31.4 |
| Release | OpenGL | 43.6 | 33.5 (p95 35.2) |
| Debug | Vulkan | 133.8 | 112.1 (p95 117.7) |

Visuals match the reference captures on both backends. errors.log contains only the usual stub messages.

Data-dependency audit (what still crosses stages on the CPU):
- DISPLAY and NODE_PREVIEW read the reflection/refraction texture handles that the REFLECTION/REFRACTION stages write into `RenderingComponent`. This is kept as a recording dependency.
- LightPool's `shadowPropertyIndex` (written by SHADOW) was the only cross-stage read in `uploadLightList`. It is now patched on the main thread after all stages finish (`patchShadowIndices`) instead of creating a recording dependency.
- The executor reads `getLoDLevel(DISPLAY)` into `transformOut._data.z`. That getter currently reads the lock-level value, not `_lodLevels`; the earlier claim of a race with `_lodLevels` writes was not established. Check the intended LoD semantics separately.

Recording timings (Release VK):
- Recording: SHADOW 12.3, REFRACTION 6.8, REFLECTION 6.3, DISPLAY 5.7 ms.
- Translation: SHADOW 5.3, DISPLAY 3.4, REFRACTION 3.2, REFLECTION 3.1 ms.
- The critical path is now max(SHADOW, REFL/REFR + DISPLAY) instead of the full chain sum.

Issues found at that historical checkpoint (subsequently addressed):
- Several views uploaded into the same stage light range, so the last write won. The Phase 1 ownership follow-up now retains and binds separate view snapshots.
- Reflection/refraction texture selections and descriptor-update flags now share the rendering-component package lock. Scene-system material updates run outside render-stage work; preserve that frame-phase boundary.

Next CPU levers:
- Early translation on Vulkan, while later stages are still recording (p2-parallel-translate).
- ~10k `ShaderBuffer::writeBytes` calls and ~7.3k `bindShaderResources` calls per frame (p3-buffer-writes).

### Later CPU investigation and deferred experiment

- Vulkan early translation was deprioritized: SHADOW is first in submission order and takes about 12.3 ms to record, so translating later stages early cannot help much until that critical path is shortened. Mapped writes and staged transfers still require a synchronization audit before any early translation.
- In the Release Vulkan caller capture, `writeBytes` accounted for roughly 10k calls / 2.4 ms per frame, `bindShaderResources` about 7.2k / 6.5 ms inclusive, and per-draw descriptor pushes about 6.7k. Changing per-draw loose uniforms creates new immutable uniform snapshots and descriptor updates.
- The attempted move of `INDIRECT_DATA_IDX`, `dvd_instanceDataIndex`, and `dvd_terrainChunkOffset` to `PushData1[3]` caused missing terrain and vegetation on both APIs. The experiment was reverted rather than retaining an unverified optimization. Consider per-draw buffer indexing, base-instance encoding, or batched uniform updates only with correct prepass/main-pass and instance-data coverage.
