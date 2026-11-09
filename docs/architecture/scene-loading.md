# Scene loading

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Scope and ownership

- `Source/Core/Kernel.cpp`: startup project selection.
- `Source/Managers/ProjectManager.cpp`: project discovery, scene switching,
  activation and save loading.
- `Source/Scenes/ScenePool.cpp`: registered scene factories and scene lifetime.
- `Source/Scenes/Scene.cpp`: scene settings and asset construction.
- `Source/Graphs/SceneGraph.cpp` and `Source/Graphs/SceneGraphNode.cpp`: graph
  outline and per-node serialized components.
- `Source/ECS/Systems/TransformSystem.cpp` and
  `Source/ECS/Systems/BoundsSystem.cpp`: world-transform history and
  synchronized bounds updates.
- `Source/Graphs/Headers/SceneNodeEventQueue.h` and
  `Source/Graphs/Headers/SceneNodeRemovalQueue.h`: deferred event and removal
  batches.
- `Source/Scenes/WarScene/WarScene.cpp`: an example of a specialized scene
  adding content in code.

## Selection and loading flow

In an editor build with the editor enabled, `Kernel` normally selects the
Default project; otherwise it uses the configured startup project. A named
smoke project overrides that choice, and a missing explicit smoke project is
an error rather than a fallback. `Project::Project()` discovers scene entries
by enumerating folders under `Projects/<project>/Scenes/`.
`ProjectManager::loadProjectInternal()` normally selects the first entry;
directory enumeration order is not guaranteed. The smoke runner's `-Project`
and `-Scene` parameters can select a particular scene, for example
`-Project Games -Scene WarScene`; `-Scene` requires `-Project`. A missing
explicitly selected scene is an error.

Smoke runs start in Play mode, using the loaded player camera and advancing
game time. The `-EditorPaused` diagnostic option starts the editor paused and
requires `-DiagnosticPresents`; it does not count as Play-mode acceptance.
Pausing uses the independent editor camera instead. WarScene's HUD reports
the player camera even while that override is active, so its coordinates do
not identify a paused editor view.

1. `Project::switchScene()` verifies both the scene directory and
   `Scenes/<scene>.xml`. `ScenePool::getOrCreateScene()` creates the registered
   scene type, such as `WarScene`, or a generic `Scene`.
2. `Scene::load()` calls `Scene::loadXML()` to read `<scene>.xml`. The file
   supplies visibility, wind, fog, LoD and day/night settings, and names the
   scene graph file, normally `assets.xml`.
3. `SceneGraph::loadFromXML()` parses the nested `<entities><node>` outline.
   `Scene::loadAsset()` recursively builds it on the asset-loader task pool,
   reading each node's XML from `Scenes/<scene>/nodes/<parent>_<node>.xml`;
   the generated filename is made XML-safe. The node type selects resource
   creation (mesh, primitive, terrain, water, sky, and others).
   The resource-specific `loadFromXML()` restores resource properties;
   `SceneGraphNode::loadFromXML()` restores graph components.
4. `Scene::load()` waits for asset tasks, establishes the scene's global water
   plane, requires a sky node, then performs scene-specific post-load work.
   Derived scenes may add further content after calling `Scene::load()`.
5. On activation, `Project::setActiveScene()` calls `LoadSave::loadScene()`;
   unless save loading is disabled for that scene, `current_save.sav` can
   overlay loaded state.

Asset loading uses the bounded, automatically sized pool described in
[Threading](threading.md); increasing its workers does not make project
activation asynchronous. Startup still completes scene loading and one
render warmup frame before exposing the editor. Shaders required by later
frames may continue loading after the editor becomes available.

## Invariants and troubleshooting

