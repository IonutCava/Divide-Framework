<!-- Copyright (c) 2026 DIVIDE-Studio -->

> Living plan. Agents: update the status checklist below as work items complete, and keep findings in the relevant section.

# Renderer performance plan (WarScene, Divide-Framework)

## Engineering style (agreed)
- Middle ground; avoid over-engineering in either direction:
  - No ASM or compiler-trick hacks. SIMD is fine and encouraged where hot.
  - No deep OOP: avoid virtual dispatch in hot paths, smart-pointer churn and per-object heap allocations.
- Backend code (GL/VK translation, pools, allocators): go as low-level as needed while staying readable. Use flat arrays/SoA, handles instead of pointers, linear/frame allocators, and switch-based dispatch over virtuals.
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
  - Debug: â‰¤ 33 ms (30 FPS) on both backends. Release/profile: < 16.6 ms (> 60 FPS).
  - Vulkan must be at least on par with, ideally faster than, OpenGL.
- Baseline (Optick CPU wall-clock medians): GL Debug 181 ms / Release ~76 ms; VK Debug 517 ms / Release ~411 ms (validation ON).
- Vulkan perf measured with **validation OFF**; validation-ON runs are correctness gates only.
- Default Vulkan MSAA shading follows OpenGL's per-pixel behavior, as explicitly requested; preserve cross-backend visual parity.
- Keep SceneGraphNode and RenderStage. Targeted rewrites of the command-buffer API are allowed where they help batching/submission.
- Tests mostly at the end, except for high-risk changes (scheduler, geometry pool, bindless).
- Existing code/architecture is a guide, not proven design: unusual choices may be plain mistakes. Question them, fix or replace freely when measurements or correctness justify it, and note any bugs found along the way.

## Key findings driving the plan
1. **Stage chain is the CPU critical path.** Kernel.cpp declares SHADOW â†’ {REFLECTION, REFRACTION} â†’ {DISPLAY, NODE_PREVIEW}. Main thread spends ~50 ms in `TaskPool::wait`. Stage tasks only *append* command buffers to the window queue; translation runs serially on the main thread in `GFXDevice::flushWindow`. Much of the chain enforces submission order, not data order.
2. **Real CPU data dependencies** between stages: `LightPool::generateShadowMaps` writes `_shadowBufferData`/`s_shadowBuffer` (consumed by later stages via GPU only); `sortLightData` per stage; shared static executor buffers (`s_materialBuffer`, `s_transformBuffer`, `s_indirectionBuffer`); env-probe refresh in REFLECTION; reflector budgets. These must be audited before parallel recording.
3. **Vulkan is validation-dominated** (single vkQueueSubmit ~160 ms; ~12 Âµs per indirect draw) â†’ first measure validation-off.
4. **Per-draw state cost**: ~6.2kâ€“7.8k draws, ~6.8k push-descriptor updates, ~7.3kâ€“8.9k `bindShaderResources`, ~10.2k `ShaderBuffer::writeBytes` + `LockManager::waitForLockedRange`, ~1.8k pipeline binds, ~1.27k VB/IB binds, ~1055 compute dispatches, 296 barrier calls. Indirect batching only merges consecutive draws with identical buffers/state/push constants.
5. Shader stubbing had negligible effect â†’ CPU/driver bound.
6. Workers show large uninstrumented time inside `runTask`; the external parser only reports top exclusive scopes, so per-stage inclusive costs are invisible.
7. Suspected `RenderQueue::sort` parent-task allocation leak (unconfirmed).
8. EASTL already in vcpkg.json and partially used.

## Phases

