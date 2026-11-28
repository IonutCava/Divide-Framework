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
  layer/object/broad-phase filter tables, and the contact/activation listeners.
- `Source/Physics/None/`: the no-op backend, used when no physics library is selected.
- `Source/Physics/Headers/PhysicsAsset.h`: the per-node body handle returned by
  `createRigidActor`. A `PhysicsAsset` is an `ITransform` owned by a
  `RigidBodyComponent`.
- `Source/ECS/Components/RigidBodyComponent.{h,cpp}`: the per-node physics component.
- `Source/ECS/Systems/RigidBodySystem.{h,cpp}`: lazily materializes bodies for nodes
  that carry the `ComponentType::RIGID_BODY` bit.
- `Source/Core/Kernel.cpp` (line 767) selects `PXDevice::PhysicsAPI::Jolt`.

## Current state

The Jolt backend is wired at the API-shell level but performs no real simulation yet.

- `PhysicsJolt::initPhysicsAPI` creates the `JPH::PhysicsSystem`, the job-system thread
  pool, and the temporary allocator, and registers two object layers (`NON_MOVING`,
  `MOVING`) with their pair/broad-phase filters. The contact and activation listeners
  are stubs that only log.
- `PhysicsJolt::frameStartedInternal` steps `_physicsSystem->Update(...)` every fixed
  step.
- Gravity is **not** set: `PhysicsSystem::Init` is called without gravity, so a dynamic
  body would never fall.
- `createRigidActor`, `convertActor`, and `intersect` are stubs that return `nullptr` /
  `true` / `false`. `frameEndedInternal` is empty, so body transforms never write back
  to the scene.
- `RigidBodyComponent` registers no editor fields, so the property window exposes no
  physics controls, and its `physicsCollisionGroup` property is read-only
  (`PROPERTY_R`). `RigidBodySystem::OnFrameStart` calls `createRigidActor` but receives
  `nullptr`, so no bodies materialize.
- Only primitives (`BOX_3D`, `SPHERE_3D`, `QUAD_3D`), terrain, and water set the
  `RIGID_BODY` component bit in `Source/Scenes/Scene.cpp`. Meshes do not.

## Design decisions

These record the agreed direction; implementation is planned, not yet landed.

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
  and vehicle bodies are wanted but deferred: for now they map to the `MOVING` layer and
  are not simulated. The body/layer design keeps room for a Jolt character controller.

## Planned integration

The path that makes a body real and lets it fall:

1. `PhysicsJolt::createRigidActor` builds a `JPH::BodyCreationSettings` from the node's
   transform, bounds, and type, with `EMotionType::Static` by default, and adds it to
   the world through the body interface. A Jolt-specific `PhysicsAsset` subclass (kept
   inside the Jolt translation unit so `JPH::BodyID` does not leak into engine-wide
   headers) stores the body ID.
2. `PhysicsJolt::convertActor` switches the body between static and dynamic by calling
   `BodyInterface::SetMotionType` with `EActivation::Activate` when the group becomes
   `GROUP_DYNAMIC`, and back to static otherwise.
3. `RigidBodyComponent` registers a `DROPDOWN_TYPE` editor field for the collision group,
   defaulting to `GROUP_STATIC`. The setter forwards through `physicsCollisionGroup` to
   `convertActor`. For terrain nodes the field is `_readOnly = true`. The
   `physicsCollisionGroup` property becomes writable (`PROPERTY_RW`).
4. `PhysicsJolt::frameEndedInternal` reads active body transforms and writes them back
   into the owning node's `TransformComponent` (moving-layer bodies only), so a dynamic
   body visibly falls and rests.
5. `Scene.cpp` extends the mesh (and any bounds-carrying 3D object) component masks with
   `ComponentType::RIGID_BODY` so every appropriate node gets a body; the lazy
   `RigidBodySystem` creates them as static by default.

## Invariants

- Keep the backend boundary generic. The wrapper interface exposes only what both
  backends implement; Jolt-specific types stay inside `Source/Physics/Jolt/`.
- Physics owns its per-frame step. Scene code requests changes (add/remove body, change
  group) rather than mutating the world mid-step; the queued change is applied at frame
  start/end, mirroring the [scene-loading frame phases](scene-loading.md#frame-phases).
- Static bodies stay on `NON_MOVING`; only moving bodies live on `MOVING`, and the pair
  filter already prevents static-static contacts.
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

- Kinematic, ragdoll, and vehicle motion are deferred; their `PhysicsGroup` values exist
  but only map to the `MOVING` layer for now.
- Triangle `MeshShape` and the instanced vegetation collider are follow-up work.
- A Jolt character controller is anticipated but not yet designed in.
