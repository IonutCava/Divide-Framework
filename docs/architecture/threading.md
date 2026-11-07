# Threading and synchronization

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and task pools

- `Source/Core/PlatformContext.cpp` and `Source/Core/Headers/PlatformContext.h`: task-pool ownership.
- `Source/Rendering/RenderPass/RenderPassExecutor.cpp`: partitioned GPU preparation and dirty ranges.
- `Source/Utility/ImageTools.cpp`: background DDS cache generation.
- `Source/Platform/Video/GFXDevice.cpp`: command-buffer queues and frame/window flush.
- GL buffer allocator/lock implementations and Vulkan buffer/texture implementations: resource synchronization.

`PlatformContext` owns `HIGH_PRIORITY`, `LOW_PRIORITY`, `RENDERER`, and `ASSET_LOADER` pools.
Reuse these semantics for asynchronous asset/GPU preparation rather than introducing ad-hoc threads.
Each thread lazily allocates its fixed-capacity, aligned task ring once and reuses
it until thread exit. This keeps the ring out of static TLS, which otherwise
can exhaust Linux worker stacks. Tasks allocated by a thread must finish before
that thread exits, and their pointers must not be used afterward.
`ThreadPoolSizing::Calculate` assigns four to eight asset-loader workers from
the configured `runtime.maxWorkerThreads` budget, reserving the existing render,
low-priority and at least four high-priority workers. The historical minimum
budget of 16 retains its one-thread oversubscription. `runtime.assetLoaderThreads`
defaults to 0 for automatic sizing; an explicit value is clamped to 4-8 and the
available budget, with a warning when it cannot be honored. OpenGL creates a
shared context for each asset and render worker using the same calculated counts.
DDS/NVTT cache generation uses `HIGH_PRIORITY` when available.
Render preparation uses partitions (`g_nodesPerPrepareDrawPartition`) and shared locks around dirty ranges.
When the main thread waits for `Parallel_For` partitions, it also services
`PlatformContext` completion callbacks. Asset-loading workers can depend on
these callbacks to finalize resources, including work from another pool.
Asynchronous task completion callbacks run on the main thread. A `REALTIME`
task and its completion callback execute on their caller, including asset-loader
workers; they must not synchronously depend on the main thread while holding
locks that the main thread may need.
Callback dispatch releases its lock and dequeues one callback at a time, so a
callback can safely wait for another callback. Main-thread task waits and
shutdown waits service pending callbacks as well. The render-pass manager waits
for its parent task through `TaskPool::wait`, which services callbacks and
eligible tasks while using a bounded condition-variable wait between checks;
render-stage tasks marked `DONT_CARE_NO_IDLE` remain on pool workers.

