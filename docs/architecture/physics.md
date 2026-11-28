# Physics

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points and ownership

- `Source/Physics/Headers/PXDevice.h` and `Source/Physics/PXDevice.cpp`: the physics
  device facade. It is a `PhysicsAPIWrapper` plus a `FrameListener`, selects a backend
  (`Jolt` or `None`) from `apiID`, and forwards every call to the backend. Owns the
  backend instance.
- `Source/Physics/Headers/PhysicsAPIWrapper.h`: the backend-neutral interface
  (`initPhysicsAPI`, `closePhysicsAPI`, per-frame step, `createRigidActor`,
  `convertActor`, `intersect`). Also defines `RigidBodyShape` and the `PhysicsGroup`
  enum.
- `Source/Physics/Jolt/Jolt.cpp` and `Headers/Jolt.h`: the Jolt backend. Owns the
  `JPH::PhysicsSystem`, `JPH::JobSystemThreadPool`, `JPH::TempAllocatorImpl`, the
  layer/object/broad-phase filter tables, and the contact/activation listeners. Defines
  `PhysicsAssetJolt`, the Jolt-specific `PhysicsAsset` holding the body ID (found
  through the Jolt body's user data).
- `Source/Physics/None/`: the no-op backend, used when no physics library is selected.
- `Source/Physics/Headers/PhysicsAsset.h`: the per-node body handle returned by
  `createRigidActor`. A `PhysicsAsset` is an `ITransform` owned by a
  `RigidBodyComponent`.
- `Source/ECS/Components/RigidBodyComponent.{h,cpp}`: the per-node physics component.
- `Source/ECS/Systems/RigidBodySystem.{h,cpp}`: lazily materializes bodies for nodes
  that carry the `ComponentType::RIGID_BODY` bit.
- `Source/Core/Kernel.cpp` (line 767) selects `PXDevice::PhysicsAPI::Jolt`.

## Current state

The Jolt backend now simulates: primitives get real bodies, and flipping a body
to dynamic makes it fall under gravity.

- `PhysicsJolt::initPhysicsAPI` creates the `JPH::PhysicsSystem`, the job-system thread
  pool, and the temporary allocator, registers two object layers (`NON_MOVING`,
  `MOVING`) with their pair/broad-phase filters, and sets explicit gravity
  (`(0, -9.81, 0)`, Y-up). Contact/activation listeners are registered but inert.
- `PXDevice::initPhysicsAPI` initialises the fixed time step via `updateTimeStep`;
  without this the accumulated-time gate in `PhysicsAPIWrapper::frameStarted` never
  steps the simulation.
- `PhysicsJolt::createRigidActor` creates a static body for `BOX_3D`, `SPHERE_3D` and
  `QUAD_3D` nodes: `BoxShape`/`SphereShape` from the node's local bounds and world
  scale, wrapped in a `RotatedTranslatedShape` when the bounds are not centred on the
  pivot. Bodies are created with `mAllowDynamicOrKinematic` so they can become dynamic
  later. Terrain, water, and mesh nodes return no body yet.
- `PhysicsJolt::convertActor` (and `PhysicsAssetJolt::physicsCollisionGroup`, which the
  component forwards to) switches bodies between static and dynamic via
  `SetObjectLayer` + `SetMotionType`. Kinematic is mapped; ragdoll and vehicle are
  simulated as plain dynamic bodies for now.
- `RigidBodyComponent` registers a Collision Group dropdown (default Static, read-only
  for terrain) that drives the conversion.
- `PhysicsJolt::frameEndedInternal` writes active dynamic body transforms back into the
  owning node's `TransformComponent`, converting world to local when the node is
  parented. A Jolt-specific `PhysicsAssetJolt` (defined in `Jolt.cpp`) holds the
  `BodyID` and is found through the body's user data.
- Only primitives (`BOX_3D`, `SPHERE_3D`, `QUAD_3D`), terrain, and water set the
  `RIGID_BODY` component bit in `Source/Scenes/Scene.cpp`. Meshes do not.

## Design decisions

These record the agreed direction; the first increment is implemented and the rest is
planned.

- **Per-scene physics settings.** Gravity and other simulation settings (time step,
  body/pair/contact capacities, solver iterations) are per-scene configuration read
  from the scene's XML description, not hard-coded in `initPhysicsAPI`. They flow
  through `Scene::load` / `initPhysicsScene(Scene&)` into the Jolt backend, which
  applies `SetGravity` per scene. The engine is Y-up (`MathVectors.h`, `WORLD_Y_AXIS`),
  so gravity points along `-Y`.
- **Default static.** Every node that should collide starts as a static rigid body.
  Static bodies use `EMotionType::Static` and the `NON_MOVING` object layer; changing a
  body to dynamic (below) flips it to `EMotionType::Dynamic` on the `MOVING` layer so it
  falls under gravity.
- **Collision shape selection.** Shape is derived from the node: primitives use
  `BoxShape`/`SphereShape`; meshes use a convex hull by default with an optional
  triangle `MeshShape` for specific nodes (an editor toggle); terrain uses Jolt's
  `HeightFieldShape` with height data plumbed in.
- **Terrain is static and read-only.** The terrain node's collision-group option is
  `_readOnly` in the editor, and its body is always static.
- **Instanced vegetation.** Trees are instanced, so per-node `RigidBodyComponent`s do
  not apply. Collision against vegetation requires a dedicated instanced-collider
  system that emits static bodies (or an equivalent batched representation) for
  instances.
- **Kinematic / ragdoll / vehicle, and a character controller.** Kinematic, ragdoll,
  and vehicle bodies are wanted but deferred: ragdoll and vehicle are simulated as
  plain dynamic bodies for now, kinematic maps to `EMotionType::Kinematic` but nothing
  drives it yet. The body/layer design keeps room for a Jolt character controller.

## Remaining work

- Mesh and other 3D object nodes: convex-hull (or opt-in `MeshShape`) colliders plus
  the `ComponentType::RIGID_BODY` masks in `Scene.cpp`, so every appropriate node gets
  a body.
- Terrain: `HeightFieldShape` with height data plumbed in, replacing the current
  no-body fallback.
- Water: a suitable collider (currently returns no body).
- Per-scene physics settings (gravity, capacities) read from the scene XML instead of
  the explicit default.
- Instanced vegetation colliders (a dedicated system).
- Kinematic body driving, ragdoll, vehicle, and a Jolt character controller.
- `intersect` ray casts through the Jolt broad phase (still a stub).

## Invariants

- Keep the backend boundary generic. The wrapper interface exposes only what both
  backends implement; Jolt-specific types stay inside `Source/Physics/Jolt/`.
- Physics owns its per-frame step. Scene code requests changes (add/remove body, change
  group) rather than mutating the world mid-step; the queued change is applied at frame
  start/end, mirroring the [scene-loading frame phases](scene-loading.md#frame-phases).
- Static bodies stay on `NON_MOVING`; only moving bodies live on `MOVING`, and the pair
  filter already prevents static-static contacts.
- The fixed time step must stay initialised (`updateTimeStep` after `initPhysicsAPI`),
  otherwise the accumulated-time gate never steps the simulation.
- The Jolt body user data is a raw `PhysicsAssetJolt*`. Assets must not outlive the
  physics system; `PXDevice` owns both and outlives scene assets.
- Terrain and instanced vegetation must never become dynamic.

## Safe-change guidance

- When adding a body shape, reuse `BoundsComponent` and the node `SceneNodeType` rather
  than reaching into rendering geometry directly.
- When changing gravity or body capacities, route the value through the scene XML
  settings so it stays per-scene and serializable.
- Preserve the object-layer mapping (`PhysicsGroup` → `NON_MOVING`/`MOVING`) unless a
  new layer is introduced deliberately; adding a layer means updating all three filter
  tables and the broad-phase mapping.

## Known limitations / open items

- Bodies exist for primitives only; mesh convex hulls, terrain height fields, and water
  colliders are pending (see [Remaining work](#remaining-work)).
- Kinematic, ragdoll, and vehicle motion are deferred: ragdoll and vehicle simulate as
  plain dynamic bodies, and kinematic bodies are not driven yet.
- Body capacities (1024 bodies/pairs) are hard-coded until per-scene settings land.
- Write-back converts world transforms to local for parented nodes; a node whose parent
  is itself physics-driven may lag a frame.
- A Jolt character controller is anticipated but not designed in.
