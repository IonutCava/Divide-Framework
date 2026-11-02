# GLSL and shader integration audit

Initial source review: 2026-11-02. A second source and host-contract review was
performed after rebasing onto `master` at `ec9a466df`. The status notes
distinguish the committed repairs from the remaining findings.

## Scope and interpretation

The shader tree contains 35 GLSW effect files and 35 shared shader atoms: 20 fragment atoms, four vertex atoms, ten common atoms, and one compute atom. An effect file can contain multiple stages and variants; these counts are not counts of compiled programs. The bundled FXAA implementation is treated as third-party code rather than independently certified.

Findings below distinguish source-proven defects, conditional/latent defects, and profiling-dependent improvements. They are not claims of observed GPU failures on a particular driver. Most arithmetic, indexing, and culling problems affect both backends.

**Repair status:** C1-C12, M1, M2, M5 and R1 have been addressed. The particle shader
compilation defects in M3 and the billboard/UV errors in M16 have been repaired,
as have the dormant water and vector-packing compilation failures. These changes
have focused compiler/link checks (including every clustered-lighting and culling
compute shader on both backends); the new transform, easing, packing, depth
reconstruction, spot bounding-sphere and cluster-index code also have Vulkan
numerical checks. The particle renderer still has the host-side blockers listed
below and has not been validated end to end.

**Remaining priorities:** HiZ sampling and synchronization, tree chunk indexing,
shadow-cache and spotlight correctness, CSM split upload, exposure
synchronization/access, particle integration, and the post-processing and
material defects below. The second audit identifies residual issues in areas
where earlier, distinct defects were fixed; those fixes are not a complete
correctness guarantee.

## How the source actually reaches the APIs

| Step | Engine behavior | Main evidence |
|---|---|---|
| Effect selection | `ShaderModuleDescriptor` selects `file.Stage.variant`; module and global defines contribute to the cached shader identity. GLSW selects the `-- Stage.variant` sections. | `Source\Platform\Video\Shaders\ShaderProgram.cpp:1680-1733,2097-2110` |
| Generated header | Adds GLSL 460, API/stage defines, engine constants, vertex attribute locations, stage interface blocks, and macro aliases. The files are not standalone GLSL compilation units. | `ShaderProgram.cpp:411-785` |
| Includes | Recursively expands atoms according to extension and records dependencies for caches/hot reload. Includes are expanded before conditional macro processing. | `ShaderProgram.cpp:1394-1470,2145-2154` |
| Macro processing | Replaces custom defines and invokes the FPP preprocessor before extracting loose uniforms. | `ShaderProgram.cpp:2145-2154` |
| Loose uniforms | Collects plain uniform declarations into generated `std140` blocks at `PER_DRAW` slots 14/15. Original names become macros referring to block members; initializers are discarded. At most two distinct blocks are supported. This is separate from fast push data. | `ShaderDataUploader.cpp:261-295`; `ShaderProgram.cpp:2154-2198` |
| Fast push data | `PushData0` and `PushData1` are column-major `mat4` values. Vulkan declares them as members of an anonymous 128-byte push-constant block. OpenGL declares two `uniform mat4` values at `GL_PUSH_CONSTANTS_LOCATION` (18) and 19. Reflection records which of the two a program uses. | `ShaderProgram.cpp:2200-2220`; `ShaderProgram.h:122` |
| Upload | Vulkan calls `vkCmdPushConstants`; OpenGL uploads both matrices using `glProgramUniformMatrix4fv(..., GL_FALSE, ...)`. Other uniforms use reflected UBO uploaders. | `VKWrapper.cpp:2760-2788`; `glShader.cpp:470-482` |
| Compilation/reflection | Both paths generate SPIR-V for reflection. Vulkan creates shader modules from it. OpenGL either compiles generated GLSL or ingests SPIR-V, depending on `useSPIRVForOpenGL` (whose configuration default is true). | `ShaderProgram.cpp:1996-2075`; `GLSLToSPIRV.cpp:168-257`; `glShader.cpp:162-200`; `vkShaderProgram.cpp:67-86`; `Configuration.h:50` |
| Caches | Generated GLSL, SPIR-V, and reflection are separated by build type and graphics API, with effect/atom modification-time checks. | `ShaderProgram.cpp:277-370` |

### Descriptor mapping

The logical Vulkan set numbers are `PER_FRAME=0`, `PER_PASS=1`, `PER_BATCH=2`, and `PER_DRAW=3` (`RenderAPIEnums.h:75-81`); `PER_DRAW` must remain last.

`DESCRIPTOR_SET_RESOURCE(PER_PASS, 9)` is **not** translated into an arithmetic binding such as 29 on OpenGL. Token concatenation selects the generated `PER_PASS_9` macro, whose value comes from the registered binding table. Texture, image, and buffer slots are assigned by the engine; compute-only buffers also have a separate allocation counter. Vulkan retains the logical set and slot.

Definitions and registration: `ShaderProgram.cpp:418-455,1240-1275`; `GFXDevice.cpp:231-279`. OpenGL SPIR-V reflection maps flattened bindings back to logical sets/slots using the resource type: `GLSLToSPIRV.cpp:328-344`.

| Logical resource | Vulkan | Current OpenGL binding |
|---|---|---|
| Draw texture 0 | set 3, binding 0 | texture unit 0 |
| Draw storage image | set 3, binding 12 | image unit 0 |
| Generated uniform blocks | set 3, bindings 14/15 | UBO bindings 1/2 |
| Camera block | set 2, binding 1 | UBO binding 3 |
| Local-light data | set 1, binding 9 | SSBO binding 10 |
| Scene block | set 0, binding 8 | UBO binding 14 |

These are values from the current registration order, not a stable shader-authoring ABI. Author shaders against logical macros, not these flattened numbers.

### Coordinate and depth conventions

Both backends use zero-to-one depth. OpenGL explicitly calls `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` (`GLWrapper.cpp:348`), and the camera projection builders default to `zeroToOneDepth=true` (`Camera.h:248-253`; `Camera.inl:235-260`). Vulkan uses a depth range of zero to one.

OpenGL is the orientation reference. Vulkan offscreen targets use a positive-height viewport, so texel row 0 is clip-space bottom and `gl_FragCoord`, sampling, copies and readbacks match OpenGL; only the swapchain uses a negative-height viewport (`VK_API::setViewportInternal`). Shaders therefore need no `TARGET_VULKAN` Y flips, and none remain. See `docs\architecture\gpu-resources.md`.

Consequently, a shared old-style minus-one-to-one depth reconstruction is a bug on **both** APIs, not just Vulkan.

## Shader map

| Family | Effects / shared code | Role |
|---|---|---|
| Scene geometry | `material`, `depthPass`, `baseVertexShaders`; `VertexAtoms`; `Common\node*` | Vertex fetch, skinning, transforms, material indirection, depth/prepass, forward shading |
| Lighting/material | `FragmentAtoms\lightingCalc`, `pbr`, `specGloss`, `BRDF`, `IBL`, `shadowMapping`, `shadowUtils`, `vsm`, `texturing`, `bumpMapping`, `materialData`, `output`, `prePass`, `velocityCalc` | BRDF evaluation, shadowing, surface construction, lighting, MRT/OIT output |
| Clustered lighting | `lightBuildClusteredAABBs`, `lightCull`; `Common\lightInput` | Build cluster bounds, classify lights, generate cluster index lists |
| Visibility | `HiZConstruct`, `HiZOcclusionCull`, `instanceCullVegetation`; `Common\HiZCullingAlgorithm` | Hierarchical depth reduction, indirect-command culling, vegetation visibility/LOD |
| Environment | `terrainPlane`, `terrainTess`, `grass`, `tree`, `water`, `sky`; terrain/vegetation/water atoms | Terrain tessellation and splatting, instanced vegetation, water, sky/clouds |
| IBL preprocessing | `irradianceCalc`; `ComputeAtoms\pbrHelpers` | Environment convolution, prefiltering, BRDF lookup generation |
| Screen-space effects | `SSAOPass`, `ScreenSpaceReflections`, `DepthOfField`, `EdgeDetection`, `blur`, `bloom` | AO, ray marching, blur, focus, bloom |
| Exposure/composition | `luminanceCalc`, `toneMap`, `postProcessing`, `OITComposition` | Histogram/exposure, HDR-to-display transform, final composition |
| Presentation/debug | `FXAA`, `SMAA`, `display`, `fbPreview`, `IMGUI`, `InfiniteGrid`, `ImmediateModeEmulation`, `passThrough`, `particles`, `lightImpostorShader`; debug/utility atoms | Antialiasing, previews, overlays, particles and light visualization |

