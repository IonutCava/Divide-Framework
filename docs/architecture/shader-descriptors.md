# Shaders and descriptors

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Platform/Video/Shaders/ShaderProgram.cpp`: shared source loading, generated header, shader pipeline.
- `Source/Platform/Video/Shaders/GLSLToSPIRV.cpp`: glslang stage mapping and SPIR-V compilation.
- `Source/Platform/Video/Shaders/ShaderDataUploader.cpp`: cache/reflection serialization and `BYTE_BUFFER_VERSION`.
- `Source/Platform/Video/Headers/RenderAPIEnums.h`: shader/topology/descriptor enums.
- `Source/Platform/Video/GFXDevice.cpp`: `initDescriptorSets()` registers canonical bindings.
- `Assets/Shaders/GLSL/`: shader declarations and shared atoms.
- `Source/Platform/Video/RenderBackend/OpenGL/Shaders/glShaderProgram.cpp`: GL consumption/binding.
- `Source/Platform/Video/RenderBackend/Vulkan/Shaders/vkShaderProgram.cpp`: Vulkan shader modules and program layouts.
- `Source/Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp`: descriptor layouts, caches, and `VK_API::bindShaderResources`.
- `Source/Platform/Video/RenderBackend/Vulkan/vkDescriptors.cpp` and `Source/Platform/Video/RenderBackend/Vulkan/Headers/vkDescriptors.h`: descriptor layout-cache identity.
- `Source/Platform/Video/RenderBackend/Vulkan/Vulkan-Descriptor-Allocator/`: descriptor pool allocation.

## Shared shader flow

`ShaderProgram` loads/reparses GLSL, loads/builds GLSL cache, loads/builds SPIR-V, loads/builds reflection, and hands shared results to the backend.
OpenGL still consumes GLSL; Vulkan consumes SPIR-V. Reflection is shared, not duplicated per backend.
The resource pool calls `unload()` before destroying a loaded shader program.
Vulkan's destructor only releases any remaining module-parent references, so it
also cleans up partially loaded programs without unregistering a loaded program
twice.
Material shader slots own their program references. Cloning acquires a separate
reference, moving a slot transfers it, and replacing a program releases the old
reference after acquiring the replacement. Queued replacements retain the old
program until processed but do not report the slot ready; material unload cancels
queued writes and waits for in-flight shader loads before releasing slots.
Vulkan shader-load and reload notifications are queued by program GUID under a
mutex. The render thread swaps the pending IDs into reusable storage at frame
end and invalidates matching compiled pipelines without retaining queued program
pointers; notifications arriving during the batch are processed next frame.
Hot reload deliberately invalidates caches and reparses source. Configuration-derived constants (clustered-lighting budget, reflection-probe size/mips, binding maps, etc.) are injected into the generated shader header by `InitGLSW`. Every generated line passes through one helper that folds the stage name and line into an FNV-1a (`_ID`) fingerprint. GLSL, SPIR-V, and reflection caches live under `header-<fingerprint>/<preprocess-version>`, so generated-header changes invalidate them. New generated lines must use `AppendToShaderHeader`, not `glswAddDirectiveToken` directly; bump `s_shaderPreprocessCacheVersion` whenever preprocessing semantics change.
The newest shader-atom timestamp is read and updated under `g_cacheLock`,
including file-watcher updates racing asset-loader cache reads.

Changes must cover source parsing, generated declarations, cache invalidation, SPIR-V generation, reflection compatibility, and both backend consumers.

### GLSL authoring and preprocessing

- Effect files are GLSW collections; `ShaderModuleDescriptor` selects a `file.Stage.variant` section, and module/global defines select material, pass, and debug variants.
- Shader-thread initialization resets the thread-local preprocessor tag cursor;
  repeated initialization on the same thread must not append past its tag array.
- `PreprocessIncludes` runs **before** `PreProcessMacros`, so every `#include` must resolve even inside a disabled `#if`.
- Include directives may end with whitespace or a single-line comment. Malformed, missing, cyclic, or nested includes beyond 64 levels stop source loading instead of being silently dropped by the macro preprocessor. Expansion uses an explicit stack; cached atoms retain their dependency depth so cache hits cannot bypass the limit. Only successfully expanded atoms are cached, and changing a dependency invalidates every cached parent.
- A missing GLSW section or failed macro preprocessing aborts source loading before uniform extraction, shader compilation, or cache writes.
- Loose uniforms are collected into generated `std140` blocks at `PER_DRAW` slots `s_uniformsStartOffset` (+1 for a second distinct block; at most two per program).
- SPIR-V reflection records vertex input locations, including matrix columns and array elements, and reflection-cache version 6 persists them. Built-in inputs are excluded; unsupported input types or locations fail reflection. Vulkan uses the reflected locations to omit unused vertex attributes from pipeline descriptions without inventing missing attributes. Shader reloads clear and rebuild this mask.
- Prefer GLSL expression defines over helper functions so preprocessed shaders stay as inline as practical. Parenthesize macro arguments, keep bodies expression-only, and do not pass side-effecting or expensive expressions to macros that evaluate an argument more than once.
- `GLSLToSPIRV` targets Vulkan 1.4 / SPIR-V 1.6 for Vulkan and the OpenGL 4.5 environment / SPIR-V 1.0 for OpenGL (`debug.renderer.useSPIRVForOpenGL` ingests the latter directly). Compiled-binary and reflection cache paths include `s_compiledShaderCacheVersion`; bump it whenever compiler targets change.
- `Source/UnitTests/Test-Engine/ShaderCompilerTests.cpp` exercises repeated shader-thread initialization, diagnostics, and include expansion. CPU checks do not replace GPU execution or image-based rendering checks.

