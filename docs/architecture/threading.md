# Threading and synchronization

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and ownership

- `Source/Core/PlatformContext.cpp` owns the engine task pools.
- `Source/Rendering/RenderPass/RenderPassExecutor.cpp` prepares render data.
- `Source/Platform/Video/GFXDevice.cpp` queues and submits command buffers.
- Resource implementations own their CPU/GPU synchronization.
- `Source/Utility/ImageTools.cpp` performs background image-cache work.

`PlatformContext` owns the `HIGH_PRIORITY`, `LOW_PRIORITY`, `RENDERER`, and
`ASSET_LOADER` pools. Use these pools for asynchronous engine work rather than
creating ad-hoc threads. The pools and all data captured by their tasks must
outlive the work.

## Tasks and callbacks

Task waits may execute eligible work while waiting. Do not hold a lock across a
wait if the work or a completion callback may need that lock.

Asynchronous completion callbacks run on the main thread. A `REALTIME` task and
its callback run on the calling thread, so they must not synchronously depend
on the main thread while holding a lock it may need. Callback dispatch releases
its lock between callbacks, allowing callbacks to enqueue or wait for further
work.

Use `TaskGroup` for optional batches of child tasks. It joins queued children
when waited or destroyed; captured data and the task pool must remain alive
until then. Empty groups should not create work.

## Render-stage ordering

Render-stage tasks record into their own command buffers. The main thread
submits those buffers after recording dependencies finish, in GPU submission
order. A stage's CPU recording dependency must be a subset of its submission
dependencies. Keep CPU data dependencies distinct from GPU ordering when
changing the schedule.

Render preparation partitions work and assigns disjoint writes to workers.
Claims or dirty-range locks establish ownership, not publication. Join workers
before uploading or reading their output. Do not let concurrent views mutate
the same camera, package, buffer range, or uniform snapshot.

See [Rendering](rendering.md#stage-dependencies) for the stage contract and
[GPU resources](gpu-resources.md) for frame-slot completion.

## Locks and contention

Cross-thread handoff should use lock-free queues, atomics, or per-worker output
merged after a join. Locks are acceptable on creation, destruction, and
first-use paths, but not on per-node, per-draw, or per-frame paths.

Current state of the hot paths:

- `TaskPool` queues are `moodycamel` concurrent queues. `_taskFinishedMutex`
  and `_taskFinishedCV` only park waiting threads.
- `RenderBin` appends through an atomic index, and `RenderQueue` takes no locks.
- `RenderPassExecutor` material lookups use a read-only fast path and take
  `_freeListLock` only on a cache miss. The transform and indirection free
  lists take it only when a `RenderingComponent` is created or destroyed.
- `RenderingComponent` guards its packages and draw commands with per-node
  shared locks. Concurrent stage tasks that visit the same node share the
  reader count but do not block each other.
- `CommandBufferPool` takes an exclusive lock per allocation and release, a few
  times per render stage per frame.

Known contention risks:

- The transform and indirection free lists are scanned linearly under the
  exclusive lock, and their `fixed_vector` storage may reallocate past
  `Config::MAX_VISIBLE_NODES` while render workers write into it.
- `CommandBufferPool` could replace its exclusive lock with a lock-free free list.

## Shared state and lifetime

Protect shared mutable state with the owning subsystem's synchronization.
Prefer immutable snapshots when workers need stable data. A raw pointer or
handle does not keep its resource alive unless its type and ownership contract
say so.

Separate CPU task completion from GPU completion. Recorded commands execute
later, during window flush. Keep upload data and resources alive until every
GPU consumer has completed. Do not infer completion from a lock, an elapsed
frame count, or successful command recording.

Scene-graph events and removals are deferred so callbacks can publish new work
without mutating the graph during traversal. Resolve queued removals against
live nodes before destruction. Bounds and transforms must be updated before
concurrent readers use their published snapshots.

For mapped buffers, uploads, resource retirement, or backend queue changes,
also read [GPU resources](gpu-resources.md).

## Console output

`Console` serializes stream writes, queue draining, and callback dispatch. In
normal deferred mode, INFO entries remain queued until an explicit or periodic
flush. WARNING and ERROR entries request a flush on print; `PRINT_IMMEDIATE`
retains immediate behavior for INFO.

The console lock preserves producer order while entries are enqueued. Output
from a callback waits until the current dispatch completes. Callbacks may
unbind themselves; an unbind from another thread waits for an in-flight
callback. Do not wait in a callback for work that needs the console lock or
call `Console::Start` from a callback. Shutdown drains accepted output.

The editor output window queues complete entries and drains them on the UI
thread. Its bounded queue retains the newest entries when full.

## Vulkan queues

`vkResources.h` defines graphics, compute, and transfer queue types. Several
upload paths still use the graphics immediate-command context, so queue names
do not prove asynchronous execution. An asynchronous queue change must define
resource ownership, barriers, completion, and render-stage ordering together.