## Second audit: remaining correctness issues

These are source and host-contract findings, not claims of observed rendering
failures. The conditions noted below matter. Earlier C1-C11 fixes remain in
place; R1 has since been repaired, while the other findings remain open.

### R1. Fixed: the tree vertex shader did not compile

**Evidence:** `Assets\Shaders\GLSL\tree.glsl:25-37`;
`Source\Platform\Video\Shaders\ShaderProgram.cpp:464`;
`Assets\Shaders\GLSL\instanceCullVegetation.glsl:43,88`.

The generated `_LoDLevel` varying is `flat uint`, but the tree shader assigned
the floating-point `data.data.z` directly to it. Representative OpenGL and
Vulkan glslang probes rejected the assignment, with and without
`HAS_CULLING_OUT`. The tree material selects this vertex shader for both depth
and colour passes.

**Status: fixed.** The shader tests the negative cull sentinel before converting
the nonnegative LOD to `uint`, maps culled instances to LOD 3, and uses the
existing cull-distance or zero-scale path. The LOD comparison now uses integer
values. Regression coverage compiles tree vertices with and without
`HAS_CULLING_OUT` for both APIs and asserts the sentinel check precedes the
conversion.

### R2. High: HiZ culling effectively samples the base mip

**Evidence:** `Source\Platform\Video\GFXDevice.cpp:782-789`;
`Assets\Shaders\GLSL\Common\HiZCullingAlgorithm.cmn:115-125`;
`OpenGL\Textures\glSamplerOject.cpp` (`getMinFilterMode`);
`Vulkan\Textures\vkSamplerObject.cpp:20-28`.

The shader selects a coarse mip and samples four corners, but the HiZ sampler
uses `TextureMipSampling::NONE`. OpenGL selects non-mipmapped `GL_NEAREST`;
Vulkan clamps the LOD to 0-0.25 with nearest mip selection. The queries thus
sample mip zero instead of the conservative maximum for the projected area.
Far/background depth inside the rectangle can be missed, causing false
occlusion despite the corrected HiZ construction in C4.

**Correction:** Enable nearest-mip sampling for HiZ, retain nearest texel
filtering and suitable LOD limits, and verify conservative queries across mip
levels on both APIs.

### R3. High: vegetation's buffer barrier is recorded before its compute writes

**Evidence:** `Source\Environment\Vegetation\Vegetation.cpp:687-716`.

The `MemoryBarrierCommand` is enqueued before the grass/tree dispatches; buffer
locks are added to that already-enqueued command afterward. There is no matching
post-dispatch barrier for those buffers in this package. In particular, Vulkan
buffer barriers are resource-specific, so an unrelated barrier cannot be
assumed to make the culling writes visible to later vertex reads. OpenGL may
incidentally encounter broader barriers on some paths, which is not a reliable
dependency.

**Correction:** Record the appropriate compute-write-to-vertex-read dependency
after all relevant dispatches and before the consuming draw, using the actual
buffer ranges and backend synchronization contracts.

### R4. High: custom-pass HiZ is constructed from the main screen depth

**Evidence:** `Source\Rendering\RenderPass\RenderPassExecutor.cpp:1275-1279,1348-1354`;
`Source\Environment\Water\Water.cpp:415-430,508-524`;
`Source\Environment\Vegetation\Vegetation.cpp:65-66,644-646`.

When a custom pass has a depth attachment and HiZ target, `occlusionPass` is
always given `RenderTargetNames::SCREEN` as its depth source instead of that
pass's target. Water reflection/refraction passes supply their own targets and
cameras, so their occlusion data can describe a different view. Vegetation
independently chooses main-view HiZ for refraction; however, vegetation is
currently excluded from reflection and refraction by default, so that
particular mismatch matters if those exclusions change.

**Correction:** Construct and consume each pass's HiZ from its own depth target,
viewport and camera, and make any per-reflector lifetime/sharing policy explicit.

### R5. High: tree draws do not receive their chunk offset

**Evidence:** `Assets\Shaders\GLSL\tree.glsl:7-8`;
`Source\Environment\Vegetation\Vegetation.cpp:547-572,614,668`;
`Assets\Shaders\GLSL\Common\vegetationData.cmn:4`.

The offset is supplied to the vegetation parent's draw and culling packages,
but tree meshes are separate child draw packages and no corresponding tree
uniform update was found. The loose-uniform initializer is discarded during
engine packing, so it is not a dependable fallback. With R1 fixed, tree draws
can index the wrong chunk's instance data.

**Correction:** Pass the chunk ID explicitly to each tree draw, or expose it
through an existing per-node data contract, and check multiple chunks.

### R6. High: cached shadow-property slots can alias and remain stale

**Evidence:** `Source\Rendering\Lighting\LightPool.cpp:253-289,415`;
`Source\Rendering\Lighting\Headers\Light.inl:64-77`.

A cache-valid light is skipped before the per-type property-slot counter is
advanced. For example, cached light A can retain slot 0 while a later
invalidated light B is assigned slot 0 and overwrites A's properties. A light
that stops casting shadows frees its map but retains its nonnegative
`shadowPropertyIndex`, which light upload still advertises. Cache-valid paths
are reachable because successful generation clears the dirty flags.

**Correction:** Maintain unique, stable property ownership for both cached and
regenerated lights, and invalidate the advertised index whenever a light no
longer has a valid shadow.

### R7. High: directional shadow-cache validity ignores camera changes

**Evidence:** `Source\Rendering\Lighting\LightPool.cpp:504-536`;
`Source\Rendering\Lighting\ShadowMapping\CascadedShadowMapsGenerator.cpp:378-385`.

The cache test ignores its camera-position argument and checks light/scene
dirty state, while CSM splits and matrices are computed from the player camera
only when the directional shadow is rendered. In a static scene, camera motion
or projection changes can leave cached cascade transforms and coverage stale.

**Correction:** Track the camera-dependent inputs needed by CSM and invalidate
or update the cascades when their coverage changes, including projection changes.

### R8. Medium: the shadow budget is consumed before eligibility is known

**Evidence:** `Source\Rendering\Lighting\LightPool.cpp:268-284`;
`Source\EngineConfig.h:153-159`.

The global counter is incremented before the per-type limit and frustum checks,
and `>=` breaks on the twelfth shadow-casting light when the configured budget
is twelve. Out-of-frustum lights can also consume the budget and prevent later
visible lights from being considered.

**Correction:** Apply eligibility before consuming a slot and allow the full
configured budget, while accounting for both cached and regenerated shadows.

### R9. High: default spotlight angles invert the shader's attenuation

**Evidence:** `Source\ECS\Components\Headers\SpotLightComponent.h:44-45`;
`Source\ECS\Components\SpotLightComponent.cpp:59-65`;
`Assets\Shaders\GLSL\FragmentAtoms\lightingCalc.frag:100-110`.

The default inner angle is 35 degrees and outer angle is 15 degrees. The
attenuation formula assumes the opposite ordering: with these defaults it is
zero on-axis, about 0.681 at 30 degrees, and one at 35 degrees. This also
breaks the culling assumption that emitted light lies inside the outer cone.
The editor callback can make the angles equal, for which the formula divides
by zero.

**Correction:** Establish and enforce ordered angle invariants at creation,
load and editing, and define the equal-angle/hard-cone case explicitly.

### R10. High: CPU spotlight bounds disagree with the GPU light volume

