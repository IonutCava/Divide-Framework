# GPU resources and backend conventions

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp`: device/queue selection and feature/extension enablement.
- `Source/Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp`: VMA creation, backend execution, viewport/scissor conventions.
- `Source/Platform/Video/RenderBackend/Vulkan/Buffers/vkBufferImpl.cpp` and `Textures/vkTexture.cpp`: Vulkan allocation/upload.
- `Source/Platform/Video/RenderBackend/OpenGL/Buffers/glMemoryManager.cpp` and `glBufferImpl.cpp`: pooled mapped allocations.
- `Source/Platform/Video/RenderBackend/OpenGL/glLockManager.cpp`: buffer synchronization.
- `Source/Platform/Video/RenderBackend/OpenGL/Textures/glTexture.cpp`: immutable texture storage/upload.

`VKDevice::submitToQueue` profiles queue-lock acquisition separately from the
`vkQueueSubmit` call. Both are CPU wall times; neither alone measures GPU execution.
Vulkan draw profiling separates source-buffer resolution from the individual
direct and indirect draw API calls. Their CPU wall times can include validation
and driver work and are not GPU execution times.

The shared OIT target family packs revealage into the alpha of its RGBA16F
normal attachment. DISPLAY and planar reflection/refraction no longer allocate a
separate R16F revealage image. This removes two logical bytes per pixel/sample
from each target's internal colour storage (plus any corresponding resolve image);
actual allocation savings depend on backend alignment and compression. Clear and
separate-alpha blend rules are in [Rendering](rendering.md).

## Vulkan capabilities

Device extension/feature notes below were checked in `VKDevice::VKDevice` at
commit `6b0a49883`; recheck when device selection changes. This source review
does not establish runtime support on other devices.

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
`EnumeratePhysicalDeviceTools` initializes every tool-property `sType`, retries
`VK_INCOMPLETE` with a fresh count, and reports errors or an unstable list
instead of publishing partial properties. The Vulkan specification requires
layers to respect the supplied array capacity; retries cannot undo an
out-of-bounds write by a nonconforming layer.

## OpenGL memory

`WindowManager::applyAPISettingsPreCreate` explicitly sets the no-error context
attribute for every attempt. SDL retains attributes between retries; enabling a
debug context must clear a previous non-debug no-error request.
`GLUtil::OnStartup` caches the context's `GL_MAX_LABEL_LENGTH` for validation
labels. `GLUtil::SetObjectLabel` passes an explicit bounded byte count, so
long names and non-null-terminated views cannot produce invalid label calls.

`glMemoryManager` creates large persistently mapped chunks, suballocates `Block`s, reuses compatible free blocks, and merges released free ranges.
`glBufferImpl` chooses target alignment, requests pooled allocations, keeps mapped CPU pointers, and uses `glLockManager` sync for frequent updates. Copy/readback buffers are used when needed.
Worker-context buffer initialization flushes its fence before another context
can wait on it. Buffer-fence waits require a current GL context, and a failed
wait prevents a mapped write instead of treating the range as unlocked.
`GLStateTracker` reapplies blend factors and equations when switching between
combined and separate alpha blending, even if the colour settings are unchanged.
Its reset marks the cached tessellation patch size unknown, so the first state
block sets `GL_PATCH_VERTICES` instead of assuming OpenGL's three-vertex default
matches the engine's four-control-point terrain patches.
Shared uniform pages use coherent persistent mappings. Before recycling a
frame slot, `GL_API::frameStarted` waits for the main frame fence and the
flushed completion fences of separately submitted window contexts. A failure
stops frame reuse. During teardown, bounded waits must complete every secondary
context fence and a newly flushed main-context fence before any tracked fences
are released. A missing or failed fence leaves completion unproven;
`GFXDevice` does not continue its renderer teardown. A reported GL reset is
classified as device loss in smoke mode so the supervisor verifies same-GPU
recovery.

Logical buffers are not normally one-to-one with GL allocations. Update frequency and host visibility affect allocation/sync strategy.
For GL-only performance issues, inspect allocation churn, contention, sync, and unnecessary buffer recreation.

Textures use immutable `glTextureStorage*` storage and DSA mip/layer uploads; the driver manages residency, with no custom texture allocator.
Both backends allocate the mip count selected by the shared texture policy and
upload no more than those levels. OpenGL sets `GL_TEXTURE_MAX_LEVEL` to the last
allocated level. The full mip count includes 3D texture depth; Vulkan mip blits
halve the depth extent along with width and height while preserving array layers.
Compressed partial-chain handling is described in the [asset pipeline](asset-pipeline.md#texture-flow).
For uploads submitted from a shared worker context, `glTexture` flushes and
waits for an upload fence, then calls `glFinish` to establish shared-object
completion before publishing the texture. This also covers
procedural uploads after resource creation and avoids requiring the main thread
to finalize a synchronous worker load. The bounded wait reports failure rather
than exposing incomplete uploads; main-context uploads remain ordered on that
context. Fence counters are atomic because worker contexts also create and
destroy sync objects.

## Vulkan memory

`VKWrapper.cpp` creates a global VMA allocator after device creation; both buffers and images reuse it.
At shutdown it logs detailed VMA allocation statistics before destroying a
nonempty allocator, so leaked allocation names are available even if VMA asserts.
VMA's routine call trace is disabled by default. Set
`debug.renderer.enableVMADebugLogging` in `XML/config.xml` before Vulkan
initialization to enable it; this does not disable VMA assertions or leak reports.

At commit `6b0a49883`, `VMABuffer`, `AllocatedImage`, cached image views and
samplers retire through `VK_API::RegisterCustomAPIDelete(..., true)`. Shader-reload
pipelines use the same transient queue, whose entries are delayed by
`MAX_FRAMES_IN_FLIGHT + 1`; pipeline-cache teardown instead destroys pipelines
directly after requesting device idle. Other deletes may use the device queue.
Do not assume every Vulkan resource follows the transient timer or treat it as
a substitute for establishing completion for new resource pools.

The fixed-capacity Vulkan sampled-image binding cache stores non-owning
`VkDescriptorImageInfo` values for immutable resource revisions. Revision
ownership remains with retained binding records/merged descriptor slots; the
cache does not extend texture or image-view lifetime. Its entries are only
reused for the exact revision and descriptor slot and are cleared before device
teardown. OpenGL's compiled sampled-image cache likewise does not own native
objects: sampler names remain owned by the backend sampler map, and every use
still touches the texture-view cache before queuing the bind.

- `vkBufferImpl` generally prefers device-local memory. Frequently updated buffers request persistent mapping; initial uploads temporarily map host-visible allocations when host access was requested. Initial and post-creation writes flush non-coherent mapped ranges before GPU use.
- Non-mappable buffers and initial allocations without VMA host-access flags use staging allocations and explicit transfer commands. Initial and later staged writes flush the staging range before the transfer. Inspect mapping assumptions, staging lifetime, copies, and synchronization separately.
- `vkTexture` derives image usage flags from engine `ImageUsage`, requests device-preferred VMA allocations without forcing dedicated memory, flushes mapped staging writes before uploads, explicitly transitions layouts, and can generate mipmaps on the GPU. VMA may suballocate small images or choose dedicated memory when appropriate or required.

The shared uniform arena requires direct persistent mapping and rejects a
staging-only allocation. Its slot stride is aligned to both the uniform-buffer
offset alignment and `nonCoherentAtomSize`, so flushing one changed snapshot
cannot touch another in-flight snapshot. The requested descriptor range is the
logical block size; OpenGL may round its bound range up to its offset alignment.
Host writes and non-coherent flushes precede queue submission. Page writes still
honor the buffer's `LockManager` range waits, including worker-context setup.
The arena does not enqueue a write-time `BufferLock` as its lifetime proof:
immutable snapshots are retired against all consuming frame slots instead.
Vulkan's base `LockManager` resets a range's sync bookkeeping rather than
waiting on a GPU fence, so it cannot establish that final-consumer completion.
Pages stay alive until GPU completion is established during teardown. A failed
wait reports a smoke failure or terminates an interactive process with a flushed
error, without continuing resource destruction or claiming completion.

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
per image. `VK_API::frameStarted` waits for the recycled slot of every retained
window before the uniform arena releases retired slots. Waiting on the main
window's recycled slot before scene preparation bounds in-flight uses of the
per-node buffer ring.
Indirect command pages use per-player rings that advance once per rendered
frame, never once per pass. The same fence waits protect their Vulkan reuse;
OpenGL additionally uses its buffer-range synchronization. Repeated stage-pass
indices use separate pages rather than overwriting commands already recorded
for that frame. Scene passes currently submit through the main window.
Particle uploads use the same frame-slot index, with independently allocated,
single-slot position/colour buffers for every distinct view in that frame.
They are reused only when the main-window frame fence has been waited, not
after an arbitrary number of cameras or CPU recording tasks. Particle scene
draws belong to the main window; moving them to independently submitted windows
requires extending this lifetime contract. Every consuming pass also records
the upload range locks so OpenGL waits for the final consumer before reuse.
Swapchain recreation waits for device idle for every window, with queue
operations synchronized by Vulkan queue handle. Image views and frame command
buffers are released during teardown, and a failed recreation is retried
before another acquisition. A failed reset, submission, or fence wait latches
the frame error and stops further slot reuse; the backend does not wait on a
fence left unsignaled by a failed submission. Device loss still requires the
external same-GPU recovery procedure before another smoke attempt.
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
from the requested clear value. Attachments use `LOAD` unless the pass explicitly
clears them; the previous contents are available only if they were stored.
`_keepMSAADataAfterResolve` controls the multisample store operation, not
whether the current pass loads retained data.
MSAA pipelines shade once per pixel on both backends: Vulkan leaves
`sampleShadingEnable` off, matching OpenGL, which never enables
`GL_SAMPLE_SHADING`. Shaders that read `gl_SampleID` or declare `sample` inputs
still run per sample. Forcing `minSampleShading = 1` previously supersampled
every MSAA pass and cost about 8 ms of DISPLAY GPU time in WarScene.
Resolve-image usage trackers change
when their layout barriers are recorded. An enabled resolve target transitions
to shader-read after a pass and back to attachment use before a later resolve,
regardless of whether the multisampled attachment contents are retained.
Vulkan pipeline variants are keyed by the complete dynamic-rendering signature:
colour attachment count and formats, depth and stencil formats, view mask, and
sample count. The compiled-pipeline key excludes dynamic render state; blend
and colour-write state remain in the key when extended dynamic blend state is
unavailable. The logical pipeline hash still includes those states so command
submission reapplies changes. Enabling stencil also reapplies its masks,
reference and operations after a disabled interval. A graphics pipeline retained
across passes is rebound for the new signature before descriptors and draw
submission. Swapchain passes begin with a
fresh rendering-create-info, so offscreen depth formats cannot leak into them.
Offscreen colour attachment counts stop at the highest enabled slot. Disabled
slots before it remain undefined, preserving fragment output locations; trailing
disabled slots are omitted from both rendering and pipeline creation.
Vertex attribute descriptions are filtered against the vertex shader's reflected
input locations; binding descriptions and missing-input validation are unchanged.
Vulkan tracks vertex buffer handles and offsets per binding, plus index buffer
handle, offset and type, for each window's active command recording. Identical
bindings are not reissued; a new recording resets the cache, while resuming a
nested window preserves its own state. Vertex binding indices outside the
tracked attribute range are always bound. Buffer destruction remains deferred
so a cached handle cannot be reused during its recording.
Transitions requested from or to shader-read use the same layouts for resolve
images as other textures; being a resolve image does not imply its current layout.
Blits of retained depth and colour attachments transition to transfer layouts
and back; the return barriers include attachment reads as well as writes so a
later pass can load the retained contents.
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

### Vulkan smoke readback and recovery

The opt-in smoke mode enables `VK_EXT_device_fault` only when the selected GPU
reports both the extension and its feature. On a reported device loss it
flushes the first failure, attempts bounded fault details before termination,
and explicitly records unavailable/failed collection. Vendor binary evidence
is capped and retained locally. A watchdog bounds the whole capture; it is not
a guarantee against a driver-wide hang.
When validation reports the `vkQueueSubmit` device-loss result inside the
tracked submit call, the callback claims and flushes the first failure but
defers termination only until that call returns. The submit wrapper then
collects fault details outside the callback and fails; all other validation
failures retain the immediate shutdown path.

For the smoke image, `vkTexture::readDataForSmoke` accepts a single-layer,
single-sample RGBA8/BGRA8 scene target. It transitions the image on the graphics
queue, copies into a mapped readback buffer, waits on a dedicated fence,
invalidates the VMA allocation and only then copies/saves the pixels. The
staging buffer follows deferred destruction. This path does not use the immediate
command ring. The older generic Vulkan `readData(VkCommandBuffer, ...)` path
copies mapped memory before its recorded commands complete and must not be
used as evidence of a completed image.

When an editor capture is requested, the main swapchain also requires
`VK_IMAGE_USAGE_TRANSFER_SRC_BIT`; an unsupported surface fails the smoke run
explicitly and ordinary swapchains are unchanged. After the final ImGui pass,
the main smoke swapchain defers its presentation transition until window flush.
The same frame command buffer transitions the colour attachment directly to a
transfer source, copies it to a host-readable staging buffer, and then
transitions it for presentation. Other smoke frames transition directly from
the attachment to presentation. Multiple screen passes keep the attachment
layout and synchronize between passes. The runner waits for a successful
present, while the backend waits for that frame's fence and invalidates mapped
memory before saving. The capture supports the selected RGBA/BGRA 8-bit and packed 10-bit
swapchain formats; conversion handles channel order and the swapchain's
top-down origin. It contains neither window decorations nor detached windows.

`Tools/VulkanRecoveryProbe.cpp` identifies the same device UUID, creates a new
logical device, submits an empty valid command buffer and waits for its fence.
The runner backs off between bounded probe processes; enumeration alone is not
recovery. See [Framework smoke diagnosis](rendering.md#framework-smoke-diagnosis).

### OpenGL smoke readback

OpenGL smoke mode verifies the expected Vulkan device UUID against the active
context's `GL_EXT_memory_object` or `GL_EXT_semaphore` device UUIDs before
recording GPU identity. It reads the single-sample scene texture and the main
default framebuffer after command replay and before `SDL_GL_SwapWindow`.
Pixel-pack state, the pixel-pack buffer and read framebuffer bindings are
restored after readback. The returned bottom-up pixels are passed to
`ImageTools::SaveImage`, which flips them for PNG output. A successful swap
is required before capture completion; the supervisor promotes the editor
image only after clean shutdown.

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
- `SceneViewWindow` samples the composed `BACK_BUFFER` rather than `SCREEN`, so its game view includes in-game text, CEGUI and debug overlays.
- `DeviceInformation::_screenSRGB` reports default framebuffer/swapchain encoding. GL queries colour encoding and disables screen `GL_FRAMEBUFFER_SRGB` if unsupported; Vulkan uses `*_SRGB` format detection.
- Without screen encoding, final screen blit, editor ImGui, and splash shaders encode/skip linearisation appropriately.
- `Assets/Shaders/GLSL/IMGUI.glsl` linearises vertex colours and only plain UNORM non-render-target textures. `lineariseTexture` is set per texture bind by `Editor::renderDrawList`; render targets, sRGB, and float textures are sampled as linear.

Non-sRGB screen fallback paths can blend translucent UI in encoded space.
Linear-space fallback compositing is deferred; do not claim identical alpha
blending between sRGB and non-sRGB presentation paths.

ImGui atlas textures honour deferred destruction: `Editor::updateFontTextures`
destroys `WantDestroy` textures only after `UnusedFrames > 0`, then clears TexID.

Keep these conventions shared across backends; do not add compensating flips or gamma transforms in individual client systems.
