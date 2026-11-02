# GLSL and shader integration audit

Initial source review: 2026-11-02. The findings below describe the code as reviewed;
the status notes identify subsequent repairs.

## Scope and interpretation

The shader tree contains 35 GLSW effect files and 35 shared shader atoms: 20 fragment atoms, four vertex atoms, ten common atoms, and one compute atom. An effect file can contain multiple stages and variants; these counts are not counts of compiled programs. The bundled FXAA implementation is treated as third-party code rather than independently certified.

Findings below distinguish source-proven defects, conditional/latent defects, and profiling-dependent improvements. They are not claims of observed GPU failures on a particular driver. Most arithmetic, indexing, and culling problems affect both backends.

**Repair status:** C1-C12, M1, M2 and M5 have been addressed. The particle shader
compilation defects in M3 and the billboard/UV errors in M16 have been repaired,
as have the dormant water and vector-packing compilation failures. These changes
have focused compiler/link checks (including every clustered-lighting and culling
compute shader on both backends); the new transform, easing, packing, depth
reconstruction, spot bounding-sphere and cluster-index code also have Vulkan
numerical checks. The particle renderer still has the host-side blockers listed
below and has not been validated end to end.

**Remaining priorities:** CSM split upload, exposure synchronization/access,
the particle host integration, and the post-processing and material defects below.
Several less dramatic visual defects are also genuine math or contract errors, not
simply stylistic opportunities.

## How the source actually reaches the APIs

| Step | Engine behavior | Main evidence |
|---|---|---|
| Effect selection | `ShaderModuleDescriptor` selects `file.Stage.variant`; module and global defines contribute to the cached shader identity. GLSW selects the `-- Stage.variant` sections. | `Source\Platform\Video\Shaders\ShaderProgram.cpp:1661-1729` |
| Generated header | Adds GLSL 460, API/stage defines, engine constants, vertex attribute locations, stage interface blocks, and macro aliases. The files are not standalone GLSL compilation units. | `ShaderProgram.cpp:390-760` |
| Includes | Recursively expands atoms according to extension and records dependencies for caches/hot reload. Includes are expanded before conditional macro processing. | `ShaderProgram.cpp:1375-1508` |
| Macro processing | Replaces custom defines and invokes the FPP preprocessor before extracting loose uniforms. | `ShaderProgram.cpp:1966-2011` |
| Loose uniforms | Collects plain uniform declarations into generated `std140` blocks at `PER_DRAW` slots 14/15. Original names become macros referring to block members. At most two distinct blocks are supported. This is separate from fast push data. | `ShaderProgram.cpp:2014-2056`; `ShaderDataUploader.cpp:249-283` |
| Fast push data | `PushData0` and `PushData1` are column-major `mat4` values. Vulkan declares them as members of an anonymous 128-byte push-constant block. OpenGL declares two `uniform mat4` values at `GL_PUSH_CONSTANTS_LOCATION` (18) and 19. Reflection records which of the two a program uses. | `ShaderProgram.cpp` (`pushConstantCodeBlock`, push-constant usage scan); `ShaderProgram.h:122` |
| Upload | Vulkan calls `vkCmdPushConstants`; OpenGL uploads both matrices using `glProgramUniformMatrix4fv(..., GL_FALSE, ...)`. Other uniforms use reflected UBO uploaders. | `VKWrapper.cpp:2563-2586`; `glShader.cpp:378-388` |
| Compilation/reflection | Both paths generate SPIR-V for reflection. Vulkan creates shader modules from it. OpenGL either compiles generated GLSL or ingests SPIR-V, depending on `useSPIRVForOpenGL` (whose configuration default is true). | `ShaderProgram.cpp:1860-1944`; `GLSLToSPIRV.cpp:168-257`; `glShader.cpp:123-151`; `vkShaderProgram.cpp:67-96`; `Configuration.h:50` |
| Caches | Generated GLSL, SPIR-V, and reflection are separated by build type and graphics API, with effect/atom modification-time checks. | `ShaderProgram.cpp:277-347` |

### Descriptor mapping

The logical Vulkan set numbers are `PER_FRAME=0`, `PER_PASS=1`, `PER_BATCH=2`, and `PER_DRAW=3` (`RenderAPIEnums.h:75-81`); `PER_DRAW` must remain last.

`DESCRIPTOR_SET_RESOURCE(PER_PASS, 9)` is **not** translated into an arithmetic binding such as 29 on OpenGL. Token concatenation selects the generated `PER_PASS_9` macro, whose value comes from the registered binding table. Texture, image, and buffer slots are assigned by the engine; compute-only buffers also have a separate allocation counter. Vulkan retains the logical set and slot.

Definitions and registration: `ShaderProgram.cpp:396-432,1219-1256`; `GFXDevice.cpp:231-279`. OpenGL SPIR-V reflection maps flattened bindings back to logical sets/slots using the resource type: `GLSLToSPIRV.cpp:331-344`.

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