**Evidence:** `Source\Rendering\Lighting\Light.cpp:194-201`;
`Source\ECS\Components\SpotLightComponent.cpp:73-84`;
`Assets\Shaders\GLSL\Common\lightInput.cmn` (`getPositionAndRangeForLight`).

For outer angles above 45 degrees, the CPU uses `range * tan(angle)` as both
the sphere radius and its forward offset; above 90 degrees that radius becomes
negative. These bounds participate in shadow frustum selection and cache
invalidation, even though C11 corrected the GPU bound. For range 1, inner
59.5 degrees and outer 60 degrees, a point at angle 59.55 degrees and distance
1.85 has positive shader attenuation but lies outside the CPU sphere.

**Correction:** Use a shared conservative bound consistent with the actual
attenuation domain, and define/validate the supported wide-angle behavior.

### R11. High, conditional: light uploads exceed capacity or write zero bytes

**Evidence:** `Source\Rendering\Lighting\LightPool.cpp:387-398,477-498`;
`Source\Rendering\Lighting\Headers\LightPool.h:215-221`;
`Source\Platform\Video\Buffers\ShaderBuffer\ShaderBuffer.cpp:82-89`.

The light count is incremented before checking the 4096-light limit. With 4097
eligible lights, `uploadLightList` returns 4097 after populating only 4096
entries, so the subsequent upload exceeds the stage's source array and buffer
range. With no eligible lights, the unconditional upload requests a zero-length
write, violating `ShaderBuffer::writeBytes`' assertion.

**Correction:** Cap the returned count before incrementing, skip empty light
buffer writes, and still upload scene data with zero light counts.

### R12. Medium, conditional: single-instance vegetation disagrees with instancing state

**Evidence:** `Source\Environment\Vegetation\Vegetation.cpp:344,359`;
`Source\ECS\Components\RenderingComponent.cpp:428-430,595-613`;
`Source\Geometry\Material\Material.cpp:859-861`;
`Assets\Shaders\GLSL\Common\nodeBufferedInput.cmn:12-29`.

Vegetation materials force `OVERRIDE_DATA_IDX`, but a rendering component is
marked instanced only for a draw with more than one instance. A one-instance
draw therefore does not upload the override indirection index and receives a
nonzero base instance. Vulkan's `gl_InstanceIndex` includes that base, while
OpenGL's `gl_InstanceID` does not, so the vegetation-data index can also differ
between backends.

**Correction:** Make the material, draw and indirection contracts agree even
for one instance, and define a backend-independent local instance index.

### R13. Medium, Vulkan-specific: debug light impostors lack vertex visibility

**Evidence:** `Assets\Shaders\GLSL\lightImpostorShader.glsl:3,10`;
`Source\Platform\Video\GFXDevice.cpp:264`;
`Source\Platform\Video\Headers\RenderAPIEnums.h:469`;
`Vulkan\VKWrapper.cpp:3355-3374,3405-3412`.

The impostor vertex stage reads the light buffer at `PER_PASS` binding 9, but
its shared Vulkan set layout uses `COMPUTE_AND_DRAW`, which actually means
compute plus fragment only. Enabling this debug path can therefore create an
invalid pipeline layout; per-program reflection only augments `PER_DRAW`.

**Correction:** Include vertex visibility for the shared binding, and audit
callers against actual stage flags rather than the enum's misleading name.

### R14. Medium, conditional: zero lights per cluster is accepted but invalid

**Evidence:** `Source\Platform\Video\GFXDevice.cpp:421-429`;
`Source\Platform\Video\Shaders\ShaderProgram.cpp:625`;
`Assets\Shaders\GLSL\lightCull.glsl:52-53`;
`Source\Rendering\Renderer.cpp:84-86`.

Configuration accepts zero and emits it as `MAX_LIGHTS_PER_CLUSTER`; the shader
then declares a zero-sized local array and the host allocates a zero-sized
index buffer. OpenGL and Vulkan glslang probes both reject the array. The
checked-in XML uses 100, so this is not a default-configuration failure.

**Correction:** Reject or clamp zero to a supported positive limit, or implement
and test an explicit no-local-lights mode without zero-sized resources.

### R15. Medium, OpenGL-specific: the exposure histogram starts uninitialized

**Evidence:** `Source\Rendering\PostFX\PreRenderBatch.cpp:339-347`;
`Assets\Shaders\GLSL\luminanceCalc.glsl:42-61,80-85`;
`Source\Platform\Video\RenderBackend\OpenGL\Buffers\glMemoryManager.cpp:88-103,313-321`.

The histogram buffer is created without initial data and the first histogram
pass atomically adds to it. OpenGL allocation, including pooled reuse, does not
guarantee zeroed contents. The averaging pass resets the bins afterward, but
the first computed exposure can already be wrong.

**Correction:** Explicitly initialize/reset the histogram before its first use
and whenever its lifetime is restarted, with the appropriate GPU dependency.

### R16. Medium, conditional: nonpositive orthographic near planes break clustering

**Evidence:** `Assets\Shaders\GLSL\lightBuildClusteredAABBs.glsl:31-35`;
`Source\Platform\Video\GFXDevice.cpp:2016-2035`;
`Source\Rendering\Camera\Headers\Camera.inl:208-229`.

Cluster construction uses fractional powers of `zFar / zNear` without guarding
nonpositive near planes, whereas the host selects fallback scale/bias values
for that case. Orthographic camera construction permits such planes, but it
has not been established that the existing utility cameras using them enter a
clustered-lighting pass.

**Correction:** Either enforce positive near/far values for clustered cameras
or implement matching linear/other valid slices in both builder and lookup.

### R17. Medium: light constructor ranges are ignored

**Evidence:** `Source\Rendering\Lighting\Light.cpp:40-45`;
`Source\Rendering\Lighting\Headers\Light.h:117-118`;
`Source\ECS\Components\PointLightComponent.cpp:13-15`;
`Source\ECS\Components\SpotLightComponent.cpp:13-15`.

`Light` ignores its `range` constructor argument and leaves the property at
10. Point and spot constructors pass 20 and 30 respectively, so their
unmodified ranges are not the requested values. Directional lights explicitly
set their range afterward.

**Correction:** Initialize the property from the constructor argument and check
the intended defaults and serialized-value migration behavior.

### R18. Medium, device-dependent: compute workgroup limits are not handled consistently

**Evidence:** `Source\EngineConfig.h:173-182`;
`Source\Platform\Video\GFXDevice.cpp:96,411-417,994-996`;
`Assets\Shaders\GLSL\HiZConstruct.glsl` (`local_size_x/y`).

The clustered shader uses 512 invocations, but startup checks use strict `<`
against device maxima and therefore reject an exactly matching limit when the
assertions are active. Separately, HiZ is compiled with a fixed 32x32 group
(1024 invocations), without validating that size and each dimension against
the device's compute limits. A Vulkan-capable device can have lower limits.

**Correction:** Compare legal maxima inclusively, validate every dispatched
workgroup shape, and choose a supported fallback or report the requirement
explicitly.

## Confirmed defects: shared compute and integration

Fixed entries below describe their original pre-repair behavior; their historic
line references may no longer correspond to the same statements after the
repairs. The second-audit entries above refer to the current rebased code.

### C1. High: clustered-light shared batches are not actually processed per batch

**Locations:** `Assets\Shaders\GLSL\lightCull.glsl:55-89`; workgroup dimensions in `Source\EngineConfig.h:173-178`.

The shader loads every light batch into the same `sharedLights` array, then processes all batches only after loading has finished. Earlier batches have already been overwritten. Invalid lanes are never set to disabled, so a partially filled batch also reads uninitialized or stale entries.

The current group has 512 invocations. With 513 lights, the shared array ends as `[512,1,2,...,511]`; the loop emits light ID 0 using light 512's bounds, and later emits IDs 513+ using old bounds. With fewer than 512 lights, the unused lanes' `enabled` values are undefined.

**Correction:** Load one batch, explicitly initialize invalid lanes, synchronize, test only that batch's valid entries, then synchronize before reusing shared storage. Retain the correct original index and type together.