`SceneGraph::worldAORevision` tracks changes to rigid interior-coverage
geometry separately from directional-shadow movement queues and static height
bounds. Terrain, vegetation, skinned nodes and translucent materials are excluded
by default; per-instance Include/Exclude rules use the existing generic draw mask.
Explicitly included geometry invalidates coverage when it changes, and policy
transitions invalidate the old image even when a node becomes excluded. Geometry
events, material coverage revisions and source-node visibility changes feed
the revision; see [Rendering](rendering.md#shadow-maps-and-spotlights) for the
cached world-AO image and its camera-footprint contract.

The rendering component's `World AO Participation` dropdown persists Automatic
(0), Include (1) or Exclude (2) as `RENDERING.World__AO__Participation` in each
renderable node's XML. Missing fields remain Automatic. A container does not
implicitly apply its overrides to children: imported mesh submeshes have their
own rendering fields. WarScene's unclassified standalone tree stores exclusions
on its three submeshes instead of mutating the shared imported resource.

`FrameListenerManager` dispatches by call order, then listener GUID. Re-registering
a listener changes its order without registering it twice. A project or scene
switch can destroy and create listeners from a frame callback; dispatch resumes
after the current ordering key without retaining invalidated iterators. New
listeners ordered after that key may receive the current event; earlier ones
wait until the next event. Each listener runs at most once per event, including
when it is re-registered from a callback, and nested events track their own
visits. Disabled listeners receive no frame events.

Check project/scene selection and the XML files before debugging rendering.
A missing per-node XML file skips that node while recursively processing its
children. The `SUBMESH` case waits for its parent mesh resource to reach
`RES_LOADED`, so a stall there points toward mesh resource loading.
`SceneGraph::loadFromXML()` expects a `TRANSFORM` root and currently leaves a
TODO for scene-graph version mismatches rather than rejecting or migrating
them. Keep loading and `Scene::saveXML()` / `SceneGraph::saveToXML()` in sync
when changing serialized data.

`XML::detail::LoadSave::write()` uses `createFile()` for new files, including
missing parent directories. `createFile()` preserves existing contents unless
overwrite is requested. A file-creation failure is logged and returned as a
failed XML save rather than treated as a fatal unexpected call. Backup and
restore failures are logged; the original read or write is still attempted.
`LoadSave` logs Boost XML errors and stream I/O failures and returns failure.
Both read paths reject anything that is not a regular file (for example a
directory) before parsing: libc++ can open a directory as an empty stream that
Boost would parse as an empty document. The standalone `XML::readXML()` also
logs these errors, but returns no status and leaves its tree unchanged.

Water registers its resource editor fields during resource loading, before
`Scene::addWater()` reads their XML. Its graph-node `postLoad()` installs
per-node callbacks without replacing those fields. `EditorComponent` checks
whether the XML component exists, not whether its parent has text content, so
compact XML and programmatically created trees restore their child fields.

Each scene's `<water><global>` settings name a root water node and specify its
surface height, square `size`, and positive depth. The scene XML values
override that node's dimensions and height; a missing node is created with the
same settings. Missing settings default to `Water`, height 0, size 4608 and
depth 500. The stationary plane keeps its authored X/Z position. WarScene's
4608-unit side extends 1280 units beyond each edge of its 2048-unit terrain,
exceeding its 1000-unit view distance. Adjust the scene XML for other scenes;
no sizing depends on terrain dimensions or camera position.

The global plane is always water body 0. Other active water nodes follow it;
the GPU scene buffer currently holds only the first two bodies. Only the global
surface excludes vegetation. `VegetationInstance` rejects roots below the
scene-configured water height during generation, and its cache records that
height. `Vegetation::finalizeInstance` uploads the remaining candidates and
initializes each chunk after scene updates have established the global level.
Per-view GPU culling additionally suppresses roots submerged by a rising level.
After lowering the level below its generation height, reload the scene to
generate candidates on newly exposed ground. Local water bodies do not exclude
vegetation. Tree models load before graph insertion; each nonempty chunk owns
its tree subtree and culling extents. Incompatible or malformed vegetation
caches are regenerated.

Grass placement samples `vegetation.grassMap` and an optional
`vegetation.grassMapSecondary` from the terrain descriptor. Each map's
`channelMask` attribute selects RGBA bits (1, 2, 4, 8); omitted masks default
to all four channels for compatibility. Each channel uses the greater selected
weight across both maps, preserving the existing billboard choice/scale rules.
Main terrain selects Alpha1 red/green (dry/regular grass) and Alpha2 alpha
(lush grass), not the rock/sand channels. Grass retains the 30-degree slope
limit and above-water requirement; trees retain their separate map and
10-degree slope limit. DDS CPU inspection follows the render texture's row
orientation (see [Asset pipeline](asset-pipeline.md)). Grass caches include
the descriptor hash; placement-cache versions reject the older inverted-mask
grass and tree candidates.

After scene updates and vegetation finalization, `SceneGraph` refreshes its
static-geometry AABB only when static geometry changes. It derives that AABB
from individual geometry bounds rather than the root's aggregate, which also
contains sky and dynamic descendants. The sky's non-rendered sphere child is
excluded as well as the sky node itself. Quad bounds include all four corners,
including after corner edits. `BoundingBox::transform` supports both in-place
and distinct-source transforms. Reparenting invalidates the aggregate even when
only a subtree changes classification. Source bounds are read as synchronized
snapshots; per-component revisions ensure asynchronous changes are picked up
by the bounds system on a later update. Change revisions are captured per
component, so an update arriving during recomputation is not cleared for other
instances sharing the same scene node. Vegetation's shared bounds conservatively
expand the terrain bounds for the undeformed scattered instances and are
world-space, matching the vegetation shaders' instance positions. Grass wind and
camera displacement are lateral and are not included in that padding.
`TransformSystem` resolves parent transforms before descendants and propagates
change masks through the hierarchy regardless of component iteration order.
The first resolved world pose initializes both interpolation endpoints. Each
fixed update advances a changed pose's history even if it stops moving, while
frames without a fixed update retain the previous endpoint for interpolation.
It marks affected bounds and their ancestors dirty before `BoundsSystem`
recomputes them. Local-space bounds use the current world matrix at
recomputation, rather than a matrix captured when they first became dirty;
world-space bounds are not transformed a second time. Source-bound revisions
mark every affected ancestor before reference bounds are rebuilt, independent
of component iteration order.
Adding, removing, or reparenting a child also dirties the old and new ancestor
bounds. Deferred removals retain node GUIDs, resolve them against live nodes at
frame end, and remove a moved node from its current parent; duplicate and
already-destroyed targets are harmless. Reparenting snapshots pending local
transforms and preserves the composed world transform using the same rotation
and scale rules as `TransformSystem`. An existing node is not moved when the
new parent has a non-invertible or invalid transform.
Shadow invalidation retains the old and new caster spheres across bounds or
usage-context changes and queues the last sphere before node removal. Full
scene unload skips these removal notifications because the light pool is also
being torn down. The sphere remains the enclosure of the published world AABB;
OBB queries construct independent snapshots and do not mutate it. Bounds updates
must finish before concurrent readers use these published bounds.

Reaching the first present does not establish that the scene loaded correctly;
inspect a captured image for the expected content. The
[rendering guide](rendering.md#framework-smoke-diagnosis) describes the smoke
runner's validation and capture criteria.
