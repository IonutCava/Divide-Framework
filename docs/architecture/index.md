# Agent codebase reference

<!-- Copyright (c) 2026 DIVIDE-Studio -->

These guides are task-scoped notes, not the framework's human-facing feature overview.
Start with the repository-wide instructions in [AGENTS.md](../../AGENTS.md), then read only the relevant guides.
Source paths below and in the guides are relative to the repository root.

| Guide | Scope and starting points |
|---|---|
| [Rendering](rendering.md) | `Kernel`, `GFXDevice`, `RenderPassExecutor`, `Renderer`, PostFX; stage dependencies, profiling and smoke diagnosis |
| [Shaders and descriptors](shader-descriptors.md) | `ShaderProgram`, `ShaderDataUploader`, `GLSLToSPIRV`, backend shader programs; bindings and cache compatibility |
| [GPU resources](gpu-resources.md) | `vkDevice`, `VKWrapper`, GL/Vulkan buffer and texture implementations; allocation, capabilities, coordinate/colour conventions |
| [Asset pipeline](asset-pipeline.md) | `MeshImporter`, `DVDConverter`, `VertexBuffer`, `Texture`, `ImageTools`; import, cache, and upload |
| [Scene loading](scene-loading.md) | `ProjectManager`, `ScenePool`, `SceneGraph`, transform and bounds systems; scene selection, XML and activation |
| [Editor windows](editor.md) | `Editor`, `DockedWindow`, property/undo handling, gizmos, output and persisted layouts |
| [Threading](threading.md) | `PlatformContext`, task pools, render preparation, resource synchronization, Vulkan queue usage |
| [Networking](networking.md) | `Client`, `Connection`; context ownership, heartbeat cancellation and loopback test initialization |

## Assertions and fatal calls

`Source/Platform/Headers/PlatformDefines.h` routes assertions through
`Assert::Callback`, which may continue after a Debug assertion. The
`DIVIDE_UNEXPECTED_CALL` and `DIVIDE_UNEXPECTED_CALL_MSG` macros instead report
the source and message, flush the console when available, and abort in every
build even when assertions can continue. They do not perform normal shutdown.
Expected-call and GPU-validation macros retain their existing assertion and
validation-gating behavior. Use a recoverable assertion or explicit error
return when a fallback is intended; changing the unexpected-call contract
affects all its callers.

## Math SIMD alignment

`Source/Core/Math/Headers/MathVectors.h` declares `vec4` inside `#pragma pack(1)`.
GCC/Clang cap member alignment under packing, which would drop `SimdVector<F32>`'s
`alignas(16)`; Clang then emits aligned SSE moves through misaligned temporaries
(for example quaternion products) and segfaults on x86. `vec4` therefore carries a
class-level `alignas(Vec4Alignment<T>)`: 16 for SIMD-backed types, 1 otherwise, so
non-SIMD vectors keep their packed layout. MSVC, clang-cl and ARM did not expose the
fault. Keep the `static_assert`s and `MathVectorTests.cpp` alignment test when
changing these types or their packing.

## Debugging routes

- Both GL and Vulkan fail: start in shared `GFXDevice`, `ShaderProgram`, `RenderPassExecutor`, and descriptor setup.
- GL only: inspect persistent mapped buffers, locks/sync, allocation churn, and GL binding/state translation; the [smoke runner](rendering.md#framework-smoke-diagnosis) can capture a same-GPU comparison.
- Vulkan only: inspect usage flags, image layouts, staging lifetimes, and descriptor/push-descriptor state; the opt-in [smoke runner](rendering.md#framework-smoke-diagnosis) captures first-failure evidence.
- Imported assets only: inspect cache formats, attribute presence, LoD/index partitions, and array/cubemap ordering.

## Maintaining these notes

- Every PR/change must review and update the relevant documentation alongside the implementation; keep one authoritative explanation and cross-link related guides.
- If the affected subsystem has no guide or entry, create one using the existing pattern: scope and entry points, ownership/data flow, invariants, safe-change guidance, and known limitations. Add new guides to this index and the routing table in `AGENTS.md`.
- Preserve invariants, ownership, data flow, debugging entry points, and limitations rather than exhaustive file inventories.
- Capability/readiness notes are a migration of the existing repository reference, not a fresh end-to-end audit. Verify them against the current branch before implementation.
- For newly checked volatile claims, record the source symbols and the last-verified commit. Do not imply that a documentation reorganization validates runtime behavior.
- Add decision records under `docs/decisions/` only for durable choices with meaningful rationale, not session logs.
