# GPU resources and backend conventions

Non-sRGB screen fallback paths can blend translucent UI in encoded space.
Linear-space fallback compositing is deferred; do not claim identical alpha
blending between sRGB and non-sRGB presentation paths.

<!-- Copyright (c) 2026 DIVIDE-Studio -->

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

Commit `7b09ed5` raises the minimum supported version to Vulkan 1.4: device selection unconditionally requires `maintenance5`, `maintenance6`, and `pushDescriptor`, with no 1.3 retry. VMA also uses Vulkan API version 1.4.

- Required: `VK_EXT_custom_border_color`, `VK_EXT_descriptor_buffer`, `VK_KHR_push_descriptor`.
- Extended dynamic state 3 is used only when the queried blend-enable,
  blend-equation and colour-write-mask bits are all supported. Maintenance 7
  similarly requires its queried feature bit; extension presence alone is not enough.
- `VK_EXT_mesh_shader` is enabled when present, but its feature path is still incomplete.
- Mesh/task feature hookup remains commented out despite extension detection; see [rendering readiness](rendering.md).
- `VK_EXT_descriptor_heap` is logged when available but not enabled/used; it is a planned descriptor model.

Do not assume optional dynamic-state/mesh support just from extension presence. Audit feature chains and capability/fallback decisions.
Descriptor-buffer and push-descriptor support are required assumptions; descriptor layout/cache rules are in [shaders and descriptors](shader-descriptors.md).
`VK_API::initRenderingAPI` initializes the `sType` of every reported
`VkPhysicalDeviceToolProperties` before querying attached Vulkan tools.

## OpenGL memory

`WindowManager::applyAPISettingsPreCreate` explicitly sets the no-error context
attribute for every attempt. SDL retains attributes between retries; enabling a
debug context must clear a previous non-debug no-error request.

`glMemoryManager` creates large persistently mapped chunks, suballocates `Block`s, reuses compatible free blocks, and merges released free ranges.
`glBufferImpl` chooses target alignment, requests pooled allocations, keeps mapped CPU pointers, and uses `glLockManager` sync for frequent updates. Copy/readback buffers are used when needed.

Logical buffers are not normally one-to-one with GL allocations. Update frequency and host visibility affect allocation/sync strategy.
For GL-only performance issues, inspect allocation churn, contention, sync, and unnecessary buffer recreation.

Textures use immutable `glTextureStorage*` storage and DSA mip/layer uploads; the driver manages residency, with no custom texture allocator.

## Vulkan memory

`VKWrapper.cpp` creates a global VMA allocator after device creation; both buffers and images reuse it.
At shutdown it logs detailed VMA allocation statistics before destroying a
nonempty allocator, so leaked allocation names are available even if VMA asserts.

- `vkBufferImpl` generally prefers device-local memory. Frequently updated buffers request persistent mapping; initial uploads temporarily map host-visible allocations when host access was requested. Initial and post-creation writes flush non-coherent mapped ranges before GPU use.
- Non-mappable buffers and initial allocations without VMA host-access flags use staging allocations and explicit transfer commands. Initial and later staged writes flush the staging range before the transfer. Inspect mapping assumptions, staging lifetime, copies, and synchronization separately.
- `vkTexture` derives image usage flags from engine `ImageUsage`, requests device-preferred VMA allocations without forcing dedicated memory, flushes mapped staging writes before uploads, explicitly transitions layouts, and can generate mipmaps on the GPU. VMA may suballocate small images or choose dedicated memory when appropriate or required.