**Status: fixed.** Each batch is loaded, fenced by `barrier()` on both sides, and only its `min(GROUP_SIZE, lightCount - batchStart)` valid entries are tested (sphere-vs-AABB by closest-point clamp).

### C2. High: cluster light capacity is never enforced

**Locations:** `lightCull.glsl:52-53,84-106`; backing allocation in `Source\Rendering\Renderer.cpp:84-90`.

`visibleLightCount` is incremented without testing `MAX_LIGHTS_PER_CLUSTER`. More than the configured maximum of one light type overruns the local array. Independently, the shader permits up to that maximum for both point and spot lights, but the global index buffer is allocated for only one maximum per cluster, not twice that value.

**Correction:** Define and enforce a total per-cluster budget, with point and spot counts whose sum never exceeds it. Keep the emitted indices and advertised counts consistent; make overflow policy explicit.

**Status: fixed.** One `MAX_LIGHTS_PER_CLUSTER` array is shared: point lights fill from the front, spot lights from the back, and both stop when they meet. Overflowing lights are dropped. Output is points then spots, matching the grid's two counts and the index-buffer allocation.

### C3. High: depth linearization does not match the engine's projection

**Locations:** `Common\utility.cmn:55`; cluster consumer `Common\lightInput.cmn:100-102`; also SSAO and depth previews.

`ToLinearDepth` implements the formula for a minus-one-to-one NDC depth input but receives raw zero-to-one depth. With near=0.1 and far=1000, a surface at view distance 10 reconstructs as approximately 19.802. Cluster lookup therefore chooses a different Z slice than the one used to build the cluster bounds.

**Correction:** For ordinary zero-to-one perspective depth, use `near * far / (far - depth * (far - near))`, or consistently reconstruct with the inverse projection. Give orthographic reconstruction its own path.

**Status: fixed.** `ToLinearDepth` uses the zero-to-one perspective formula. Cluster lookup uses the new `ViewDistanceFromDepth(depth, projection)`, which is exact for both perspective and orthographic projections. Note that SSAO's range threshold (`SSAOPass.glsl`) now compares against true view distance, which is roughly half the previous value for the same surface; retune `SSAO` max-range settings if occlusion reach changes visibly.

### C4. High: even HiZ reduction uses a shifted source footprint

**Locations:** `HiZConstruct.glsl:11-26`; mip dispatch/setup `GFXDevice.cpp:2362-2410`.

For an 8-to-4 reduction, the current centered coordinate becomes 1,3,5,7 and the offsets sample `[1,2]`, `[3,4]`, `[5,6]`, `[7,7]`. The correct nonoverlapping pairs start at 0,2,4,6.

The base level's conservative 3x3 dilation does not fix the whole pyramid. A source row `[1,0,0,0,0,0,0,0]` becomes base `[1,1,0,0,0,0,0,0]`, mip 1 `[1,0,0,0]`, then mip 2 `[0,0]`: far/background depth has vanished. This permits false occlusion.

**Correction:** Use a footprint covering the entire destination texel's source region. For exact even reductions, start at `2 * outputPosition`; handle odd dimensions explicitly and make the base-level operation intentional.

**Status: fixed.** Each output texel max-reduces its full source footprint `[p*in/out, ceil((p+1)*in/out) - 1]`, which handles even and odd sizes alike. The base level (flagged by push data `isBaseLevel`) is deliberately dilated by one texel on each side. The host starts from the real depth-texture size instead of a rounded pyramid size.

**Remaining:** The HiZ sampler still prevents the culler from using the
constructed mip chain (R2).

### C5. High for vegetation: padded culling invocations do not stop at valid items

**Locations:** `HiZOcclusionCull.glsl:61-65`; `instanceCullVegetation.glsl:56-61`; host dispatch `GFXDevice.cpp:2436-2437,2480`; `Vegetation.cpp:688,703`.

The out-of-range branches call `CullItem` instead of simply returning. For example, 65 draw commands dispatch 128 invocations, and indices 65-127 still write `instanceCount=0` despite being outside the active command count. For ordinary indirect culling, the host binds a full 8192-command capacity (`RenderPassExecutor.cpp:774-778`; `EngineConfig.h:121`), so the current rounded dispatch alone does **not** establish a physical buffer overrun. That part is unnecessary tail modification and misleading accounting, not a proven adjacent-allocation corruption.

Vegetation has an additional host-side alignment mistake: `_maxGrassInstances += _maxGrassInstances % WORK_GROUP_SIZE` (same for trees) is not a round-up to the work-group size (`Vegetation.cpp:307-308`). With 65 generated instances, capacity becomes 66 while 128 invocations are dispatched. Here the excess lanes really can write into the next chunk or beyond the last allocation. Separately, the shader is not given the actual active count, so capacity-padding entries can be processed as if they were real instances.

**Correction:** Return without item writes for every out-of-range invocation. Use correct capacity round-up on the host and pass the actual active instance count separately from chunk capacity.

**Status: fixed.** Padding lanes only bump the padding statistic and return. Vegetation capacity is rounded up to a multiple of `WORK_GROUP_SIZE`, and `dvd_grassInstanceCount` / `dvd_treeInstanceCount` bound the shader's work.

**Open notes (not changed):** Vegetation still needs the post-dispatch
dependency in R3. `HiZOcclusionCull` still returns early when
`dvd_numEntities < WORK_GROUP_SIZE`, so small command lists are never
occlusion-culled; this is conservative but probably unintended. In
`HiZCullingAlgorithm.cmn`, `pos.x < -pos.w * 0x01` reads as a bit test but is
plain multiplication by one; drop the `* 0x01` for clarity.

### C6. High, Vulkan-specific: vegetation compute reads a draw-only descriptor

**Locations:** `instanceCullVegetation.glsl:29-31,81`; `Common\sceneData.cmn:20-33`; `Source\Platform\Video\GFXDevice.cpp:278`; `VKWrapper.cpp:3355-3374,3405-3412`.

Vegetation compute accesses `dvd_waterEntities` through `IsUnderWater`, but the containing scene UBO (`PER_FRAME`, slot 8) is registered with `ALL_DRAW`, excluding compute. Vulkan copies those stage flags into the descriptor set layout. Reflection only augments `PER_DRAW` layouts, so it does not repair this registration.

**Correction:** Include compute visibility for the scene block, or move the required data to a compute-visible block. The descriptor must also be bound consistently for the compute pipeline.

**Status: fixed.** The scene block is registered with `ShaderStageVisibility::ALL`. It is not compute-only, so its OpenGL binding (UBO 14) is unchanged.

### C7. Fixed: SPIR-V target was incompatible with baseline OpenGL ingestion

**Locations:** `Source\Platform\Video\Shaders\GLSLToSPIRV.cpp:180-185`; `OpenGL\Shaders\glShader.cpp:124-139`.

The compiler unconditionally emits SPIR-V 1.6. OpenGL's standard SPIR-V environment is based on SPIR-V 1.0; the optional `useSPIRVForOpenGL` path feeds the same binary directly to `glShaderBinary`.

A minimal OpenGL-target shader generated with SPIR-V 1.6 is rejected by `spirv-val --target-env opengl4.5` with an invalid-binary-version error. This establishes the target mismatch, not behavior on every vendor driver. The textual GLSL path is not affected in the same way. The ingestion flag's configuration default is true (`Configuration.h:50`), so this is not necessarily a rarely selected option.

**Correction:** Use explicit per-backend target environments: OpenGL-compatible SPIR-V for the optional GL path, and Vulkan 1.4 / SPIR-V 1.6 for Vulkan. `setEnvInput` and `setEnvClient` are also currently always set to OpenGL despite the conditional Vulkan parsing rules; make the client selection consistent.

### C8. Medium: initial/invalidated cluster rebuild does not capture current camera data

**Locations:** `Source\Rendering\Renderer.cpp:222-254`; initial invalidation in `Headers\Renderer.h:67-79`.

