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
Hot reload deliberately invalidates caches and reparses source. Clustered-lighting constants are injected into the generated shader header.
The newest shader-atom timestamp is read and updated under `g_cacheLock`,
including file-watcher updates racing asset-loader cache reads.

Changes must cover source parsing, generated declarations, cache invalidation, SPIR-V generation, reflection compatibility, and both backend consumers.

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
- Partial push-constant commands merge only `dataFlags()`-selected slots into
  the Vulkan replay cache; omitted slots retain their last explicit values.
- `PER_DRAW` is per-program and uses push descriptors. Pipeline creation assembles a local layout array; never overwrite the global shared-layout array with a program-specific layout.
- Descriptor layout-cache identity must include creation flags, so push-descriptor layouts cannot alias ordinary layouts with the same bindings.
- Push-descriptor validation uses queried `VkPhysicalDevicePushDescriptorProperties::maxPushDescriptors`, not dynamic-buffer limits.

The local-array, flag-aware cache, and push-descriptor limit notes were checked against commit `1508b95` during this reorganization; see `VK_API::createLayoutFromBindings`, pipeline layout creation, and the descriptor layout cache.

## Binding and cache lifetime

`VK_API::bindShaderResources` caches resolved shared-set contents. New descriptor sets are allocated/written when resources change; otherwise only dynamic offsets change.
Bound sets/offsets are tracked per bind point (graphics/compute) and command buffer. `PER_DRAW` is re-pushed when contents or pipeline layout change.
`frameStarted` invalidates cached sets as descriptor pools flip.

Both OpenGL and Vulkan resolve null and omitted active combined image samplers through `ShaderProgram::resolveSampledImages`. Shared-set bindings remain persistent; missing per-draw slots cannot inherit stale textures.
Non-MSAA fallbacks share 1x1 black texel values and nearest/clamp sampling, with compatible dimensionality (including arrays/cubes), numeric format, and depth comparison.
Missing active MSAA bindings are hard GPU assertions, never fallback textures. Reflection cache v5 stores these sampler traits.
These sampler notes were added by commit `7b09ed5`.

`DescriptorSetBindingType::INDIRECT_BUFFER` is not a shader resource. `SetIndirectBuffer` at `PER_BATCH` slot 0 selects the indirect draw buffer (`GL_DRAW_INDIRECT_BUFFER` / `vkCmdDraw*Indirect`) and is skipped in GL binding tables and Vulkan layouts.
The same command buffer is separately a writable SSBO at `PER_BATCH` slot 2 (`dvd_GPUCmds`) for compute Hi-Z culling. Synchronization follows `BufferUsageType::COMMAND_BUFFER` storage/indirect usage, not the descriptor binding type.

## Changing bindings safely

Audit `GFXDevice::initDescriptorSets()`, shader declarations, generated headers, reflection serialization, GL binding translation, and Vulkan descriptor/push-descriptor/descriptor-buffer paths together.
Check pipeline compatibility, dynamic-offset ordering, cache invalidation, and frame/pool lifetimes. See [GPU resources](gpu-resources.md) and [threading](threading.md) for resource/synchronization changes.
