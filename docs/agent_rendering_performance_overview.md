# Agent Rendering & Performance Overview

This document is for future agents that need to make deep changes in the rendering, asset upload, shader, and performance-sensitive systems.

## 1. Fast entry points

If you need to change:

- backend selection / command execution:
  - `Source/Platform/Video/Headers/GFXDevice.h`
  - `Source/Platform/Video/GFXDevice.cpp`
  - `Source/Platform/Video/Headers/RenderAPIWrapper.h`
- render graph / stage scheduling:
  - `Source/Core/Kernel.cpp`
  - `Source/Managers/Headers/RenderPassManager.h`
  - `Source/Rendering/RenderPass/RenderPassExecutor.cpp`
- shaders / descriptor layouts:
  - `Source/Platform/Video/Shaders/ShaderProgram.cpp`
  - `Source/Platform/Video/GFXDevice.cpp`
  - `Source/Platform/Video/RenderBackend/OpenGL/Shaders/glShaderProgram.cpp`
  - `Source/Platform/Video/RenderBackend/Vulkan/Shaders/vkShaderProgram.cpp`
- Vulkan device setup / allocator / extensions:
  - `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp`
  - `Source/Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp`
  - `Source/Platform/Video/RenderBackend/Vulkan/Buffers/vkBufferImpl.cpp`
  - `Source/Platform/Video/RenderBackend/Vulkan/Textures/vkTexture.cpp`
- OpenGL memory behavior:
  - `Source/Platform/Video/RenderBackend/OpenGL/Buffers/glMemoryManager.cpp`
  - `Source/Platform/Video/RenderBackend/OpenGL/Buffers/glBufferImpl.cpp`
  - `Source/Platform/Video/RenderBackend/OpenGL/Textures/glTexture.cpp`
- geometry import / upload:
  - `Source/Geometry/Importer/MeshImporter.cpp`
  - `Source/Geometry/Importer/DVDConverter.cpp`
  - `Source/Platform/Video/Buffers/VertexBuffer/VertexBuffer.cpp`
  - `Source/Geometry/Shapes/Mesh.cpp`
  - `Source/Geometry/Shapes/SubMesh.cpp`
- texture decode / cache / upload:
  - `Source/Platform/Video/Textures/Texture.cpp`
  - `Source/Utility/ImageTools.cpp`

## 2. High-level rendering architecture

The engine keeps most high-level rendering logic backend-agnostic and funnels actual GPU work through `GFXDevice` plus a backend-specific `RenderAPIWrapper`.

- `GFXDevice` owns shared rendering state, descriptor layout registration, pipeline creation, command buffer handling, and frame/window flush logic.
- Backends implement API-specific execution in wrappers such as the OpenGL and Vulkan implementations.
- Most game/editor systems enqueue generic rendering commands first and only translate them into API calls during command-buffer flush.

Important consequence: if a bug looks like “logic is wrong before submission,” inspect `GFXDevice`, render packages, descriptor setup, and render-pass code first. If the bug only appears in one backend, inspect the backend wrapper and resource implementations second.

## 3. Command buffer model

The CPU records backend-neutral commands and defers execution until window flush time.

- `GFXDevice::flushCommandBuffer()` only appends command buffers into the active window queue.
- `GFXDevice::flushWindow()` iterates the queued command buffers and calls `flushCommandBufferInternal()` for each one before handing control to the backend window flush.
- `flushCommandBufferInternal()` walks the generic command list, uploads descriptor state on submit boundaries, and dispatches the matching backend operations.

This means:

- command generation and command execution are intentionally separated
- many state bugs come from incorrect command ordering rather than immediate API misuse
- window/frame boundaries matter when debugging “nothing rendered” or “stale state” issues

## 4. Render graph / stage layout

The app-wide render stages are registered in `Kernel.cpp`:

- `SHADOW`
- `REFLECTION` depends on `SHADOW`
- `REFRACTION` depends on `SHADOW`
- `DISPLAY` depends on `REFLECTION` and `REFRACTION`
- `NODE_PREVIEW` depends on `REFLECTION` and `REFRACTION`

Treat this as the coarse render graph. `RenderPassManager` owns the stage passes, while `RenderPassExecutor` handles most per-stage GPU preparation and draw execution details.