`tempData` is constructed and copied into `_gridData` only when `needRebuild` initially evaluates false. On the first build, or any explicitly invalidated build, the shader instead receives default/uninitialized/stale `_gridData` values.

**Correction:** Capture the current inverse projection, viewport and Z planes unconditionally; then decide whether a rebuild is required and commit that data before sending push constants.

**Status: fixed.** The current grid data is always captured and stored; `needRebuild` additionally compares it with the previous data.

### C9. Medium: cluster lookup is not clamped and assumes a zero-origin viewport

**Locations:** `Common\lightInput.cmn:100-110`; `lightBuildClusteredAABBs.glsl:28-29,81-83`; cluster sizing `GFXDevice.cpp:1912-1918`.

The far plane can evaluate to Z index `CLUSTERS_Z`, which is outside the grid. XY lookup divides absolute `gl_FragCoord.xy` by cluster size without subtracting viewport origin; a sufficiently offset viewport can generate columns/rows outside the allocated dimensions. The builder also generates grid screen coordinates without adding the viewport origin before subtracting it in `screen2View`.

**Correction:** Use viewport-relative coordinates consistently, add the origin when constructing absolute screen points, and clamp every grid component to its legal maximum.

**Status: fixed.** Lookup and builder both work in viewport-relative coordinates; XY tiles are clamped in float space and Z to `CLUSTERS_Z - 1`. The builder now unprojects tile corners at depth 0 and 1 and intersects them with the exponential slice planes.

### C10. Medium: vegetation culling applies scale twice to the center offset

**Location:** `instanceCullVegetation.glsl:66-72`.

`extents` already includes `scale`; the local vertical offset is formed from those extents and then multiplied by `scale` again before rotation. Its displacement scales quadratically while the culling radius scales linearly. This can move the culling bounds away from rendered vegetation.

**Correction:** Construct a local-space center and scale it exactly once. Derive transformed center and conservative extents from the same transform used by rendering.

**Status: fixed.** The centre offset is built from the already-scaled extents and only rotated.

### C11. Medium: spotlight culling geometry is quantized down to an integer

**Locations:** `Source\Rendering\Lighting\LightPool.cpp:413`; `ECS\Components\SpotLightComponent.cpp:80-83`; `Common\lightInput.cmn:43-50`.

The host converts floating-point cone slant height to `I32`; the shader uses that integer to construct the culling sphere. A positive slant height below one becomes a zero-radius sphere; larger lights are also rounded down, making bounds nonconservative.

**Correction:** Preserve this geometric value as a float, or derive conservative bounds directly from floating-point range and cone parameters. If integer packing is retained, rounding down is not safe for a bounding volume.

**Status: fixed.** The host packs the slant height with `std::bit_cast<I32>` and shaders read it via `SpotConeSlantHeight`. Culling uses the minimal sphere enclosing the spherical sector (apex-and-rim sphere for narrow cones, rim-equator sphere for wide ones).

**Remaining:** Spotlight defaults and CPU bounding volumes still need the
separate corrections in R9 and R10.

### C12. Fixed, latent: several easing macros could not be instantiated as GLSL expressions

**Location:** `Common\utility.cmn:35-43`.

The Back, Elastic, and Bounce macros expand declarations/statements inside parenthesized expressions. Nine easing expansions fail a minimal GLSL 460 compiler check: `easeInBack`, `easeOutBack`, `easeInOutBack`, `easeInElastic`, `easeOutElastic`, `easeInOutElastic`, `easeOutBounce`, `easeInBounce`, and `easeInOutBounce`.

No current shader references to those names were found outside their definitions, so these are latent helper defects rather than evidence of a current rendering failure.

**Correction:** Use parenthesized expression-only defines, with side-effect-free
arguments because macros may evaluate them repeatedly. Instantiating
`easeInOutEXpo` also exposed an invalid `2f` literal, which was corrected.

## Confirmed defects: post-processing and presentation

Paths in this section are relative to `Assets\Shaders\GLSL` unless otherwise qualified.

### P1. High: luminance compute barriers do not synchronize execution

**Locations:** `luminanceCalc.glsl:42-61,80-95`; workgroup configuration `Source\Rendering\PostFX\PreRenderBatch.cpp:28-29,253-256`.

`groupMemoryBarrier()` orders memory accesses but is not a workgroup execution rendezvous. Shared histogram initialization can race accumulation, and reduction steps can read another invocation's value before that invocation has updated it. These are 256-invocation groups, not a portable single-subgroup operation.

**Correction:** Use `barrier()` after initialization, after accumulation, and between dependent reduction steps. Keep every rendezvous in uniform control flow.

### P2. Withdrawn: fullscreen Vulkan Y-flip claim

The rebased backend uses positive-height Vulkan viewports for offscreen
targets, matching OpenGL's texture orientation; negative height is used for
the swapchain only (`VKWrapper.cpp:3277-3303`). The previously asserted
Vulkan-only fullscreen UV inversion is not warranted. The separate SMAA
initialization and quality-index issues in P3 and P4 remain.

### P3. High: SMAA uses a varying before assigning it

**Location:** `SMAA.glsl:19-25`; consumers at `460,501`.

`vPixCoord` is calculated from `VAR._texCoord` before the UV is assigned. Later distance calculations use this undefined value for area-texture lookup.

**Correction:** Assign the fullscreen UV first, then calculate pixel coordinates and offsets.

### P4. High: SMAA quality level 5 exceeds the shader tables

**Locations:** `SMAA.glsl:68-69,420-421`; editor range `Source\Editor\Widgets\DockedWindows\PostFXWindow.cpp:173-175`.

The shader tables have four entries, but the UI allows quality levels 0-5 and the caller sends `qualityLevel - 1`. Level 5 indexes element 4.

**Correction:** Define the supported ranges explicitly per AA algorithm, share the contract with C++, and clamp before indexing.

### P5. High: SSAO downsampling overruns and incompletely initializes local arrays

**Location:** `SSAOPass.glsl:301-326`.

With four equal depths, all samples can enter `rejected_idx[3]`, overflowing the array. The repair loop also starts at `rem_samples[3]` and reads one past the populated rejection list. A two-way maximum-distance tie can leave a retained sample uninitialized.

**Correction:** Use bounded selection/sorting that always initializes exactly the required retained samples. Explicitly cover equal-depth and tied-depth cases.

### P6. High: bloom restores its upsample pipeline only after a settings change

**Location:** `Source\Rendering\PostFX\CustomOperators\BloomPreRenderOperator.cpp:236-246`.

The upsample pipeline bind and push-data submission are conditional on `_filterRadiusChanged`. On a later frame, downsampling has already rebound the downsample pipeline and overwritten the push data. Unchanged settings skip the commands required to restore the upsample state.

**Correction:** Bind the upsample pipeline and submit its data on every bloom execution. Cache the CPU calculations, not the command emission.

### P7. High: OIT composition does not define the complete normal output

**Locations:** `OITComposition.glsl:59-63`; normal attachment enabled at `Source\Rendering\RenderPass\RenderPassExecutor.cpp:1108-1112`; blend state at `310-319`.

For revealage between 0.5 and 1, colour is written without assigning `_normalsOut`; below 0.5, only `.rg` is assigned, leaving roughness `.b` undefined. The normal attachment is enabled, and only the albedo attachment has blending enabled. Unwritten outputs do not preserve old attachment contents.

**Correction:** Define the complete output and explicitly design how existing normals/roughness are preserved or composed, using attachment blending, masks, or a separate pass as appropriate.

### P8. High on OpenGL: exposure adaptation reads a write-only image

**Locations:** `luminanceCalc.glsl:111-115`; `Source\Rendering\PostFX\PreRenderBatch.cpp:630-652`; OpenGL access translation `GLWrapper.cpp:1787-1789`.

Adaptation loads the previous image value, but C++ binds/transitions the image as `SHADER_WRITE`, which maps to `GL_WRITE_ONLY` on OpenGL.

**Correction:** Use `SHADER_READ_WRITE` and matching synchronization for the read-modify-write operation. Correct Vulkan's access description too; a permissive image layout is not a substitute for the right access contract.

