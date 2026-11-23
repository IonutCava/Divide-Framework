# GPU resources and backend conventions

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Platform/Video/RenderBackend/Vulkan/vkDevice.cpp` selects the device and queues.
- `Source/Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp` owns Vulkan execution and VMA setup.
- Vulkan buffer and texture implementations own allocations, uploads, and transitions.
- OpenGL `glMemoryManager.cpp`, `glBufferImpl.cpp`, and `glLockManager.cpp` manage mapped storage and synchronization.
- OpenGL `glTexture.cpp` owns immutable texture storage and uploads.

## Resource lifetime

The CPU finishing a write or recording a command does not mean the GPU has
finished using the resource. Keep allocations, descriptors, upload data, and
command payloads alive until every consuming frame has completed.

Use the existing allocators, upload arenas, and deferred-destruction paths when
their ownership model fits. When adding a resource type or pool, define who
owns it, when it can be reused, and how teardown proves GPU completion. A
timeout or failed fence does not establish completion.

Frame-slot rings must advance at the frame boundary they represent, not once per
pass or view. Every consumer must capture the same slot and range selected by
the producer. Do not infer completion from elapsed frames alone when multiple
windows or queues can submit work.

## Device capabilities

Device selection must validate the feature bits an operation needs. Extension
presence alone does not establish support. Required capabilities fail device
initialization when unavailable; optional capabilities need a tested fallback.

Keep capability checks, feature enablement, and the code that uses a feature in
sync. Vulkan queue types also do not prove that a resource is transferred or
synchronized on that queue. Check queue ownership, barriers, and completion
before adding asynchronous work.

## OpenGL resources

`glMemoryManager` allocates persistently mapped chunks and suballocates buffer
ranges. `glBufferImpl` selects alignment and allocation based on the buffer's
usage and update pattern. `glLockManager` protects overlapping CPU/GPU access.
Do not treat a mapped pointer as proof that a range is safe to write.

Worker contexts share GL objects with the main context. A worker must publish
completed uploads with the synchronization required by the consumer before the
resource becomes visible. Buffer waits require a current context; a failed wait
must stop the write rather than mark the range available.

Textures use immutable storage and direct uploads. The GL state tracker owns
texture and sampler binding state. Invalidate tracked bindings before deleting
or reusing a texture name, and preserve the tracker state when adding queued
bind updates.

## Vulkan resources

`VKWrapper.cpp` creates the shared VMA allocator. Buffers and images use
device-preferred allocations where appropriate. Host reads or writes require
the matching VMA host-access flags; otherwise, use staging buffers and explicit
transfer commands. Flush non-coherent mapped ranges before GPU reads and
invalidate them before CPU reads.

Texture uploads use staging storage and explicit layout transitions. Keep image
usage flags, current layouts, barriers, and consumers consistent. A resource's
creation usage does not describe its current layout.

The uniform upload arena uses persistent host-visible memory and retains each
snapshot until all consuming frame slots complete. Align allocations and flush
ranges to backend requirements. Its lifetime rules are separate from
write-time buffer locks.

Descriptor and pipeline caches may hold non-owning native handles. Their keys
must identify the resource revision they describe, and they must be cleared or
invalidated before the underlying resources are destroyed.

## Readback and recovery

Readback paths must wait for the GPU work that produces the image before
accessing mapped data. The supervised smoke runner uses dedicated readback
paths and validates the captured image after a successful present. See
[Rendering smoke diagnosis](rendering.md#framework-smoke-diagnosis) for the
runner's safety contract.

### Vulkan smoke readback and recovery

Vulkan smoke readback transitions and copies the requested image, waits for its
completion fence, invalidates mapped memory when required, then saves the
pixels. Do not use a readback path that copies mapped memory before its recorded
commands complete.

After device loss or a suspected GPU hang, retain local evidence and verify
that a fresh device on the same GPU can submit work and complete a fence before
starting another renderer attempt. Device enumeration alone does not prove
recovery.

### OpenGL smoke readback

OpenGL smoke readback runs after command replay and before buffer swap. Restore
pixel-pack and framebuffer state after reading. A successful swap and clean
shutdown are required before accepting the capture.

## Coordinate convention

OpenGL is the reference for client coordinate behavior. Clip space is Y-up,
texture origin is bottom-left, and client shaders share the same NDC-to-UV
math. Loaders convert image origin during import.

Vulkan offscreen targets use the same texture orientation and depth convention.
Only the swapchain presentation path converts to the platform's presentation
origin. Keep flips and winding changes in the backend rather than adding
API-specific branches to client rendering code.

Both APIs use zero-to-one depth. Depth reconstruction must not apply the
OpenGL-style `0.5 * z + 0.5` remapping.

## Colour-space convention

Lighting uses linear HDR values. The final back buffer may use sRGB encoding;
the backend reports whether that encoding is available. If it is unavailable,
the final presentation path performs the required encoding.

Keep UI blending and texture sampling consistent with the target's encoding.
Render targets, sRGB textures, and floating-point textures have different
sampling behavior. Do not add gamma corrections to individual client systems
to compensate for a backend mismatch.

Scene View displays the composed game image, including the in-game GUI. See
[Editor windows](editor.md) for its ownership and sampling path.