When changing stage-level behavior:

- check dependency assumptions in `Kernel.cpp`
- inspect render-target availability for downstream passes
- audit descriptor consumers that assume a given pass already produced depth, light, or scene textures

## 5. What `RenderPassExecutor` actually does

`RenderPassExecutor` is one of the most important performance-sensitive files.

Key responsibilities:

- owns the render queue for one render stage
- builds and uploads shared per-node GPU data
- allocates and maintains GPU buffers for:
  - node transforms
  - material data
  - indirection data
  - indirect draw command data
- tracks dirty ranges so buffer uploads can be partial instead of always full-buffer
- partitions prep work for parallel jobs (`g_nodesPerPrepareDrawPartition`)

Operationally, this file is where scene data is compacted into GPU-friendly buffers before the actual draw submission path consumes it.

If you are debugging CPU-side render scalability, draw-call buildup, or stale per-node data, start here.

## 6. Descriptor-set model shared across backends

The engine uses a backend-neutral descriptor-set model defined by `DescriptorSetUsage` buckets such as:

- `PER_DRAW`
- `PER_BATCH`
- `PER_PASS`
- `PER_FRAME`

`GFXDevice::initDescriptorSets()` registers the canonical layout shared by the engine:

- `PER_DRAW` is mostly material textures, optional image bindings, and per-draw buffers
- `PER_BATCH` contains command, camera, transform, indirection, and material batch buffers
- `PER_PASS` contains pass inputs such as scene textures and clustered-lighting buffers
- `PER_FRAME` contains global environment, shadow, probe, and scene data

This shared layout is one of the main compatibility layers between OpenGL and Vulkan. If you change bindings, you must audit:

- shader reflection assumptions
- shader source declarations
- OpenGL binding code
- Vulkan descriptor/push-descriptor/descriptor-buffer code paths

## 7. Shared shader system between OpenGL and Vulkan

`ShaderProgram` is the shared shader pipeline.

The flow is:

1. load or reparse GLSL
2. cache GLSL if needed
3. load or build SPIR-V
4. cache SPIR-V
5. load or build reflection data
6. cache reflection
7. hand the shared load results to the active backend

Backend split:

- OpenGL consumes the shared load data and builds GL shader stages/program state in `glShaderProgram`
- Vulkan consumes the same shared load data and builds Vulkan shader objects/modules in `vkShaderProgram`

This design is important:

- OpenGL still wants GLSL
- Vulkan runs from SPIR-V
- reflection is shared infrastructure rather than duplicated backend logic

Also note:

- hot reload deliberately invalidates cache entries and reparses source
- clustered-lighting constants are injected into the generated shader header in `ShaderProgram.cpp`

When changing shader infrastructure, always think in terms of:

- source parsing
- cache invalidation
- SPIR-V generation
- reflection compatibility
- backend consumption

## 8. Vulkan backend: features, extensions, and overall shape

The Vulkan path is modern and explicit. Device selection and feature/extension enablement live in `vkDevice.cpp`.

Required extensions currently include:

- `VK_EXT_custom_border_color`
- `VK_EXT_descriptor_buffer`
- `VK_KHR_push_descriptor`

Extensions enabled when present:

- `VK_EXT_extended_dynamic_state_3`
- `VK_KHR_maintenance7`
- `VK_EXT_mesh_shader` (currently detected but mesh-shader feature hookup is effectively disabled/commented)

Important implications:

- the Vulkan path depends on descriptor-buffer and push-descriptor support assumptions
- dynamic-state behavior may differ depending on whether `extended_dynamic_state_3` is available
- optional mesh-shader work will require revisiting the currently disabled feature block

Allocator setup:

- `VKWrapper.cpp` creates a global VMA allocator once the device is ready
- that allocator is then reused for both buffer and image allocations

## 9. OpenGL backend: overall shape

The OpenGL path is optimized around AZDO-style persistent mapping and driver-managed texture residency.

Conceptually:

- the engine works hard to keep buffer updates cheap and coherent
- buffer allocations are pooled and persistently mapped
- texture objects are direct GL objects with immutable storage
- synchronization is explicit where CPU writes could race GPU reads

If a performance issue is GL-only, inspect buffer-allocation churn, lock contention, sync usage, and excessive buffer recreation first.