### P9. High: black HDR input can produce NaNs in tone mapping

**Location:** `toneMap.glsl:114-124,138-172`.

RGB zero produces an XYZ sum of zero. The chromaticity conversion divides by that sum and multiplies zero by infinity; RGB reconstruction then divides by the undefined chromaticity.

**Correction:** Handle black/near-zero luminance explicitly, or use guarded luminance-based RGB rescaling.

### P10. Medium: grid depth is remapped a second time

**Locations:** `InfiniteGrid.glsl:64`; helper `FragmentAtoms\utility.frag:49-51`.

`computeDepth` maps projected depth with an old-style `0.5*z + 0.5`, although projected depth is already zero to one. A correct value of 0.2 becomes 0.6, changing depth ordering.

**Correction:** Map zero-to-one projected depth directly into the requested depth range.

### P11. Medium: DoF has a missing comma and a mismatched noise define

**Locations:** `DepthOfField.glsl:139-141,186`; `Source\Rendering\PostFX\CustomOperators\DoFPreRenderOperator.cpp:35,38-39`.

`vec2(wh.x -wh.y)` splats one subtraction into both components instead of specifying `(wh.x, -wh.y)`. On square textures it duplicates a center tap. The actual caller enables this depth-blur path.

Separately, C++ defines `USER_NOISE` while the shader checks `USE_NOISE`, silently bypassing the requested noise branch. The caller also defines `FIRST_RING_SAMPLES` twice.

**Correction:** Restore the comma, align the noise define, and remove the duplicate definition.

### P12. Medium: DoF vignette calls `smoothstep` with reversed edges

**Locations:** `DepthOfField.glsl:205-210`; defaults `Source\Core\Headers\Configuration.h:183-184`.

The default outer boundary is 1.3 and inner boundary is zero, but they are supplied in reverse order. GLSL leaves `smoothstep` undefined when `edge0 >= edge1`.

**Correction:** Use one minus an ascending-edge smoothstep, and validate distinct, ordered input boundaries.

### P13. Medium: a duplicated tone-map condition makes an option unreachable

**Locations:** `toneMap.glsl:160-163`; selection names `Source\Rendering\PostFX\Headers\PreRenderBatch.h:48-56,73-74`.

Two successive branches test `AMD_ACES`. The second should select `GT_DIFF_PARAMETERS`; that CPU-selectable option currently reaches no tone curve.

**Correction:** Correct the second selector and test every exposed tone-map enumeration value.

### P14. Medium: bloom's source-texel spacing is taken from the destination

**Locations:** `bloom.glsl:86-110`; `Source\Rendering\PostFX\CustomOperators\BloomPreRenderOperator.cpp:205-206`.

`invSrcResolution` is populated from the destination mip dimensions, even though the source is twice as large. Sampling offsets therefore span twice the intended source-texel distance, including the first downsample.

**Correction:** Derive spacing from the actual source view and mip.

### P15. Medium: bloom's soft-knee coefficient uses multiplication instead of reciprocal

**Locations:** `bloom.glsl:63-75`; `BloomPreRenderOperator.cpp:206`.

The quadratic knee needs `0.25 / knee`, but the caller supplies `0.25 * knee`. At knee=0.1 and brightness equal to threshold, the quadratic term becomes 0.00025 instead of 0.025.

**Correction:** Use the reciprocal with an explicit zero-knee policy, and correct the comments describing the packed values.

### P16. Medium: SSR intersection and reconstructed hits disagree with valid ray cases

**Locations:** `ScreenSpaceReflections.glsl:113-152`.

The intersection inequalities assume the ray moves farther from the camera. A segment from Z=-10 to Z=-8 crossing scene Z=-9 is rejected. Marching also advances the ray before testing, while the unrefined hit reconstruction uses one fewer step for XY than for Z/W, yielding an internally inconsistent hit position.

**Correction:** Order segment endpoints for depth-slab overlap; track actual ray advancement consistently through the coarse and refinement paths.

### P17. Medium: signed preview data is fed into fractional `pow`

**Locations:** `fbPreview.glsl:26-54`; actual preview consumers `GFXDevice.cpp:2599-2622`.

Decoded normals and signed velocities pass through `ToLinear`, implemented with `pow(rgb, 2.2)`. Negative components produce undefined values/NaNs rather than meaningful visualization.

**Correction:** Keep numerical data linear, explicitly remap signed values for display, guard zero-vector normalization, and initialize preview alpha.

### Additional conditional/latent post-processing issues

| Issue | Evidence / condition | Correction |
|---|---|---|
| 8K weighted histogram overflow | `luminanceCalc.glsl:82-95` sums weighted counts in `uint`. 7680x4320 pixels in bin 255 give 8,460,288,000, exceeding 32 bits. | Use an overflow-safe weighted reduction, such as floating-point accumulation. |
| SSAO upsample candidate can remain uninitialized | `SSAOPass.glsl:400-410`: `best_depth_idx` is not initialized; no candidate wins if all differences equal the initial threshold. | Initialize a valid candidate and a suitably large comparison distance. |
| Latent asymmetric SSAO box blur | `SSAOPass.glsl:137-138` scans `[-k,k)` rather than symmetric `[-k,k]`. Current C++ selects the Nvidia blur variant. | Correct the footprint if retaining the variant. |

The earlier histogram binding-mismatch claim is withdrawn: the averaging pass
now binds `PER_DRAW` slot 13, matching the shader (`PreRenderBatch.cpp:647-650`).

No current C++ references were found for `BloomApply`, `BloomCalc`, `passThrough.glsl`, or FXAA's geometry stage. Incomplete behavior in those variants should not be presented as an active-path regression without establishing another consumer.

## Confirmed defects: materials, geometry and environment

Paths are relative to `Assets\Shaders\GLSL` unless qualified with `Source`.

### M1. Fixed: the terrain control shader called an unavailable matrix overload

**Locations:** `terrainTess.glsl:154`; `Common\nodeDataDefinition.cmn:32-35`.

`Transform(dvd_terrainWorld, ...)` passes a matrix, but the available shader function accepts a `vec4`. This affects the ordinary terrain tessellation-control section.

**Correction:** Construct the equivalent node transform matrix with an inline
define and multiply in the intended order.

### M2. Fixed: velocity-enabled vertex code referenced an undeclared name

**Locations:** `VertexAtoms\vbInputData.vert:84-95`; define selection `Source\Geometry\Material\Material.cpp:750-755`.

`data_prevTransform` should be `data._prevTransform`. Non-static display materials enable `HAS_VELOCITY`, exposing the invalid function body even when a specialized vertex entry point does not call it.

**Correction:** Use the structure member and compile velocity-enabled material variants.

### M3. Shader compilation fixed; particle host integration remains broken

**Locations:** `particles.glsl:53-58,76-92`; consumers `Source\Dynamics\Entities\Particles\ParticleEmitter.cpp:209-247`.

`#ensif` is not `#endif`, leaving the conditional unterminated. The prepass also uses `texDepth` even though its declaration comes only from the OIT branch of `output.frag`; the non-prepass section uses material functions/data without including `materialData.frag`.

**Correction:** Repair the directive and provide the dependencies/depth input explicitly for every supported soft-particle variant.

### M4. High: CSM upload duplicates the first split and permits out-of-range lookup

**Locations:** `Source\Rendering\Lighting\LightPool.cpp:321-324`; `FragmentAtoms\shadowUtils.frag:37-47,71`; `shadowMapping.frag:50-53`.

The CPU copies `_lightPosition[0]` into every split entry rather than `_lightPosition[i]`. Fragments beyond that first uploaded split can make `getCSMSlice` return `MAX_CSM_SPLITS_PER_LIGHT`, followed by out-of-range matrix indexing.

**Correction:** Upload the correct split element and return a bounded fallback when no active split matches.

### M5. Fixed: selectable terrain debug sections did not compile/link correctly

**Locations:** `terrainTess.glsl:436-484,536,286,506-508`; selectors `Source\Environment\Terrain\Terrain.cpp:450-478`.