Render stages use ready-only scheduling (`RenderStageSchedule` and
`RenderPassManager`). Stage tasks are precreated and the root stages are
enqueued. Each completed stage decrements atomic predecessor counters, which
release its dependents only after their *recording* dependencies have finished.
Stage tasks never block waiting on other stages. They only record. The main
thread submits the recorded buffers afterwards, in GPU submission order. Catch2
tests cover the schedule and the topological order; the WarScene measurements
are recorded in `docs/plans/renderer-performance-plan.md`.
Preserve the producer/consumer stage ordering described in
[Rendering](rendering.md#stage-dependencies) regardless of scheduler design.

Idle task checks poll both priority queues without blocking. A task wait
continues helping immediately after executing eligible work and only waits
when no work was executed. Parent and `Parallel_For` waits use bounded
completion notifications when no eligible task is available, so they continue
servicing main-thread callbacks without busy-polling. Every task completion
wakes all waiters (`notify_all`): many unrelated waiters (main thread, stage
tasks, nested `Parallel_For`) share one condition variable, and waking only one
left the right waiter sleeping for its full timeout (a 2x frame-time regression
in WarScene). Workers may still wait
briefly on the normal queue between priority checks.
Resource creation without an external task counter does not retain a pointer
to a temporary counter; supplied counters must outlive their asynchronous work.
Resource states use release/acquire ordering to publish completed resource data.
Resource-pool reference-count increments and decrements both require the pool's
exclusive lock. `ResourceCache::Inspect` captures state and GUID under the pool's
shared lock, including stale-handle and bounds checks, so polling cannot race a
failed load's deallocation. A raw pointer returned by `Get` does not retain
that lock or acquire a reference; callers must separately ensure its lifetime.
Texture alpha workers write separate partition summaries and merge only after
the task wait; versioned metadata reads and publications are synchronized.
The per-node frustum-plane cache is a relaxed atomic hint, not a substitute for
testing the remaining planes after an intersection.
Scene-graph spatial callbacks can run on parallel workers. The editor coalesces
their notifications and refreshes gizmo selections on the main thread, retaining
the request while the gizmo is being manipulated.
`Console` serializes stream writes, queue draining, and callback dispatch.
Logging from a callback is queued until the current dispatch completes; a
callback may unbind itself, and an unbind on another thread waits for its
in-flight callback. Callbacks must not wait on work that needs the console lock
or call `Console::Start`. `Stop` drains accepted queued output before shutdown,
including when output has since been disabled. Immediate output does not drain
unrelated deferred messages unless a flush is requested. Temporary diagnostic
flag changes use `Console::ScopedFlags`, which affects only the calling thread.
The editor output window queues a bounded set of complete entries and drains
them into its display ring on the UI thread; overflow retains the newest entries.
Frame-count and application-start timestamp reads used for log decoration are
atomic snapshots, not synchronization for other frame or timer state.
`ProfileTimer` measures intervals with a steady clock independent of application
timer resets; a timer's mutable state still requires exclusive ownership.
Scene-node event slots publish complete payloads before dispatch; overflow is
retained rather than spinning or recursively dispatching. The graph processes a
separate node batch without holding the event-queue lock, so callbacks may queue
more events for a later pass. Node destruction is deferred to frame end and is
serialized against event-batch processing; scene unload destroys the graph
after its work has stopped. Immediate destruction must not run from an event
callback; use the graph's removal queue instead. Producers must stop using node
pointers before those nodes are destroyed.
Deferred removals use a mutex-protected GUID queue and a separate processing
batch. The batch resolves each target against the live-node registry while
holding the graph's destruction/lifetime lock; requests queued by callbacks
wait for the next batch. Child-list mutations take the corresponding exclusive
lock. This does not make arbitrary external node-pointer use or simultaneous
topology edits safe.
Bounds changes use per-component captured revisions, and transform dirtiness
is consumed before recomputation. Changes published during a bounds update
therefore remain pending for the next update.
World-transform interpolation history is initialized on first resolution and
settled on the next fixed update after movement; render frames do not advance it.
OBB queries return independently calculated values rather than mutating a
shared lazy cache or the sphere used for shadow invalidation. Bounds and
transform updates still precede the concurrent read phase.
The shadow pass snapshots the pending moved-volume prefix under the light-pool
lock. Frame end removes only that prefix, retaining later publications; if no
shadow pass runs, accumulated changes collapse into a conservative all-lights
invalidation for the next pass instead of growing without bound.
Previous and current caster spheres are appended under the same lock, so a
shadow-pass snapshot cannot split a movement or usage-context change.
Material descriptor caches are built, invalidated, and copied under the texture
lock. Render preparation receives a snapshot, never a reference to a cache
that another worker may still be populating or invalidating.
Debug-view registration, removal, selection and rendering use their respective
locks. A removed view is retained until the main-window queue is flushed, so
recorded push-constant commands cannot reference freed view data.
`IMPrimitive` serializes recording of a shared primitive and retains separate
uniform snapshots for every draw until window flush. Display and node-preview
passes can record the same editor axis and grid concurrently; their matrices
and pending buffer barriers must not overwrite each other. Snapshot storage
reuses its high-water capacity after the frame changes.
The rendering component's package lock also protects reflection/refraction
texture selections and descriptor-update flags. Callbacks publish selections
after recording their views; dependent stages wait for those producers before
building packages. Scene-system material updates run outside render-stage work.
The draw-command lock serializes instancing discovery with material shader
readiness and pending checks; shader invalidation remains in the next scene
update, outside render-stage workers.
Vulkan shader-loading workers queue reload notifications under a mutex; the
render thread drains a reusable GUID batch at frame end without holding that
mutex while invalidating pipelines.
Vegetation scene updates can visit chunks in parallel; the shared wind and
visibility state is synchronized, while material and tree-node initialization
is per chunk. After graph updates, the scene collects water bodies serially and
finalizes candidate uploads before render preparation. Per-view culling compares
candidate roots with the current global-water height on the GPU; level changes
do not rewrite instance buffers in flight. Tree resources load before per-node
initialization, outside the update lock.
Vegetation preparation is recorded per view and submitted before that view's
draw passes. Shared instance-buffer ranges have GPU read/write and write/read
barriers between views; do not move the compute dispatch into a render pass or
reuse another reflector's Hi-Z results.
Within one phase, explicitly independent packages with disjoint declared writes
share boundary barriers; overlaps and other packages remain ordered individually.
Tree components' non-owning preparation and buffer references point to their
vegetation ancestor, whose graph children are destroyed before the ancestor's
resources. The shared billboard geometry commit is serialized across chunks and
parallel render stages.
OpenGL vegetation initialization runs on the renderer pool, whose workers own
shared GL contexts, rather than executing GPU-buffer fence waits on general
workers without a context.

## CPU and resource lifetime checks

Audit dirty-range locks, allocator free-list locks, command-buffer queue locks, and background asset work that can race render/main-thread resource finalization.
Separate CPU work completion from GPU submission and completion: recorded commands execute later at window flush, as described in [rendering](rendering.md).
The renderer-owned uniform arena serializes allocation, retirement, and
frame-slot recycling with its mutex; unchanged draw consumers only update their
program's usage record and do not take that lock. Shader unload or reload may
retire an allocation from an asset worker, so pages outlive programs and the
arena is destroyed only after shader shutdown and successful GPU completion.
If completion fails, teardown reports the failure and terminates rather than
destroying resources that could still be in use.
Frame-slot completion is established for every submitting window before
retirement processing, not inferred from elapsed frame numbers alone.
Indirect command pages likewise retain per-player frame slots until the bounded
in-flight frame interval has elapsed. Stage-pass preparation uses disjoint
ranges or separate pages, including separate high-water pages for command lists
larger than a regular range, and snapshots the slot in both draw and compute
bindings before later players or frames are recorded. Duplicate-pass pages can
be created on concurrent renderer workers; buffer-count and buffer-VRAM
performance counters use atomic updates and snapshots for those allocations.
Arena writes retain the underlying buffer's range-wait checks, but the
`LockManager` pool and a write-time fence do not replace that all-window
completion proof. Unchanged uniform consumers neither write nor register
per-draw range fences.
Particle sorting and upload-cache selection are serialized per emitter.
Frame-local uploads and their handle arrays have stable addresses even when
another camera grows the cache. Render packages reference immutable draw
snapshots rather than mutating the component's shared command template.
For changes to mapped writes or staging uploads, also read [GPU resources](gpu-resources.md).

## Vulkan queues and async compute

`Source/Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h` exposes `GRAPHICS`, `COMPUTE`, and `TRANSFER` queue types.
Selection and immediate-command use live in `vkDevice.cpp` and `VKWrapper.cpp`.
Several upload paths still use the graphics immediate-command context; queue enums are not evidence that async compute is wired end-to-end.

Smoke-test scene readback uses a separate graphics command pool and completed
fence rather than the immediate-command ring; see
[GPU resources](gpu-resources.md#vulkan-smoke-readback-and-recovery).

Any actual async-compute/transfer feature requires an audit of queue ownership, resource transitions/barriers, completion/lifetime synchronization, and render-stage dependencies.
Do not infer synchronization from descriptor binding type: indirect command-buffer hazards follow `BufferUsageType::COMMAND_BUFFER`; see [shaders and descriptors](shader-descriptors.md).