## Descriptor ownership and invariants

| Usage | Vulkan set index | Main resources |
|---|---|---|
| `PER_FRAME` | 0 | Environment, shadows, probes, scene globals |
| `PER_PASS` | 1 | Scene inputs and clustered-light buffers |
| `PER_BATCH` | 2 | Indirect commands, camera, transforms, indirection, materials |
| `PER_DRAW` | 3 | Material textures, optional images, per-draw buffers |

- Shaders use usage names, such as `DESCRIPTOR_SET_RESOURCE(PER_BATCH, 2)`, never raw set indices. Generated headers map bindings per API.
- `DescriptorSetUsage` is ordered by update frequency; `PER_DRAW` must remain last. Vulkan uses the enum value directly; OpenGL maps `(usage, slot)` to flat bindings.
- Changing enum order requires bumping `BYTE_BUFFER_VERSION`: cached reflection stores usage indices.
- Every Vulkan pipeline layout uses real shared layouts for sets 0-2, even if unused by a program, and an identical `VK_SHADER_STAGE_ALL` 128-byte push-constant range. This preserves shared-set/push-constant compatibility across pipeline changes.
- The `PER_PASS` light buffer at slot 9 is visible to vertex, fragment, and compute stages: debug light impostors read it in their vertex shader. Slots 8, 10, and 11 are fragment/compute only. The legacy `COMPUTE_AND_DRAW` name means fragment and compute, not all draw stages; use the explicit visibility names for new bindings.
- Partial push-constant commands merge only `dataFlags()`-selected slots into
  the Vulkan replay cache; omitted slots retain their last explicit values.
- Command coalescing accepts complementary or equal overlapping push slots,
  rejecting conflicting overlapping values without mutating the earlier command.
- Uniform-data coalescing treats shared storage as a no-op. Distinct sources
  transfer their live bindings only after all overlapping types and sizes pass
  validation; later values overwrite earlier ones, and successful transfers empty
  the source. Rejected transfers leave both sides unchanged.
- `DebugView` explicitly initializes `skipLinearisation` to zero so shared
  `fbPreview` programs do not inherit the splash screen's encoded-output mode.
- OpenGL retains the same flagged-slot cache and replays known slots on pipeline
  binds, including when no new push command follows a shader change. Partial
  uploads use the merged cache rather than shader-local stale uniform values.
- `PER_DRAW` is per-program and uses push descriptors. Pipeline creation assembles a local layout array; never overwrite the global shared-layout array with a program-specific layout.
- Descriptor layout-cache identity must include creation flags, so push-descriptor layouts cannot alias ordinary layouts with the same bindings.
- Push-descriptor validation uses queried `VkPhysicalDevicePushDescriptorProperties::maxPushDescriptors`, not dynamic-buffer limits.

The local-array, flag-aware cache, and push-descriptor limit notes were checked against commit `1508b95` during this reorganization; see `VK_API::createLayoutFromBindings`, pipeline layout creation, and the descriptor layout cache.

## Binding and cache lifetime

`VK_API::bindShaderResources` caches resolved shared-set contents. New descriptor sets are allocated/written when resources change; otherwise only dynamic offsets change.
Bound sets/offsets are tracked per bind point (graphics/compute) and command buffer. `PER_DRAW` is re-pushed when contents or pipeline layout change.
`frameStarted` invalidates cached sets as descriptor pools flip.
Vulkan descriptor-pool and descriptor-set allocation failures are logged and
propagated; the affected draw or dispatch is skipped rather than binding an
invalid or stale set. A later submission retries dirty descriptor state.

Both OpenGL and Vulkan resolve null and omitted active combined image samplers through `ShaderProgram::resolveSampledImages`. Shared-set bindings remain persistent; missing per-draw slots cannot inherit stale textures.
When an OpenGL shader bind fails, ordinary descriptor updates still run without
sampler resolution; a null active shader must not turn the existing draw-skip
path into an assertion or dereference.
Non-MSAA fallbacks share 1x1 black texel values and nearest/clamp sampling, with compatible dimensionality (including arrays/cubes), numeric format, and depth comparison.
Missing active MSAA bindings are hard GPU assertions, never fallback textures. Reflection cache v5 stores these sampler traits.
These sampler notes were added by commit `7b09ed5`.

`DescriptorSetBindingType::INDIRECT_BUFFER` is not a shader resource. `SetIndirectBuffer` at `PER_BATCH` slot 0 selects the indirect draw buffer (`GL_DRAW_INDIRECT_BUFFER` / `vkCmdDraw*Indirect`) and is skipped in GL binding tables and Vulkan layouts.
The same command buffer is separately a writable SSBO at `PER_BATCH` slot 2 (`dvd_GPUCmds`) for compute Hi-Z culling. Synchronization follows `BufferUsageType::COMMAND_BUFFER` storage/indirect usage, not the descriptor binding type.

## Changing bindings safely

Audit `GFXDevice::initDescriptorSets()`, shader declarations, generated headers, reflection serialization, GL binding translation, and Vulkan descriptor/push-descriptor/descriptor-buffer paths together.
Check pipeline compatibility, dynamic-offset ordering, cache invalidation, and frame/pool lifetimes. See [GPU resources](gpu-resources.md) and [threading](threading.md) for resource/synchronization changes.