The geometry section references `ViewProjectionMatrix` instead of `dvd_ViewProjectionMatrix`; line 536 lacks a semicolon; tessellation-level colour output uses `ATTRIB_FREE_START + 4` while the matching fragment input uses `+3`.

**Correction:** Repair the names/punctuation and make varying locations match across stages.

### M6. Medium: sky IBL samples a view-space direction as world space

**Location:** `FragmentAtoms\IBL.frag:95-123`.

Local-probe directions are transformed by `dvd_InverseViewMatrix`; the sky branch returns `reflectionWV` unchanged, despite the sky cube being world-oriented. Camera rotation therefore changes the sampled environment incorrectly.

**Correction:** Transform the sky reflection vector into world space too.

### M7. Medium: diffuse irradiance depends on reflection direction instead of normal

**Location:** `FragmentAtoms\IBL.frag:147-152`.

The diffuse irradiance lookup uses the reflected/box-projected direction intended for specular. Diffuse environment lighting then varies with the view direction.

**Correction:** Sample irradiance using the world-space surface normal, with a separate reflected lookup for specular.

### M8. Medium: metallic-roughness shading ignores F0 and double-attenuates diffuse IBL

**Locations:** `FragmentAtoms\materialData.frag:210-216`; `pbr.frag:190-196`; `IBL.frag:157-160`; host source `Source\Geometry\Material\Material.cpp:1217-1237`, `Headers\Material.h:177`.

The shader calculates metallic/base-colour-derived `_F0`, but direct MR Fresnel uses legacy Phong `_specular.rgb`, whose default is black. Separately, diffuse IBL multiplies by `(1-metallic)` after that factor is already present in `_diffuseColour`.

**Correction:** Use the MR F0 for the MR Fresnel path and apply diffuse metal suppression exactly once.

### M9. Medium: opacity-map alpha is overwritten in the colour path

**Locations:** `FragmentAtoms\materialData.frag:290-302`; alpha-source flags `Source\Geometry\Material\Material.cpp:1220-1221,1252-1253`.

After sampling opacity, the function overwrites it with base alpha whenever `dvd_UseAlbedoTextureAlphaChannel` is false. That flag is false for opacity-map sources, so colour/OIT can disagree with prepass/shadows.

**Correction:** Make alpha-source selection mutually exclusive; do not overwrite a chosen opacity-map result.

### M10. Medium: shininess-to-roughness mapping runs in the wrong direction

**Location:** `FragmentAtoms\materialData.frag:162-166`.

For white specular, shininess zero becomes roughness zero, while shininess 255 becomes roughness one. This reverses sharpness in the normal/roughness buffer and downstream IBL/SSR use.

**Correction:** Use a monotonic decreasing shininess-to-roughness mapping, with the roughness convention explicitly documented.

### M11. Medium: octahedral encoding flips the negative-Z pole

**Location:** `FragmentAtoms\utility.frag:63-66`.

Octahedral folding needs a sign-not-zero operation. GLSL `sign(0)` is zero, so `(0,0,-1)` encodes to `(0.5,0.5)` and decodes as `(0,0,+1)`.

**Correction:** Use component-wise nonzero sign selection for the fold.

### M12. Medium: water-volume bounds have the wrong center and ignore later bodies

**Locations:** `Common\waterData.cmn:5-14,38-41`; host contract `Source\Environment\Water\Water.cpp:348-355`.

Bounds are computed as plus/minus `(extents + position)/2` instead of `position +/- extents/2`. A body centered at X=100 with width 20 is tested against `[-60,60]`, not `[90,110]`. In addition, `IsUnderWater` repeatedly reads body zero instead of the loop index.

**Correction:** Compare position-relative absolute XZ against half-extents and index every body correctly.

### M13. Withdrawn: water and velocity Vulkan Y-flip claim

The earlier claim assumed a negative-height Vulkan viewport for offscreen
rendering. The rebased renderer uses positive height for those targets, in
the same texture-space convention as OpenGL (`VKWrapper.cpp:3277-3303`).
Do not add a Vulkan-only water or velocity Y flip on that basis.

### M14. Medium: specialized vertices do not initialize required varyings

**Locations:** `terrainPlane.glsl:9-19,39-45`; `grass.glsl:41-81`; `tree.glsl:25-56`.

The plane writes a normal in the prepass without producing `_normalWV`. Grass/tree specialized paths do not initialize selection state or previous-frame projected position. Grass is explicitly non-static (`Vegetation.cpp:358`), so its display prepass requests velocity.

**Correction:** Initialize every consumed varying; compute previous-frame positions using previous instance/procedural deformation, not merely a current-frame copy. This remains necessary after fixing M2.

### M15. Medium: tree normals omit instance rotation; shared normals omit nonuniform scale

**Locations:** `tree.glsl:41,50-52`; `VertexAtoms\lightingDefaults.vert:7-12,23`; position transform `Common\nodeDataDefinition.cmn:32-35`.

Tree positions include the instance quaternion, while their normals use only node rotation. More generally, positions include independent XYZ scale but normal transformation includes only rotation, so nonuniformly scaled objects shade incorrectly.

**Correction:** Apply instance orientation and the appropriate inverse-transpose normal transform. Transform tangents consistently and handle reflected scale/handedness deliberately.

### M16. Partially addressed: particle billboard axes and soft-particle UVs

**Locations:** `particles.glsl:12-18,76`.

The billboard takes columns from world-to-view as if they were world-space camera right/up, then transforms them by view again. Soft particles multiply `gl_FragCoord.xy` by viewport dimensions instead of normalizing by texture dimensions.

**Correction:** Use inverse-view camera axes or expand the quad in view space; divide pixel coordinates by the sampled depth texture dimensions and preserve a coherent depth-fade convention.

The shader now expands billboards in view space and normalizes depth coordinates.
The display prepass leaves particle depth and G-buffer data untouched so the
soft fade does not sample the particles' own depth. Actual rendered images and
the interaction with all transparency paths still need validation; previous
particle positions are not buffered, so velocity captures camera motion only.

### M17. Medium: terrain normal gradients omit physical scale

**Location:** `Common\terrainUtils.cmn:33-42`.

Height differences use normalized texture samples while geometry uses `WORLD_SCALE_Y` and horizontal world/terrain scale. The fixed Y component of two only describes a special unit-scale case.

**Correction:** Include vertical amplitude and physical horizontal texel spacing in the central differences, followed by the proper node normal transform.

### M18. Medium: ordinary mip generation overwrites roughness-prefiltered environment mips

**Locations:** `irradianceCalc.glsl:157-177`; `Source\Scenes\SceneEnvironmentProbePool.cpp:535-573`; backend commands `GLWrapper.cpp:1336-1345`, `VKWrapper.cpp:2812-2818`.

The host explicitly computes the roughness-prefiltered mip chain, then enqueues ordinary mip generation on the same destination. The generated lower mips no longer represent the intended roughness convolution.

**Correction:** Do not regenerate destination mips after the explicit convolution. Generate required source mips before convolution and preserve the relevant barriers.

### M19. Medium: light-impostor geometry does not reassign colour after each emission

**Location:** `lightImpostorShader.glsl:40,48-69`.

Geometry-shader outputs become undefined after `EmitVertex`. `lightColour` is assigned only once, leaving later vertices/provoking vertices without a defined colour.

**Correction:** Assign every required output before each emitted vertex.

### Additional material/environment issues and caveats