### Phase 0 â€” Measurement foundation (do first; everything else is judged against it)
- **p0-harness-perf-mode**: add opt-in Vulkan validation-off perf mode to `Tools\FrameworkSmokeTest.ps1` (default stays validation-on); support both backends selecting API without silently editing config.xml (explicit flag).
- **p0-parser**: extend Optick summary parser (kept in `Tools/` or session files, not as a test harness) to report inclusive time per scope, per thread, per frame call counts, and per-RenderStage inclusive times.
- **p0-scopes**: add missing Optick scopes: per-stage inclusive (`RenderPass::render` per stage), `RenderPassExecutor::doCustomPass` sub-phases (prepareRenderQueues, sort, buildDrawCommands, PrepareGPUBuffers, pre/main/OIT/transparent), `flushCommandBufferInternal` per command type, `CommandBuffer::batch/clean`.
- **p0-baseline**: capture Debug + Release on GL and VK (validation off and on) from the fixed reference camera â†’ `baseline.md` in session files with per-stage and per-command tables plus workload counters (visible nodes per stage, HiZ-culled, draws, active and shadow-casting lights). Also verify that HiZ culling and light culling are actually working; a broken cull is both a bug and a perf issue. Re-run this matrix after every phase.

### Phase 1 â€” Critical path / scheduling (largest expected Debug win)
- **p1-prototype**: build, run, measure the ready-only scheduler prototype (`RenderStageSchedule.h`, `RenderPassManager`); check worker starvation; keep only if it helps.
- **p1-data-deps-audit**: document every CPU-side cross-stage dependency (light sorting, shadow data, static executor buffers, probe pool, reflector budgets, `s_Nodes`, `PrepareGPUBuffers/FlushBuffersToGPU`) and make shared state per-stage or pre-computed before stage tasks start (e.g. move `sortLightData` / shadow matrix computation into a pre-pass on main/worker before recording).
- **p1-ordered-slots**: pre-reserve ordered per-stage (per-pass) submission slots in the window CommandBufferQueue so stages record in parallel and submission order is preserved. Remove ordering-only edges from Kernel.cpp dependencies. (High risk â†’ Catch2 tests for slot ordering.)
- **p1-split-shadows**: split shadow stage per light/cascade, and reflection/refraction per probe/reflector, into parallel subtasks writing into their own slots.
- **p1-sort-leak**: confirm/fix `RenderQueue::sort` task allocation leak.

### Phase 2 â€” Command buffer system and main-thread translation
- **p2-cmdbuf-assess**: profile `CommandBuffer::batch()`/`clean()` and command storage (allocations, polymorphic commands, per-command vectors). Decide fix/remove from data.
- **p2-cmdbuf-rewrite**: targeted rewrite of hot command paths: arena/linear-allocated command storage reused across frames, fewer per-command heap allocs, merge-friendly draw representation (multi-draw command carrying a range of indirect records).
- **p2-parallel-translate**: translate per-slot command buffers to API commands in parallel. Vulkan: per-slot secondary command buffers (or separate primaries) recorded on workers with per-thread command pools and state trackers; GL: keep serial translation but cheaper. (High risk.)
- **p2-submit-batching**: Vulkan: single submit per frame with batched barriers (`vkCmdPipelineBarrier2` coalescing in vkTexture/VKWrapper), reuse command buffers, avoid per-frame begin cost.

