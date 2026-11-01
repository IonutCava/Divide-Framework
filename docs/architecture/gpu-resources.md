# GPU resources and backend conventions

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp`: device/queue selection and feature/extension enablement.
- `Source/Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp`: VMA creation, backend execution, viewport/scissor conventions.
- `Source/Platform/Video/RenderBackend/Vulkan/Buffers/vkBufferImpl.cpp` and `Textures/vkTexture.cpp`: Vulkan allocation/upload.
- `Source/Platform/Video/RenderBackend/OpenGL/Buffers/glMemoryManager.cpp` and `glBufferImpl.cpp`: pooled mapped allocations.
- `Source/Platform/Video/RenderBackend/OpenGL/glLockManager.cpp`: buffer synchronization.
- `Source/Platform/Video/RenderBackend/OpenGL/Textures/glTexture.cpp`: immutable texture storage/upload.

## Vulkan capabilities

Device extension/feature notes below were checked in `vkDevice.cpp` at commit `1508b95`; recheck on later branches.

- Required: `VK_EXT_custom_border_color`, `VK_EXT_descriptor_buffer`, `VK_KHR_push_descriptor`.
- Enabled when present: `VK_EXT_extended_dynamic_state_3`, `VK_KHR_maintenance7`, and `VK_EXT_mesh_shader`.
- Mesh/task feature hookup remains commented out despite extension detection; see [rendering readiness](rendering.md).
- `VK_EXT_descriptor_heap` is logged when available but not enabled/used; it is a planned descriptor model.

Do not assume optional dynamic-state/mesh support just from extension presence. Audit feature chains and capability/fallback decisions.
Descriptor-buffer and push-descriptor support are required assumptions; descriptor layout/cache rules are in [shaders and descriptors](shader-descriptors.md).

## OpenGL memory

`glMemoryManager` creates large persistently mapped chunks, suballocates `Block`s, reuses compatible free blocks, and merges released free ranges.
`glBufferImpl` chooses target alignment, requests pooled allocations, keeps mapped CPU pointers, and uses `glLockManager` sync for frequent updates. Copy/readback buffers are used when needed.

Logical buffers are not normally one-to-one with GL allocations. Update frequency and host visibility affect allocation/sync strategy.
For GL-only performance issues, inspect allocation churn, contention, sync, and unnecessary buffer recreation.

Textures use immutable `glTextureStorage*` storage and DSA mip/layer uploads; the driver manages residency, with no custom texture allocator.

## Vulkan memory

`VKWrapper.cpp` creates a global VMA allocator after device creation; both buffers and images reuse it.

- `vkBufferImpl` generally prefers device-local memory. Frequently updated/host-visible buffers request mapped VMA host access for direct writes.
- Non-mappable buffers use staging allocations and explicit transfer commands. Inspect mapping assumptions, staging lifetime, copies, and synchronization separately.
- `vkTexture` derives image usage flags from engine `ImageUsage`, usually uses dedicated device-preferred VMA allocations, uploads through staging, explicitly transitions layouts, and can generate mipmaps on the GPU.

Changes to writable/readable images must audit usage flags, transitions, staging lifetime, and consumers together.
For arrays, cubemaps, mip generation, and partial updates, inspect backend `loadDataInternal(...)` overloads; shared decode/import is in [asset pipeline](asset-pipeline.md).
Queue ownership is covered in [threading](threading.md).

## Coordinate convention

OpenGL is the reference. Client code does not branch on the API for Y: projection/clip space is Y-up, NDC-to-UV math is shared, texture origin is bottom-left (loaders flip on import), and viewport/scissor rectangles use bottom-left origin.

- Vulkan offscreen targets use an unflipped viewport and pass-through scissor; texel row 0 is clip-space bottom, matching GL sampling, `texelFetch(gl_FragCoord)`, copies, and readbacks. Dynamic front face is inverted to compensate.
- Only the Vulkan swapchain uses a negative-height viewport and top-left scissor conversion (`IsSwapChainTargetActive()` in `VKWrapper.cpp`); its front face is not inverted.
- Shaders drawing to the screen must not rely on `gl_FragCoord.y`.
- Future backends should flip only at presentation and adjust winding internally.

## Colour-space convention

- Lighting is linear HDR: `RGBA16F` for `SCREEN`, reflection/refraction, and probes; bloom is `R11G11B10F`. Tonemapping produces linear LDR.
- `BACK_BUFFER` is `RGBA8` sRGB storage: hardware write encoding/read decoding reduces banding; UI blends in linear space.
- `DeviceInformation::_screenSRGB` reports default framebuffer/swapchain encoding. GL queries colour encoding and disables screen `GL_FRAMEBUFFER_SRGB` if unsupported; Vulkan uses `*_SRGB` format detection.
- Without screen encoding, final screen blit, editor ImGui, and splash shaders encode/skip linearisation appropriately.
- `Assets/Shaders/GLSL/IMGUI.glsl` linearises vertex colours and only plain UNORM non-render-target textures. `lineariseTexture` is set per texture bind by `Editor::renderDrawList`; render targets, sRGB, and float textures are sampled as linear.

Keep these conventions shared across backends; do not add compensating flips or gamma transforms in individual client systems.
