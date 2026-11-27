# Scene loading

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Scope and ownership

- `Source/Core/Kernel.cpp` selects and starts the project.
- `Source/Managers/ProjectManager.cpp` discovers projects and coordinates scene switching and activation.
- `Source/Scenes/ScenePool.cpp` owns registered scene factories and scene lifetime.
- `Source/Scenes/Scene.cpp` loads scene settings and assets.
- `Source/Graphs/SceneGraph.cpp` and `SceneGraphNode.cpp` load the scene hierarchy and node components.
- `Source/ECS/Systems/TransformSystem.cpp` and `BoundsSystem.cpp` update world transforms and bounds.
- `SceneNodeEventQueue` and `SceneNodeRemovalQueue` defer graph work to safe processing points.

## Loading flow

1. `Project` discovers scenes under `Projects/<project>/Scenes/`.
   `ProjectManager` selects the startup project and scene.
2. `ScenePool::getOrCreateScene()` creates the registered scene type or a generic `Scene`.
3. `Scene::load()` reads the scene settings and graph file.
4. `SceneGraph::loadFromXML()` reads the hierarchy. `Scene::loadAsset()` creates
   resources and loads per-node XML on the asset-loader pool.
5. After required asset work finishes, the scene performs post-load work and
   becomes active. `LoadSave::loadScene()` may then apply saved state.

Asset loading uses the bounded pool described in [Threading](threading.md).
Scene activation waits for required resources and a render warmup before
exposing the editor; later shader work may continue asynchronously.

## Frame phases

`Kernel::onLoop()` runs each frame in this order:

1. **Frame start.** `ResourceCache::OnFrameStart()`, then `FRAME_EVENT_STARTED`.
   `SceneGraph::frameStarted()` rebuilds the node list if nodes were added or
   removed.
2. **Fixed update loop** (`Kernel::mainLoopScene()`, up to four steps). It
   flushes threaded task callbacks, then runs `Scene::processInput()` and
   `Scene::processTasks()`. `ProjectManager::updateSceneState()` follows: the
   ECS updates, per-node `sceneUpdate()`, queued node events,
   `Scene::flushDirtySceneData()`, static-geometry bounds, and AI.
3. **Render.** `Kernel::presentToScreen()` records and submits the frame.
4. **Frame end.** `FRAME_EVENT_PROCESS` and `FRAME_EVENT_ENDED`.
   `SceneGraph::frameEnded()` applies queued parent changes and node deletions.
   `Kernel::idle()` then lets `ProjectManager::idle()` apply queued player
   additions and removals and lets the scene poll its tasks. Finally
   `ResourceCache::OnFrameEnd()` runs.

The fixed update loop is simulation only. Loading, unloading, and validating
resources or graph structure belong in a queue that frame start or frame end
drains, so that work overlaps GPU rendering instead of stretching the update.
Scene code should request the change (`SceneGraph::removeNode()`, parent-change
and event queues, player queues) rather than apply it during the update.

Known exceptions to this rule:

- Threaded task callbacks, including asset-load completions, are flushed at the
  start of every fixed update step rather than once at frame start.
- `ProjectManager::updateSceneState()` starts the periodic autosave. The
  low-priority task then serializes the live scene graph through
  `Scene::save()` while later frames keep updating it.
- `Scene::flushDirtySceneData()` runs inside the update and performs one-time
  vegetation instance finalization (`Vegetation::finalizeInstance()`).

## Invariants and safe changes

- Keep scene selection, scene XML, graph XML, and per-node component data in sync. Update readers and writers together when serialized data changes.
- Preserve parent-child transforms and per-node overrides when loading imported hierarchies. Resource-specific loaders restore resource properties; `SceneGraphNode::loadFromXML()` restores graph components.
- Asset tasks must not access a resource before it is ready. Independent assets may load in parallel; shared resources still require their existing synchronization.
- `TransformSystem` resolves parent transforms before descendants and propagates changes to affected bounds. `BoundsSystem` publishes synchronized snapshots for concurrent readers.
- Reparenting must preserve world transforms when the new parent has a valid transform. Use the deferred removal queue for node destruction; do not destroy nodes from event callbacks.
- Shadow and visibility systems consume bounds changes, including the last published bounds when a node is removed. Finish updates before concurrent readers use the new bounds.
- Check project selection and the relevant XML files before debugging a scene that does not load. Report missing or invalid resources instead of silently substituting another scene or asset.

Keep scene-specific behavior in the owning scene or feature subsystem. Shared
loading code should not encode policies for a particular project, terrain, or
water setup.