| Classification | Evidence | Action |
|---|---|---|
| Fixed latent compile failure | `water.glsl:83-84,94-100`: malformed skipped-reflection output call, missing comma, and incompatible fallback component count. | Repaired and covered by representative compilation. |
| Fixed latent compile failure | `FragmentAtoms\utility.frag:57`: packing macro accepted `X,Y` but referred to lowercase `x,y`; no callers found. | Corrected and covered by representative compilation. |
| Unsafe shadow guard / extra work | `lightingCalc.frag:68,84,99`: `mix` receives already evaluated shadow-function calls, so it does not guard a negative shadow index or `receivesShadows=false`. | Use a real conditional before accessing shadow data. Do not claim boolean `mix` necessarily propagates an unselected NaN. |
| Broken seam helper | `IBL.frag:75,79,84,89`: integer division produces a zero scale; merely fixing that division exposes additive offsets where scaling non-dominant components is intended. | Correct the complete seam operation, not just the literal type. |
| Conditional UI alpha defect | `ImmediateModeEmulation.glsl:89` discards input colour alpha. | If vertex alpha represents text opacity, combine it with glyph coverage. |
| Asset contract unresolved | `terrainSplatting.frag:117-124,132-140,149-162` implements ordered alpha layering from zero, not normalized splat-weight blending. Two white 0.5 layers yield 0.75. | Establish whether assets encode ordered opacity or partition-of-unity weights before changing behavior. |
| Documentation mismatch, not established LOD bug | Trees read `.data.z` for LOD, and the compute producer deliberately writes tree distance levels into Z. | Document the tree-specific overloaded field rather than blindly changing it to Y. |

### Remaining particle host integration blockers

- The default quad has no index list, but `ParticleEmitter::buildDrawCommands`
  only creates a draw command for indexed geometry. The draw path also reports
  four source buffers even when no index-buffer handle was populated.
- Particle instance counts grow after command creation, but
  `RenderingComponent` determines instancing from the initial count. The
  resulting nonzero base instance can offset per-particle attributes and bypass
  the instanced-material indirection path.
- `_bufferUpdate` and `_bbUpdate` start as null but are dereferenced by waits;
  the asynchronous buffer-update task also captures `renderStagePass` by
  reference. Task lifetime and synchronization need a complete repair.
- Bounding-box sampling can advance by zero when the average emit rate is
  below four, and the average calculation divides by the source count even
  when it is zero.

## Optimization opportunities

These are candidates for measurement, not promised speedups. Resolve correctness and capture representative GPU timings before changing algorithms.

| Area | Opportunity | Tradeoff / constraint |
|---|---|---|
| Cluster light lists | Each invocation now reserves `uint[MAX_LIGHTS_PER_CLUSTER]`. Evaluate bounded direct writes into fixed per-cluster ranges instead of private arrays plus a global allocator. | May reduce register/private-memory pressure and allocator atomics; changes compactness/locality. Preserve point/spot partitions and the total budget. |
| Cluster tests | The sphere/AABB test is already vectorized using closest-point clamping. Profile batch size, private-array pressure and light-type handling before changing it further. | Preserve workgroup barriers, conservative intersection and the combined point/spot budget. |
| HiZ | Output bounds checks and deliberate base-level dilation are already implemented. Evaluate specialized even-size reduction and base-level paths while retaining the general odd-size footprint. | Any faster path must preserve conservative maximum depth for every covered source texel. |
| HiZ workgroup size | Evaluate smaller groups than the current 32x32 reduction group. | 1024 invocations can constrain occupancy and exceeds some device limits; see R18. Choose from actual supported limits. |
| Exposure | Skip histogram/adaptation when neither automatic exposure nor a diagnostic consumer needs it. | Preserve exposure history/reset behavior when enabling it again. |
| Gaussian blur | Evaluate bilinear tap pairing for separable blur. | Requires equivalent weights, sampler filtering, and edge behavior. |
| SSAO | Remove redundant normalization of already normalized unpacked normals; hoist invariant falloff calculations; make half-resolution texel footprints explicit. | Keep normal-space and sampling contracts intact. |
| SSR | Reject invalid reprojected W/UV before previous-frame sampling. | Avoids clamp-to-edge streaks; needs a clear miss/disocclusion policy. |
| Shared qualifiers | Narrow `coherent` and synchronization scopes only after establishing actual communication. | Do not remove required workgroup rendezvous or host barriers in pursuit of a presumed speedup. |
| BRDF LUT dispatch | `irradianceCalc.glsl:89` uses 16x16 local groups, but `SceneEnvironmentProbePool.cpp:350-352` divides dimensions by eight. Match those dimensions and add a bounds guard. | At 128x128 this launches 256x256 invocations: four times the required count. This is a static count, not a measured fourfold speedup. |
| Diffuse environment convolution | `irradianceCalc.glsl:25-34` performs roughly 360x90 samples for each output face texel. Evaluate cosine-weighted low-discrepancy sampling and dispatching faces in Z. | Establish an image-quality budget and profile representative environment maps. |
| Lighting arithmetic | Use squared distance where only a squared comparison/attenuation needs it (`lightingCalc.frag:86,109`; `bumpMapping.frag:16`). | Check compiler output; do not replace distance values that are also needed elsewhere. |

## Clarity and rewrite opportunities

1. **One fullscreen vertex contract.** Centralize clip-position creation,
offscreen UV orientation, inverse resolution and viewport origin; document the
separate swapchain convention. The current parallel implementations have
already drifted.
2. **Named push-data accessors.** Keep the efficient two-matrix ABI, but expose effect-local names for packed fields, with the CPU packing documented beside those aliases. Avoid unexplained `PushData0[column].component` at call sites.
3. **Typed depth helpers.** Separate device depth, positive view distance, signed view Z, orthographic reconstruction, and normalized linear depth. Names should identify both input and output domains.
4. **Well-formed expression defines.** Prefer inline GLSL defines; parenthesize
their parameters and keep bodies expression-only. Document repeated evaluation
and materialize side-effecting or expensive arguments before invoking a macro.
The current bare-identifier `easeInQuint(kD)` call is valid; lack of macro
argument parentheses alone is not proof that this call is broken.
5. **Explicit quality and variant contracts.** Keep CPU selections and shader tables in agreement. Distinguish active variants from legacy code; do not silently retain unsupported menu values.
6. **Resource declarations near command contracts.** Use named logical slot constants for exposure/histogram and other multipass resources. Read-only, write-only and read-write usages should match shader operations.
7. **Pipeline-state caching versus command submission.** Cache expensive calculations, not required state-setting commands. Bloom demonstrates why settings-change flags must not control per-execution binding.
8. **Separate numerical data from colour transforms.** Normal, velocity, depth and material-property previews should not share gamma conversion intended for colour.
9. **Propagate preprocessing errors.** `loadAndParseGLSL` ignores a failed `PreProcessMacros` result with `NOP()` (`ShaderProgram.cpp:2149-2152`). Return an explicit failure before caching or compiling the remaining source, so diagnostics identify the first failing stage.
10. **Name artistic choices as choices.** Bloom's `mix` versus additive composition and its Karis weighting are visual-policy questions, not established bugs without the intended contract. Similarly, changing a BRDF or shadow filter should not be hidden inside a cleanup.
11. **One roughness convention.** Direct GGX, LUT integration and environment filtering should explicitly distinguish perceptual roughness from microfacet alpha; the current paths use different powers. Resolve the intended model before algebraic cleanup.
12. **Delete truly dead alternatives only after checking consumers.** Candidates include the unreachable duplicate return in `pbrHelpers.comp:63-65`, unused material/SSR temporaries, and legacy shader sections. Keep debug and runtime-selectable variants covered.

## Evidence limits

This is a source and host-contract audit, not a GPU image comparison or
performance profile. Local counterexamples establish the original shared
depth-reconstruction error, loss of conservative HiZ depth across the old
reduction, shared-light batch corruption, invalid easing expansions, and the
OpenGL SPIR-V target mismatch. Representative probes also exposed R1 before
its repair and expose R14 on both APIs.

The existing compilation harness is not the engine's exact preprocessing
pipeline: among other differences, it omits some engine aliases, uses one
water body and two probes where the engine declares two and 512 respectively,
and uses a 64-light cluster limit where the checked-in XML uses 100. Some
one-off probes supplied the real missing aliases in memory. The harness does
not cover every active shader variant, GPU synchronization, or end-to-end
rendering. A full engine build and runtime rendering were not verified.

The relevant OpenGL SPIR-V environment is described by the [Khronos ARB_gl_spirv specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gl_spirv.txt).