CPU-readable buffers (`_hostVisible`, required by `ShaderBuffer::readBytes`) request `HOST_ACCESS_RANDOM | MAPPED`. VMA therefore places them in host-visible, preferably cached memory and keeps them persistently mapped. `vkBufferImpl::readBytes` invalidates non-coherent ranges before copying. If a mapping is missing, it maps temporarily. It rejects non-host-visible memory instead of mapping it. Reads are bounded to the selected logical ring slot before the slot offset is applied. GPU writers must record a `GPU_WRITE_TO_CPU_READ` lock, which becomes a shader-write to host-read barrier; the barrier alone does not wait for GPU completion. `READ_BUFFER_DATA` requests are queued for deferred host reads: OpenGL inserts a fence and polls it with a zero timeout, while Vulkan associates the request with the active frame fence and reads it after that fence has been waited during frame reuse. Neither backend blocks the render thread for an individual readback.

The occlusion counter initializes every ring slot, serializes slot selection
when recording, and captures the intended slots for reads and clears. All
counter writes are disabled when statistics are not requested. A slot remains
reserved until its asynchronous readback completes, preventing wraparound from
overwriting an in-flight result. If no counter slot is available, only that
frame's optional statistics are skipped; culling and rendering continue. The
reported count can therefore lag by a few frames, and older completions cannot
replace a newer result.

Vulkan resets the frame fence only when submitting, not before image
acquisition. If acquisition fails, the window's queued commands are discarded
instead of being replayed into an unrecorded command buffer. Resuming a parent
window after a nested window reuses its already-acquired frame, or preserves
its skipped state until that window is flushed. Submissions wait on the
transfer semaphores claimed for that submission. A suboptimal acquisition
still consumes and presents the acquired image before recreation.
Each window has exactly `Config::MAX_FRAMES_IN_FLIGHT` command-buffer and fence
slots, independent of swapchain image count; render-finished semaphores remain
per image. Waiting on the main window's recycled slot before scene preparation
bounds in-flight uses of the per-node buffer ring.
Swapchain recreation waits for device idle for every window, with queue
operations synchronized by Vulkan queue handle. Image views and frame command
buffers are released during teardown, and a failed recreation is retried
before another acquisition. A queue-submission failure after fence reset is
not yet recovered: it can leave an unsignaled fence.
An immediate upload submission failure is logged and latches the Vulkan backend
into a stopping state instead of publishing its unsignaled completion semaphore
or waiting on its fence. Pending semaphores are destroyed during device teardown
after the idle attempt; a new backend initialization resets the latch.

Changes to writable/readable images must audit usage flags, transitions, staging lifetime, and consumers together.
Sampleable Vulkan render-target images are cleared on the graphics queue before
entering shader-read layout, and their attachment trackers reflect that initial
state. The clear covers every mip and layer (zero colour, depth one and stencil
zero), so resources such as the previous-screen image can be sampled before
their first render-target blit. The first attachment write explicitly transitions
the image to attachment use. This adds initialization work when the resource is
created or resized; it is not a per-frame clear.
`vkRenderTarget::begin` resets the dynamic-rendering attachment information on
every pass: a pass with automatic MSAA resolve disabled must not inherit the
previous pass's resolve mode or image view. Colour clears copy all four channels
from the requested clear value. Resolve-image usage trackers change
when their layout barriers are recorded. An enabled resolve target transitions
to shader-read after a pass and back to attachment use before a later resolve,
regardless of whether the multisampled attachment contents are retained.
Vulkan pipeline variants are keyed by the complete dynamic-rendering signature:
colour attachment count and formats, depth and stencil formats, view mask, and
sample count. A graphics pipeline retained across passes is rebound for the new
signature before descriptors and draw submission. Swapchain passes begin with a
fresh rendering-create-info, so offscreen depth formats cannot leak into them.
Offscreen colour attachment counts stop at the highest enabled slot. Disabled
slots before it remain undefined, preserving fragment output locations; trailing
disabled slots are omitted from both rendering and pipeline creation.
Vertex attribute descriptions are filtered against the vertex shader's reflected
input locations; binding descriptions and missing-input validation are unchanged.
Transitions requested from or to shader-read use the same layouts for resolve
images as other textures; being a resolve image does not imply its current layout.
The previous-screen blit relies on render-target transitions instead of a
separate one-time barrier that bypasses the attachment tracker.
Cubemap render-target selections identify a cube and one face when their layer
count is one; Vulkan attachment views address exactly that physical face.
Layered selections with `ALL_LAYERS` or multiple cubes select complete cubes
starting at face zero. Colour and depth attachment views must select the same
number of layers. Attachment transitions cover all layers of the selected
mip because usage is tracked per texture. Retaining an attachment across passes
does not widen the later transition to other mips, which remain shader-readable;
the retained passes must use the same mip.
Shared texture views use cube indices for cube views and physical face indices
for 2D views of cube storage. Their default ranges cover all array layers, and
Vulkan uses the same conversion for image views and explicit layout barriers.
`VK_KHR_unified_image_layouts`, when enabled with its feature, guarantees that
`GENERAL` is efficient where supported, but it does not eliminate synchronization
or the special initialization and presentation transitions. The engine does not
currently enable it; do not assume the same guarantee on unsupported devices.
`vkTexture::GetAspectFlags` follows depth/stencil packing, not attachment usage:
sampled-only depth fallbacks must upload and create views with a depth aspect.
For arrays, cubemaps, mip generation, and partial updates, inspect backend `loadDataInternal(...)` overloads; shared decode/import is in [asset pipeline](asset-pipeline.md).
Queue ownership is covered in [threading](threading.md).