### Phase 3 â€” Draw/state batching
- **p3-buffer-writes**: coalesce the ~10k `ShaderBuffer::writeBytes`/`waitForLockedRange` calls into per-frame bulk writes (one range lock per buffer per frame; persistent mapped ring).
- **p3-bindless**: full bindless on both backends behind a simple command-buffer-facing interface (texture/sampler handles stored in material data; one global descriptor set). Vulkan: descriptor indexing, evaluate `VK_EXT_descriptor_buffer`; GL: existing bindless residency, batched `makeTextureViewResident`. Remove per-draw push descriptors for material textures. (High risk â†’ tests for handle allocation/lifetime/deferred free.)
- **p3-geometry-pool**: shared vertex/index pools keyed by vertex layout + index width with suballocation (firstIndex/baseVertex), GPU-completion-safe free via existing deferred deletion; migrate static meshes, then dynamic. Audit `DrawIndex` semantics. (High risk â†’ tests for allocator.)
- **p3-mdi**: once state is uniform (bindless + pooled geometry + per-draw data via indirection buffer instead of push constants), emit one multi-draw-indirect per (pipeline, pool) bucket; adjust sort keys to maximise bucket length.
- **p3-dispatch-audit**: explain the ~1055 compute dispatches per frame (HiZ per pass, culling, blur, grass/foliage, PostFX). Merge or skip redundant ones, e.g. a single-pass HiZ downsample or one shared culling dispatch per stage.
- **p3-feature-costs**: per-feature cost breakdown for terrain, grass/trees (instancing path), water (reflection/refraction passes), sky, PostFX and the MSAA resolve. Cut redundant passes, such as reflection/refraction re-rendering heavy vegetation or terrain at full detail.
- **p3-pipeline-binds**: reduce pipeline binds via 64-bit sort keys (radix sort) and pipeline dedup.
- **p3-vk-display-parity**: Vulkan DISPLAY costs ~24.8 ms GPU vs ~17 ms on OpenGL with identical draw/bin counts. Find the difference (barriers and layout transitions, MSAA resolve, load/store ops, pipeline state, descriptor updates). Use per-pass timestamps first, then RGP if needed.
- **p3-gpu-culling-count**: where HiZ culling already runs on GPU, write compacted indirect args + draw count and use DrawIndirectCount on both backends (removes CPU-side per-draw work).
- **p3-shadow-caching**: cache shadow maps for static casters and unmoved lights. **Staggered updates are allowed:** distant CSM cascades, point/spot shadows of far lights, environment probes and planar/cube reflection/refraction targets may refresh at half rate (or round-robin across frames), with near cascades and the main view kept per-frame. Make the rates configurable per feature (e.g. in config.xml/scene settings) and expose a "force every frame" debug toggle. Use low-detail scene lists for reflection/refraction passes. Make sure staggered targets stay consistent: matrices stored with the map, and no sampling of half-written layers.

### Phase 4 â€” Debug-build CPU cost
- **p4-debug-profile**: identify Debug-only hot spots (STL debug iterators, non-inlined helpers) from Phase 0 data.
- **p4-containers**: migrate hot-path containers to EASTL/custom containers with custom (arena/frame) allocators: render queues, command buffers, draw lists, visible-node lists.
- **p4-inlining**: enable selective inlining (/Ob1) for engine Debug or force-inline hot helpers; change `_ITERATOR_DEBUG_LEVEL` only if proven necessary and compatible with vcpkg deps.

### Phase 5 â€” Validation, tests, docs
- **p5-tests**: Catch2 tests for scheduler slots, geometry pool allocator, bindless handle allocator, command buffer changes.
- **p5-acceptance**: harness 300-present acceptance on both backends with validation ON; perf matrix with validation OFF; RGP capture for GPU-side bottlenecks once CPU < 33 ms.
- **p5-docs**: update docs/architecture (rendering, threading, gpu-resources, shader-descriptors), AGENTS.md as needed; decision records for bindless and geometry pooling in docs/decisions/.

