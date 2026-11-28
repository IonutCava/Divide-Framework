# Shaders and descriptors

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Platform/Video/Shaders/ShaderProgram.cpp` loads shader source and manages compilation and caches.
- `GLSLToSPIRV.cpp` compiles Vulkan shader stages.
- `ShaderDataUploader.cpp` serializes reflection and shader data.
- `GFXDevice.cpp` registers descriptor bindings.
- `Assets/Shaders/GLSL/` contains shader declarations and shared shader code.
- The OpenGL and Vulkan shader backends consume the shared program and descriptor data.

## Shader flow and caches

`ShaderProgram` loads GLSL, preprocesses it, creates SPIR-V where needed,
builds reflection data, and passes the results to the backend. OpenGL consumes
GLSL; Vulkan consumes SPIR-V. Reflection is shared by both.

Generated shader headers can contain configuration and feature limits. Send
each generated line through `AppendToShaderHeader` so its stage and content
contribute to the cache fingerprint. Bump the relevant cache version when
preprocessing, reflection, or compiled-binary semantics change.

OpenGL's SPIR-V ingestion path is not supported by the current configuration;
keep OpenGL on GLSL unless that path has been validated. Check
`XML/config.xml` and the shader cache settings before changing backend inputs.

Preprocessing must fail explicitly on missing shader sections, unresolved
includes, invalid reflection, or incompatible cache data. Do not silently drop
shader source or pass incomplete binary data to a backend.

Keep shader declarations, generated headers, reflection serialization, cache
compatibility, and both backend consumers consistent. A source change that
alters any of these contracts may require a cache-version update.

## Descriptor ownership and layout

The descriptor-set usage order reflects update frequency:

| Usage | Vulkan set index | Typical contents |
|---|---:|---|
| `PER_FRAME` | 0 | Environment and scene-wide data |
| `PER_PASS` | 1 | Pass inputs and lighting data |
| `PER_BATCH` | 2 | Camera, transforms, materials, and indirect commands |
| `PER_DRAW` | 3 | Material textures and per-draw data |

Use descriptor usage names in shaders, not raw set indices. Vulkan uses the
usage order directly; OpenGL flattens usage and slot into binding points.
`PER_DRAW` remains last. Changing the enum order requires updating serialized
reflection data.

Descriptor layouts and pipeline layouts are shared contracts. Keep shader
declarations, generated binding maps, reflection, and backend layout creation
in sync. Push descriptors are per-program; do not overwrite shared layouts with
program-specific layouts.

## Binding and cache lifetime

Material resource bindings are published as immutable revisions. Recorded
commands retain the revision they use, while package and view overrides remain
separate. Replacing a resource publishes a new revision; existing commands may
continue using the old one until their work completes.

Backend compiled-binding caches are non-owning. Their keys must identify the
resource revision and descriptor slot. A cache miss may cost work; a false hit
can bind stale resources. Clear these caches before tearing down the native
device or context.

Descriptor updates merge active slots until command-buffer flush. Omitted
slots can retain an earlier binding, so callers must explicitly replace slots
that are no longer valid. Fallback resolution may borrow an unchanged set or
use reusable scratch storage; scratch data must not outlive the synchronous
binding call.

Command coalescing must compare active bindings, not unused capacity. Reject
conflicting overlapping updates without partially changing the earlier
command. Keep resource ownership, binding state, and GPU completion as separate
concerns.

## Safe changes

When changing descriptor or shader bindings, review the complete path:

- `GFXDevice::initDescriptorSets()` and shader declarations.
- Generated headers, reflection data, and cache versions.
- OpenGL binding translation and Vulkan descriptor/push-descriptor layouts.
- Pipeline compatibility, dynamic offsets, and cache invalidation.
- Resource revisions, command-buffer ownership, and frame completion.

Use `Source/UnitTests/Test-Engine/ShaderCompilerTests.cpp` for shader and
reflection regressions. CPU-side checks do not replace backend execution or
image-based validation.
