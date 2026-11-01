---
applyTo: "Source/Rendering/**,Source/Platform/Video/**,Source/Managers/*RenderPass*,Source/Managers/Headers/*RenderPass*,Source/Core/Kernel.cpp,Assets/Shaders/**"
---

Read the relevant task-scoped guides linked from `AGENTS.md` before changing rendering code:

- `docs/architecture/rendering.md` for stages, command submission, lighting, and feature integration.
- `docs/architecture/shader-descriptors.md` for bindings, reflection, and caches.
- `docs/architecture/gpu-resources.md` for backend allocation, uploads, and coordinate/colour-space invariants.
- `docs/architecture/threading.md` for parallel preparation or queue/synchronization changes.

These references are shared with other agents; update them rather than duplicating subsystem notes here.