## Risks / notes
- Parallel recording + parallel Vulkan translation touch global state trackers â†’ highest risk; gated behind measured wins and tests.
- Bindless changes shader descriptor layouts â†’ shader cache invalidation via AppendToShaderHeader / preprocessing version bump.
- Vulkan lifetime rule: all resource destruction deferred MAX_FRAMES_IN_FLIGHT+1 frames; pools must respect it.
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
- [x] p0-baseline — Debug/Release perf mode captured on both backends (see Findings). Harness gained `-Configuration Debug|Release` (Release restricted to `-PerfMode`). The backend switch now overrides the configured API in memory for OpenGL too. Workload counters (per-stage draws/bin items, HiZ culled/overflow/skipped) and per-pass GPU times are logged as `[PerfMetrics]`. HiZ culling is active (~1000 items culled, 58 overflow). Clustered light culling is dispatched every pass. Validation-on runs are deferred to p5-acceptance (correctness gate only).
- [x] p0-gpu-timers (new) — Frame GPU time on both backends (VK timestamp pool per frame slot; GL fixed `RingBufferSeparateWrite` precedence bug and query ring writing ahead of the read slot). Per-command-buffer GPU timestamps (`writePassTimestamp`/`readPassTimestamps`, `GPUPassTimingTable`) logged as `[PerfMetrics] gpuPasses`. Pipeline-statistics queries are now opt-in (`queryPipelineStatistics`): on AMD GL they cost about 10x the frame time. Editor panel no longer overrides perf-mode stats.
- [x] p1-taskpool-wakeups (new, found during baseline) — `TaskPool::runTask` woke waiters with `notify_one()` on a single CV shared by every waiter (main thread, stage tasks, nested `Parallel_For`), so the right waiter often slept the whole 2 ms timeout. Switched to `notify_all()`. Release VK 121→54 ms, Debug VK 217→131 ms; SHADOW record 62→10 ms (Release).
- [x] p1-prototype — kept; it is now the ready-only recording schedule used by p1-ordered-slots.
- [ ] p1-data-deps-audit — Partially audited: known shadow-index and reflection/refraction dependencies are handled. Verify the remaining shared stage state and the per-stage light-buffer overwrite described in "Ordered slots results".
- [x] p1-ordered-slots — Stages now declare GPU submission dependencies (`RenderPass::dependencies`) separately from CPU recording dependencies (`recordingDependencies`, which must be a subset). Stage tasks only record. After `pool.wait`, the main thread flushes the stage command buffers in topological submission order (`RenderStageSchedule::topologicalOrder`), then calls `LightPool::patchShadowIndices`, then `FlushBuffersToGPU`. SHADOW is now a GPU-order-only dependency. Release: VK 41.3→31.4 ms, GL 43.6→33.5 ms. Debug VK: 133.8→112 ms. Catch2 test added.
- [x] p1-node-update-bitset experiment — An atomic bitset measured 41.3 ms (within noise) but was reverted: indirection storage can grow beyond `MAX_VISIBLE_NODES`, so the fixed bitset could access out of bounds. The original growable set is retained.
- [x] p1-render-workers-experiment (new) — Raising `RENDER_WORKERS` from 6 to 12 made frames slower. Reverted.
- [ ] p1-split-shadows
- [x] p1-sort-leak — Confirmed and fixed. `RenderQueue::sort` and `RenderPassExecutor::buildDrawCommands` allocated a parent task they only enqueued if a bin or queue was large. Pooled tasks that are never enqueued keep `_unfinishedJobs == 1`, so each such pass permanently leaked one of the 65,536 slots. `AllocateTask` then probes past more and more dead slots, and eventually spins forever. Both now allocate the parent lazily. No frame-time change in short runs (32.1 ms); this is a long-session stability fix.
- [x] p3-vk-sample-shading — Vulkan now matches OpenGL's per-pixel MSAA shading, as the user requested. It previously forced per-sample shading on every MSAA pipeline. VK GPU 31.3→21.8 ms (DISPLAY MainPass 7.2→3.4, W-OIT 7.2→4.5, PrePass 4.3→1.6); GL was 26.6 ms. No obvious reference-camera difference was observed; shader-dependent sample shading can still change. Vulkan Release is now CPU-bound (frame ~32–35 ms).
- [x] p2-cmdbuf-assess — `batch`/`clean` cost about 4–5 ms inclusive in Release and 9 ms in Debug in the earlier capture; a wholesale rewrite is not justified by those measurements alone.
- [ ] p2-cmdbuf-rewrite
- [ ] p2-parallel-translate
- [ ] p2-submit-batching
- [ ] p3-buffer-writes
- [ ] p3-per-draw-uniforms — About 10k `writeBytes` calls/frame originate in uniform-block commits. A fast push-constant experiment made instanced terrain/vegetation disappear on both backends and was reverted; its root cause remains unconfirmed. Keep the existing per-draw uniforms until an alternative passes both image and performance checks.
- [ ] p3-bindless
- [ ] p3-geometry-pool
- [ ] p3-mdi
- [ ] p3-dispatch-audit
- [ ] p3-feature-costs
- [ ] p3-pipeline-binds
- [ ] p3-gpu-culling-count
- [ ] p3-shadow-caching
- [x] p3-vk-display-parity — Matching OpenGL's shading frequency removes much of the GPU difference; in the measured captures, VK DISPLAY GPU was at or below GL.
- [x] p0-gpu-scope-timings (new) - opt-in per-debug-scope GPU timestamps (`-GpuScopeTimings`); see 'GPU scope breakdown' in Findings.
- [x] p3-woit-cost (new) - Resolved as a misattribution: W-OIT scopes are expensive because they contain the tree geometry, not because of OIT itself (see 'Feature costs' in Findings).
- [x] p3-tree-instance-compaction (new) - Trees: the cull compacts visible instances per species into `PER_PASS` 14 and writes per-species counts to `PER_PASS` 15. A patch compute (`instanceCountPatch.glsl`) clamps indirect `instanceCount` before drawing. Release VK 53→41 ms (GPU ~33), GL 47.5→43.6 ms (GPU 49→33). See "Tree compaction results". Grass is split into p3-grass-compaction.
- [ ] p3-grass-compaction (new) - Apply the same compaction/patch path to grass (~4 ms VK).
- [ ] p3-tree-lod (new) - Distance LoD/impostors for instanced trees; reflection/refraction draw distance as a quality setting (default = current visuals).
- [ ] p3-tree-shadow-cost (new) - Reduce tree cost in SHADOW (per-cascade compaction, distance cut-off for far cascades).
- [x] p3-gl-hiz-bug (new) - Root cause: OpenGL buffer binding aliasing (see "Tree compaction results"). GL now culls 1002 items like VK. Uniform and storage bindings have separate namespaces, while storage bindings are unique across stages; the OpenGL flat-binding limit is not applied to Vulkan's per-stage limit.
- [ ] p4-debug-profile
- [ ] p4-containers
- [ ] p4-inlining
- [ ] p5-tests
- [ ] p5-acceptance
- [ ] p5-docs

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

