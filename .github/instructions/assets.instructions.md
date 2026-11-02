---
applyTo: "Source/Geometry/**,Source/Utility/ImageTools*,Source/Utility/Headers/ImageTools*,Source/Platform/Video/Textures/**,Source/Platform/Video/Buffers/VertexBuffer/**"
---

Read `docs/architecture/asset-pipeline.md` before changing geometry import, LoD, texture decode/cache, or vertex-buffer upload.
For backend upload/lifetime changes, also read `docs/architecture/gpu-resources.md` and `docs/architecture/threading.md`.
Preserve shared mesh/submesh ownership and cross-platform cache behavior. Keep detailed notes in the shared guides, not here.