## Confirmed defects: shared compute and integration

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

### C5. High for vegetation: padded culling invocations do not stop at valid items

**Locations:** `HiZOcclusionCull.glsl:61-65`; `instanceCullVegetation.glsl:56-61`; host dispatch `GFXDevice.cpp:2436-2437,2480`; `Vegetation.cpp:688,703`.

The out-of-range branches call `CullItem` instead of simply returning. For example, 65 draw commands dispatch 128 invocations, and indices 65-127 still write `instanceCount=0` despite being outside the active command count. For ordinary indirect culling, the host binds a full 8192-command capacity (`RenderPassExecutor.cpp:774-778`; `EngineConfig.h:121`), so the current rounded dispatch alone does **not** establish a physical buffer overrun. That part is unnecessary tail modification and misleading accounting, not a proven adjacent-allocation corruption.

Vegetation has an additional host-side alignment mistake: `_maxGrassInstances += _maxGrassInstances % WORK_GROUP_SIZE` (same for trees) is not a round-up to the work-group size (`Vegetation.cpp:307-308`). With 65 generated instances, capacity becomes 66 while 128 invocations are dispatched. Here the excess lanes really can write into the next chunk or beyond the last allocation. Separately, the shader is not given the actual active count, so capacity-padding entries can be processed as if they were real instances.

**Correction:** Return without item writes for every out-of-range invocation. Use correct capacity round-up on the host and pass the actual active instance count separately from chunk capacity.

**Status: fixed.** Padding lanes only bump the padding statistic and return. Vegetation capacity is rounded up to a multiple of `WORK_GROUP_SIZE`, and `dvd_grassInstanceCount` / `dvd_treeInstanceCount` bound the shader's work.

**Open notes (not changed):** `HiZOcclusionCull` still returns early when `dvd_numEntities < WORK_GROUP_SIZE`, so small command lists are never occlusion-culled; this is conservative but probably unintended. In `HiZCullingAlgorithm.cmn`, `pos.x < -pos.w * 0x01` reads as a bit test but is plain multiplication by one; drop the `* 0x01` for clarity.

### C6. High, Vulkan-specific: vegetation compute reads a draw-only descriptor

**Locations:** `instanceCullVegetation.glsl:29-31,81`; `Common\sceneData.cmn:20-33`; `Source\Platform\Video\GFXDevice.cpp:278`; `VKWrapper.cpp:3113-3167`.

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

### P2. High: some fullscreen stages omit the Vulkan UV convention

**Locations:** `EdgeDetection.glsl:16-28`; `SMAA.glsl:19-39,52-61`; compare `baseVertexShaders.glsl:15-17`.

These fullscreen stages omit the target-specific UV inversion used by the shared fullscreen vertex shader. With the engine's negative-height Vulkan viewport, edge/weight and original-colour sampling no longer follow the shared render-target orientation.

**Correction:** Reuse one fullscreen vertex setup and derive sample offsets after applying the intended UV convention, without feeding transformed UVs back into clip-position generation.

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

**Locations:** `luminanceCalc.glsl:111-115`; `Source\Rendering\PostFX\PreRenderBatch.cpp:630-652`; OpenGL access translation `GLWrapper.cpp:1731-1735`.

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
| Exposure histogram binding mismatch | `luminanceCalc.glsl:72-74` declares slot 13, but averaging binds slot 1 in `PreRenderBatch.cpp:647-648`. A retained slot-13 binding from creation can mask this. | Bind the declared resource slot explicitly. |
| 8K weighted histogram overflow | `luminanceCalc.glsl:82-95` sums weighted counts in `uint`. 7680x4320 pixels in bin 255 give 8,460,288,000, exceeding 32 bits. | Use an overflow-safe weighted reduction, such as floating-point accumulation. |
| SSAO upsample candidate can remain uninitialized | `SSAOPass.glsl:400-410`: `best_depth_idx` is not initialized; no candidate wins if all differences equal the initial threshold. | Initialize a valid candidate and a suitably large comparison distance. |
| Latent asymmetric SSAO box blur | `SSAOPass.glsl:137-138` scans `[-k,k)` rather than symmetric `[-k,k]`. Current C++ selects the Nvidia blur variant. | Correct the footprint if retaining the variant. |

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

### M13. Medium, Vulkan-specific: water screen-space lookup and velocity Y use the wrong orientation

**Locations:** `water.glsl:69-73,86,98`; `FragmentAtoms\velocityCalc.frag:9-11`; velocity consumer `blur.glsl:248-258`.

Water's NDC-to-UV mapping assumes a positive-height viewport. Velocity similarly subtracts NDC-derived UVs without accounting for the Vulkan negative viewport height; motion blur consumes the result directly as texture-space offsets.