One preceding Vulkan diagnostic exited during teardown with Windows heap
corruption `0xc0000374`. The next, longer run shut down cleanly after restoring
the growable node set, but the crash's root cause was not established. If it
recurs, capture a debugger stack and investigate; do not infer a GPU loss or
claim the set change proved the cause.

The restored `NodeNeedsUpdate` set remains a possible optimization target:
the OpenGL capture attributes approximately 3.6 ms and 2.9 ms of aggregate
worker time per frame to its transform and material lookups, respectively.
Any replacement must handle indirection indices beyond `MAX_VISIBLE_NODES`,
avoid render-loop allocation churn, and include concurrent and overflow tests.

## Findings (perf mode, Vulkan, WarScene, CPU frame median)
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
- **Root cause:** trees cost ~32 ms. `tree.glsl` draws every instance in every pass; the vegetation cull only marks culled instances (`data.z = -1`) and the vertex shader collapses them, so culled instances still pay the full vertex cost (and `meshes` overlaps because tree submeshes are meshes). The fix is GPU instance compaction with GPU-written instance counts (p3-tree-instance-compaction).

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

Pre-existing issues found (not fixed):
- When a stage contains several passes (for example REFLECTION's water and probe passes), each pass calls `uploadLightData` into the same stage range. The last write wins, so earlier passes may light with the wrong view-space data.
- Reflection and refraction callbacks update `_materialUpdateMask` while holding `_renderPackagesLock`. The rendering system also accesses the mask during update; verify the frame-phase boundary before claiming those accesses can overlap.

Next CPU levers:
- Early translation on Vulkan, while later stages are still recording (p2-parallel-translate).
- ~10k `ShaderBuffer::writeBytes` calls and ~7.3k `bindShaderResources` calls per frame (p3-buffer-writes).

### Later CPU investigation and deferred experiment

- Vulkan early translation was deprioritized: SHADOW is first in submission order and takes about 12.3 ms to record, so translating later stages early cannot help much until that critical path is shortened. Mapped writes and staged transfers still require a synchronization audit before any early translation.
- In the Release Vulkan caller capture, `writeBytes` accounted for roughly 10k calls / 2.4 ms per frame, `bindShaderResources` about 7.2k / 6.5 ms inclusive, and per-draw descriptor pushes about 6.7k. Changing per-draw loose uniforms creates new immutable uniform snapshots and descriptor updates.
- The attempted move of `INDIRECT_DATA_IDX`, `dvd_instanceDataIndex`, and `dvd_terrainChunkOffset` to `PushData1[3]` caused missing terrain and vegetation on both APIs. The experiment was reverted rather than retaining an unverified optimization. Consider per-draw buffer indexing, base-instance encoding, or batched uniform updates only with correct prepass/main-pass and instance-data coverage.
