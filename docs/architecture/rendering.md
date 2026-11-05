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

- `GFXDevice::flushCommandBuffer()` appends buffers to the active window queue; it does not immediately execute GPU commands.
- `GFXDevice::flushWindow()` executes queued buffers through `flushCommandBufferInternal()` before backend window flush.
- `flushCommandBufferInternal()` walks commands, uploads descriptor state at submission boundaries, and dispatches backend operations.

For bugs before submission, inspect shared command generation and ordering first. Window/frame boundaries matter for missing output or stale state.
`AnimationSystem::Update` skips components without an animator individually;
later animated components must still advance. Animation update events track
changes to the rendered frame, not the evaluator's neighbouring frame index.
Component or global pause/resume transitions also report a change when they
switch the rendered frame between the current animation frame and frame zero.

## Performance investigations (planned)

- Startup is reported to be unacceptably slow. Measure cold and warm time to
  scene readiness, separating asset loading, shader compilation, pipeline
  creation, synchronization and frame warmup before optimizing the dominant
  costs. Preserve correct shader-cache invalidation when investigating caching.
  `ResourceCache::Build` times the complete resource `load` call, including
  synchronous waits; it is not GPU frame time. Its `ProfileTimer` uses a steady
  clock independent of application timer resets.
- Vulkan is reported to render at roughly one third of OpenGL's performance
  on an AMD Radeon RX 9070 XT. The developer expects that GPU to handle the
  current geometry and shaders comfortably; the ratio is an observation, not a
  controlled benchmark. Once rendering is correct, compare CPU and GPU frame
  times on the same scene, GPU, resolution and settings, with both instrumented
  and representative non-instrumented builds. Investigate backend-specific
  costs without assuming exact parity.

## Stage dependencies

`Kernel.cpp` registers the coarse render graph:

- `SHADOW` precedes `REFLECTION` and `REFRACTION`.
- `DISPLAY` and `NODE_PREVIEW` depend on both `REFLECTION` and `REFRACTION`.

Before changing ordering, audit render-target producers/consumers, depth and clustered-light availability, and PostFX assumptions. `RenderPassManager` owns passes; `RenderPassExecutor` performs per-stage preparation and draw execution.
Pass tasks are queued in dependency order and wait before recording dependent
commands, not just before submission. Waiting workers help execute ordinary
renderer-pool tasks so a dependent stage does not starve a producer's partitions.
Parallel reflection and refraction stages publish their texture selections under
the rendering component's package lock; display preparation snapshots those
selections after both producer stages complete.

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
- Each render stage uploads at most `MAX_ACTIVE_LIGHTS_PER_FRAME` eligible lights. A stage with no eligible lights skips the light-buffer write but still uploads scene data with zero light counts.

`HiZConstruct.glsl` max-reduces each output texel's full source footprint; push data `isBaseLevel` enables the conservative one-texel dilation for the first level only.

`GFXDevice::initRenderingAPI` checks every fixed compute workgroup in `REQUIRED_COMPUTE_WORKGROUPS` with `SupportsComputeWorkgroup`. Device limits are inclusive maxima, both per axis and for total invocations. Unsupported devices fail with `ErrorCode::GFX_OLD_HARDWARE`. Vulkan guarantees only 128 invocations, while some groups use 256 or 512, so keep this table in sync with shader `local_size` values. Dispatches round up, so compute shaders that write images must bounds-check `gl_GlobalInvocationID` against the target size.
### Shadow maps and spotlights

`Camera::lookAt` accepts a world-space eye and reconstructs the camera
orientation from the inverse view rotation. CSM, spot and cubemap shadow
cameras, as well as world AO, depend on this shared camera transform.
The mutable `viewProjectionMatrix()` accessor also refreshes the combined
matrix when the frustum is dirty, even before a full `updateLookAt()`.
Stored shadow view-projection matrices apply the XY bias after projection.
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
unshadowed rather than indexing a nonexistent split.
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

Vegetation instance buffers are partitioned by terrain chunk, with fixed-capacity slots per chunk and separate active instance counts. Compute culling must check the active count before indexing a chunk's slots. Tree culling stores a negative `data.z` sentinel for hidden instances and positive integer-valued LOD flags for visible ones; the tree vertex shader must test the sentinel before converting the value to `uint`. LOD 3 is reserved by the vertex shader for the culled fallback.

Tree mesh draws use separate child `RenderingComponent` packages per species.
Each child binds its own instance buffer and supplies `dvd_instanceDataIndex`
as the absolute offset of its species range in the chunk's fixed-capacity block.
The uniform's GLSL initializer is not a fallback because shader preprocessing
strips loose-uniform initializers.

Materials that require instanced indirection must mark their rendering component instanced even for a single instance. Instanced draws upload `INDIRECT_DATA_IDX` and use base instance zero; ordinary draws encode the node indirection index plus one in the base instance. Grass and tree shaders use a draw-local instance index: OpenGL's `gl_InstanceID` is already local, while Vulkan's `gl_InstanceIndex` includes the base instance. Vegetation buffers accept CPU writes, and empty chunks do not issue zero-length uploads. A terrain can omit tree meshes when it only needs grass.

Hi-Z culling queries select a mip level and use nearest texel and nearest-mip sampling with a minimum LOD of zero. This preserves the max-reduced depths used for conservative visibility; the reduction itself reads single-mip views with `texelFetch`. Vegetation culling records a buffer dependency for each written buffer after the compute dispatches and before the subsequent draw commands.
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
reprojections and stored unweighted; the material pass applies the SSR blend
weight once. There is no previous-depth disocclusion test, so temporal
reflections remain approximate. SSR is applied only in the display pass; other
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