**Correction:** Define one framebuffer-texture UV convention. Correct water lookup and negate velocity Y for Vulkan under that convention; avoid fixing both producer and consumer in conflicting ways.

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

**Locations:** `irradianceCalc.glsl:157-177`; `Source\Scenes\SceneEnvironmentProbePool.cpp:535-573`; backend commands `GLWrapper.cpp:1316-1320`, `VKWrapper.cpp:2613-2615`.

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
| Cluster light lists | Each invocation reserves `uvec2[MAX_LIGHTS_PER_CLUSTER]`, while C++ already allocates a fixed maximum number of indices for every cluster. Evaluate bounded direct writes into fixed per-cluster ranges instead of private arrays plus a global allocator. | May reduce register/private-memory pressure and allocator atomics; changes compactness/locality. Preserve point/spot partitions and the total budget. |
| Cluster tests | Replace scalar point/AABB branches with a vector closest-point distance, and remove `currentCell.maxPoint[3] = tile`, whose modified component is never used. | Compilers may already eliminate/fold much of this; compare emitted code rather than assuming improvement. |
| HiZ | Add an output-size guard for padded dispatch lanes. Make the same-size base operation an explicit copy or deliberate conservative dilation. | Out-of-range `imageStore` is not being reported as the same buffer-corruption defect as C5; the guard principally avoids useless work. |
| HiZ workgroup size | Evaluate smaller groups than the current 32x32 reduction group. | 1024 invocations can constrain occupancy and excludes devices with smaller compute limits. Choose from actual supported device limits. |
| Exposure | Skip histogram/adaptation when neither automatic exposure nor a diagnostic consumer needs it. | Preserve exposure history/reset behavior when enabling it again. |
| Gaussian blur | Evaluate bilinear tap pairing for separable blur. | Requires equivalent weights, sampler filtering, and edge behavior. |
| SSAO | Remove redundant normalization of already normalized unpacked normals; hoist invariant falloff calculations; make half-resolution texel footprints explicit. | Keep normal-space and sampling contracts intact. |
| SSR | Reject invalid reprojected W/UV before previous-frame sampling. | Avoids clamp-to-edge streaks; needs a clear miss/disocclusion policy. |
| Shared qualifiers | Narrow `coherent` and synchronization scopes only after establishing actual communication. | Do not remove required workgroup rendezvous or host barriers in pursuit of a presumed speedup. |
| BRDF LUT dispatch | `irradianceCalc.glsl:89` uses 16x16 local groups, but `SceneEnvironmentProbePool.cpp:350-352` divides dimensions by eight. Match those dimensions and add a bounds guard. | At 128x128 this launches 256x256 invocations: four times the required count. This is a static count, not a measured fourfold speedup. |
| Diffuse environment convolution | `irradianceCalc.glsl:25-34` performs roughly 360x90 samples for each output face texel. Evaluate cosine-weighted low-discrepancy sampling and dispatching faces in Z. | Establish an image-quality budget and profile representative environment maps. |
| Lighting arithmetic | Use squared distance where only a squared comparison/attenuation needs it (`lightingCalc.frag:86,109`; `bumpMapping.frag:16`). | Check compiler output; do not replace distance values that are also needed elsewhere. |

## Clarity and rewrite opportunities

1. **One fullscreen vertex contract.** Centralize clip-position creation, API-dependent UV orientation, inverse resolution and viewport origin. The current parallel implementations have already drifted.
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
9. **Propagate preprocessing errors.** `loadAndParseGLSL` ignores a failed `PreProcessMacros` result with `NOP()` (`ShaderProgram.cpp:2006-2009`). Return an explicit failure before caching or compiling the remaining source, so diagnostics identify the first failing stage.
10. **Name artistic choices as choices.** Bloom's `mix` versus additive composition and its Karis weighting are visual-policy questions, not established bugs without the intended contract. Similarly, changing a BRDF or shadow filter should not be hidden inside a cleanup.
11. **One roughness convention.** Direct GGX, LUT integration and environment filtering should explicitly distinguish perceptual roughness from microfacet alpha; the current paths use different powers. Resolve the intended model before algebraic cleanup.
12. **Delete truly dead alternatives only after checking consumers.** Candidates include the unreachable duplicate return in `pbrHelpers.comp:63-65`, unused material/SSR temporaries, and legacy shader sections. Keep debug and runtime-selectable variants covered.

## Evidence limits

This is a source and host-contract audit, not a GPU image comparison or performance profile. Local counterexamples establish the shared depth-reconstruction error, loss of conservative HiZ depth across the actual base dilation and two mips, shared-light batch corruption, invalid easing expansions, and the OpenGL SPIR-V target mismatch. The report does not claim that every shader variant was compiled through the full engine or that every conditional issue occurs in a default scene.

The relevant OpenGL SPIR-V environment is described by the [Khronos ARB_gl_spirv specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gl_spirv.txt).