## Coordinate convention

OpenGL is the reference. Client code does not branch on the API for Y: projection/clip space is Y-up, NDC-to-UV math is shared, texture origin is bottom-left (loaders flip on import), and viewport/scissor rectangles use bottom-left origin.

- Vulkan offscreen targets use an unflipped viewport and pass-through scissor; texel row 0 is clip-space bottom, matching GL sampling, `texelFetch(gl_FragCoord)`, copies, and readbacks. Dynamic front face is inverted to compensate.
- Vulkan tessellation pipelines explicitly use a lower-left domain origin to match OpenGL's tessellation winding. This is separate from the viewport/front-face convention.
- Only the Vulkan swapchain uses a negative-height viewport and top-left scissor conversion (`IsSwapChainTargetActive()` in `VKWrapper.cpp`); its front face is not inverted.
- Shaders drawing to the screen must not rely on `gl_FragCoord.y`.
- Both APIs use zero-to-one depth: OpenGL enables `GL_ZERO_TO_ONE` via `glClipControl`, Vulkan's viewport depth range is 0..1, and camera projection builders default to `zeroToOneDepth=true`. Depth reconstruction must not apply `0.5*z+0.5` remapping.
- Future backends should flip only at presentation and adjust winding internally.

## Colour-space convention

- Lighting is linear HDR: `RGBA16F` for `SCREEN`, reflection/refraction, and probes; bloom is `R11G11B10F`. Tonemapping produces linear LDR.
- `BACK_BUFFER` is `RGBA8` sRGB storage: hardware write encoding/read decoding reduces banding; UI blends in linear space.
- `DeviceInformation::_screenSRGB` reports default framebuffer/swapchain encoding. GL queries colour encoding and disables screen `GL_FRAMEBUFFER_SRGB` if unsupported; Vulkan uses `*_SRGB` format detection.
- Without screen encoding, final screen blit, editor ImGui, and splash shaders encode/skip linearisation appropriately.
- `Assets/Shaders/GLSL/IMGUI.glsl` linearises vertex colours and only plain UNORM non-render-target textures. `lineariseTexture` is set per texture bind by `Editor::renderDrawList`; render targets, sRGB, and float textures are sampled as linear.

ImGui atlas textures honour deferred destruction: `Editor::updateFontTextures`
destroys `WantDestroy` textures only after `UnusedFrames > 0`, then clears TexID.

Keep these conventions shared across backends; do not add compensating flips or gamma transforms in individual client systems.
