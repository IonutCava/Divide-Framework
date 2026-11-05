# Agent codebase reference

<!-- Copyright (c) 2026 DIVIDE-Studio -->

These guides are task-scoped notes, not the framework's human-facing feature overview.
Start with the repository-wide instructions in [AGENTS.md](../../AGENTS.md), then read only the relevant guides.
Source paths below and in the guides are relative to the repository root.

| Guide | Scope and starting points |
|---|---|
| [Rendering](rendering.md) | `Kernel`, `GFXDevice`, `RenderPassExecutor`, `Renderer`, PostFX; stage dependencies and feature insertion points |
| [Shaders and descriptors](shader-descriptors.md) | `ShaderProgram`, `ShaderDataUploader`, `GLSLToSPIRV`, backend shader programs; bindings and cache compatibility |
| [GPU resources](gpu-resources.md) | `vkDevice`, `VKWrapper`, GL/Vulkan buffer and texture implementations; allocation, capabilities, coordinate/colour conventions |
| [Asset pipeline](asset-pipeline.md) | `MeshImporter`, `DVDConverter`, `VertexBuffer`, `Texture`, `ImageTools`; import, cache, and upload |
| [Scene loading](scene-loading.md) | `ProjectManager`, `ScenePool`, `Scene`, `SceneGraph`; scene selection, XML and activation |
| [Threading](threading.md) | `PlatformContext`, task pools, render preparation, resource synchronization, Vulkan queue usage |
| [Networking](networking.md) | `Client`, `Connection`; context ownership, heartbeat cancellation and loopback test initialization |

## Debugging routes

- Both GL and Vulkan fail: start in shared `GFXDevice`, `ShaderProgram`, `RenderPassExecutor`, and descriptor setup.
- GL only: inspect persistent mapped buffers, locks/sync, allocation churn, and GL binding/state translation.
- Vulkan only: inspect usage flags, image layouts, staging lifetimes, and descriptor/push-descriptor state.
- Imported assets only: inspect cache formats, attribute presence, LoD/index partitions, and array/cubemap ordering.

## Maintaining these notes

- Every PR/change must review and update the relevant documentation alongside the implementation; keep one authoritative explanation and cross-link related guides.
- If the affected subsystem has no guide or entry, create one using the existing pattern: scope and entry points, ownership/data flow, invariants, safe-change guidance, and known limitations. Add new guides to this index and the routing table in `AGENTS.md`.
- Preserve invariants, ownership, data flow, debugging entry points, and limitations rather than exhaustive file inventories.
- Capability/readiness notes are a migration of the existing repository reference, not a fresh end-to-end audit. Verify them against the current branch before implementation.
- For newly checked volatile claims, record the source symbols and the last-verified commit. Do not imply that a documentation reorganization validates runtime behavior.
- Add decision records under `docs/decisions/` only for durable choices with meaningful rationale, not session logs.