## 10. Geometry import, LoD generation, and GPU upload

Geometry loading starts in `MeshImporter`.

Pipeline:

1. attempt cached geometry/animation load from `.DVDGeom` / `.DVDAnim`
2. on cache miss or disabled cache, fall back to Assimp conversion through `DVDConverter`
3. convert the imported scene into engine-side `Import::ImportData`
4. create runtime mesh/submesh resources
5. upload a shared `VertexBuffer`

Important `DVDConverter` behavior:

- applies Assimp post-processing aggressively
- normalizes data into `Import::SubMeshData`
- extracts optional tangent and skinning/bone data
- runs meshoptimizer passes for:
  - vertex remap
  - vertex-cache optimization
  - overdraw optimization
  - vertex-fetch optimization
- auto-generates lower LoD index buffers with `meshopt_simplify` when the mesh is large enough

Runtime buffer shape:

- one imported model uses one shared `VertexBuffer`
- each submesh stores partitions/triangle ranges/material data instead of separate GPU vertex buffers
- `SubMesh::geometryBuffer()` resolves back to the parent mesh’s shared buffer

Upload behavior:

- `VertexBuffer` owns one GPU vertex buffer and one GPU index buffer
- `commitData()` compacts the CPU vertex layout to only the attributes actually present
- full layout changes call `setBuffer(...)`
- incremental updates call `updateBuffer(...)`
- indices may be packed to 16-bit when the mesh vertex count allows it
- CPU copies can be discarded after upload when dynamic updates are not needed

This makes geometry import sensitive to:

- vertex-layout changes
- attribute-presence tracking
- LoD/index partition correctness
- any code assuming a submesh owns its own vertex storage

## 11. Texture decode, cache, and GPU upload

The shared texture load path begins in `Texture::loadInternal()`.

Key behavior:

- comma-separated source asset lists are interpreted as layered textures or cubemap faces
- image bytes are accumulated into `ImageTools::ImageData`
- transparency / translucency inspection is performed and cached

`ImageTools` responsibilities:

- STB decodes standard image formats
- DevIL handles DDS loading compatibility
- NVTT can create DDS cache files for later fast loads
- DDS conversion can be scheduled on the `HIGH_PRIORITY` task pool

Platform note:

- DDS cache creation through NVTT is compiled out on macOS, so that cache-generation path is not universally available even though DDS loading still exists through the shared image stack

Shared texture creation path:

1. decode/load image data
2. determine dimensions, layers, format, and mip policy
3. `prepareTextureData(...)`
4. backend `loadDataInternal(...)`
5. backend `submitTextureData(...)`

Backend split:

- OpenGL reserves immutable texture storage and uploads each mip/layer with DSA texture APIs
- Vulkan allocates a VMA-backed image, fills staging buffers, copies into the image, and performs explicit image-layout transitions

If a texture bug only affects arrays, cubemaps, mip generation, or partial updates, inspect the backend-specific `loadDataInternal(...)` overloads first.

## 12. GPU memory management: OpenGL

OpenGL buffer memory is handled by `glMemoryManager`.

Core model:

- create large persistently mapped chunks
- suballocate `Block`s out of those chunks
- reuse compatible free blocks
- merge free ranges when blocks are released

`glBufferImpl` behavior:

- chooses alignment based on target type
- requests an allocation from the GL allocator
- keeps a mapped pointer for CPU writes
- uses `glLockManager` sync objects for frequently updated buffers
- uses copy/readback buffers only when necessary for read paths

Important consequence:

- logical buffers are usually not 1:1 with raw GL allocations
- changing update frequency or host-visible behavior affects allocator/sync strategy

Texture memory on the GL path is much simpler:

- no custom texture allocator
- immutable texture storage through `glTextureStorage*`
- the driver owns actual residency decisions

## 13. GPU memory management: Vulkan

Vulkan uses VMA for both buffers and images.

### Buffers

`vkBufferImpl` generally prefers device-local allocations.

Two main modes:

- host-visible / frequently updated buffers:
  - request mapped host access through VMA
  - CPU writes directly into mapped memory
- non-mappable / device-local buffers:
  - allocate staging buffers
  - copy through explicit transfer commands

This means buffer bugs often fall into one of two categories:

- wrong mapped-memory assumptions
- wrong staging/copy/synchronization assumptions

### Images

`vkTexture`:

- creates a Vulkan image with usage flags derived from requested engine `ImageUsage`
- allocates image memory with VMA, usually as dedicated device-preferred memory
- uses staging buffers for upload
- transitions layouts explicitly
- can generate mipmaps on the GPU

Important implication:

- image usage, layout transitions, and staging lifetimes are tightly coupled
- any change to writable/readable texture usage must be audited for correct layout transitions

## 14. Clustered lighting path

`Renderer.cpp` sets up the clustered-forward lighting compute path.

It allocates per-stage GPU resources for:

- light indices
- cluster AABBs
- light grid
- global index counts

It also builds compute pipelines for:

- light culling
- counter reset
- clustered AABB generation

Relevant bindings are registered through `PER_PASS` descriptor slots in `GFXDevice::initDescriptorSets()`.

If lighting changes break only on some stages, verify:

- per-stage buffer allocation
- descriptor slot wiring
- compute dispatch dimensions
- shader header constants for cluster dimensions / thread group sizes

## 15. Threading and task pools

`PlatformContext` owns four task pools:

- `HIGH_PRIORITY`
- `LOW_PRIORITY`
- `RENDERER`
- `ASSET_LOADER`

Examples of where this matters:

- DDS texture cache generation uses `HIGH_PRIORITY`
- render-pass preparation uses internal partitioning and shared locks around dirty ranges
- future async asset or GPU-prep work should reuse existing pool semantics instead of inventing ad-hoc threads

When touching thread-sensitive code, watch for:

- buffer dirty-range locks
- free-list locks
- command-buffer queue locks
- background asset processing that races resource finalization on the render thread/main thread

## 16. What to inspect before changing major systems

### If changing render-stage ordering

- `Kernel.cpp`
- render-target producer/consumer relationships
- post-processing assumptions
- clustered-light and depth input availability

### If changing descriptor bindings or shader resource layouts

- `GFXDevice::initDescriptorSets()`
- shader declarations
- reflection generation
- OpenGL shader binding code
- Vulkan descriptor-buffer / push-descriptor code

### If changing geometry upload

- `DVDConverter`
- `MeshImporter`
- `VertexBuffer`
- submesh partition assumptions

### If changing texture upload or cache behavior

- `Texture.cpp`
- `ImageTools.cpp`
- `glTexture.cpp`
- `vkTexture.cpp`

### If changing GPU memory behavior

- GL: `glMemoryManager`, `glBufferImpl`
- VK: `VKWrapper`, `vkBufferImpl`, `vkTexture`

## 17. Practical debugging heuristics

- If both GL and VK break, start in shared code:
  - `GFXDevice`
  - `ShaderProgram`
  - `RenderPassExecutor`
  - shared resource descriptors
- If only GL breaks, suspect:
  - persistent mapped buffer usage
  - sync/locking
  - GL-specific binding/state translation
- If only VK breaks, suspect:
  - image/buffer usage flags
  - layout transitions
  - staging copies
  - descriptor-buffer / push-descriptor interactions
- If only imported assets break, suspect:
  - cache format mismatches
  - attribute-presence assumptions
  - LoD/index partitioning
  - array/cubemap texture face ordering

## 18. Suggested first reads for a new agent

Read in this order if you need fast situational awareness:

1. `Source/Core/Kernel.cpp`
2. `Source/Platform/Video/GFXDevice.cpp`
3. `Source/Rendering/RenderPass/RenderPassExecutor.cpp`
4. `Source/Platform/Video/Shaders/ShaderProgram.cpp`
5. `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp`
6. `Source/Platform/Video/RenderBackend/Vulkan/Buffers/vkBufferImpl.cpp`
7. `Source/Platform/Video/RenderBackend/Vulkan/Textures/vkTexture.cpp`
8. `Source/Platform/Video/RenderBackend/OpenGL/Buffers/glMemoryManager.cpp`
9. `Source/Geometry/Importer/MeshImporter.cpp`
10. `Source/Utility/ImageTools.cpp`

That sequence gives the best payoff for understanding:

- the frame graph
- command submission
- shared shader/resource layout
- Vulkan-specific constraints
- memory/upload behavior
- asset ingestion
