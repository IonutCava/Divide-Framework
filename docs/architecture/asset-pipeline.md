# Asset pipeline

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Geometry/Importer/MeshImporter.cpp`: geometry/animation cache and import orchestration.
- `Source/Geometry/Importer/DVDConverter.cpp`: Assimp normalization, optimization, LoD generation.
- `Source/Geometry/Shapes/Mesh.cpp` and `SubMesh.cpp`: runtime ownership and partitions.
- `Source/Platform/Video/Buffers/VertexBuffer/VertexBuffer.cpp`: shared geometry buffer compaction/upload.
- `Source/Platform/Video/Textures/Texture.cpp`: shared texture loading and creation.
- `Source/Utility/ImageTools.cpp`: image decode, inspection, DDS cache generation.

## Geometry flow and ownership

`MeshImporter` attempts `.DVDGeom` / `.DVDAnim` cache loads. On a miss or disabled cache, Assimp conversion through `DVDConverter` creates `Import::ImportData`, then runtime meshes/submeshes and a shared `VertexBuffer`.

`DVDConverter` applies Assimp post-processing, normalizes `Import::SubMeshData`, extracts optional tangent/bone/skinning data, and runs meshoptimizer vertex remap, vertex-cache, overdraw, and vertex-fetch optimization.
For sufficiently large meshes, `meshopt_simplify` generates lower-LoD index buffers.

One model owns one shared `VertexBuffer`. Submeshes store partitions, triangle ranges, and material data rather than separate vertex allocations; `SubMesh::geometryBuffer()` resolves the parent's buffer.

`VertexBuffer` owns a GPU vertex buffer and index buffer. `commitData()` compacts the CPU layout to present attributes.
Full layout changes call `setBuffer(...)`; incremental changes call `updateBuffer(...)`.
Indices may use 16-bit packing when vertex count permits. CPU copies can be discarded when dynamic updates are unnecessary.

Before changing geometry upload, check cache compatibility, attribute-presence tracking, compact layout, index width, LoD/partition correctness, and parent/submesh ownership.

## Texture flow

`Texture::loadInternal()` treats comma-separated asset lists as layers or cubemap faces, accumulates bytes into `ImageTools::ImageData`, and inspects/caches transparency and translucency.

- STB decodes standard formats; DevIL handles DDS compatibility.
- NVTT can generate DDS cache files; conversion may run on the `HIGH_PRIORITY` task pool.
- NVTT DDS cache creation is compiled out on macOS, although shared DDS loading still exists.

Creation proceeds through decode/load, dimensions/layers/format/mip policy, `prepareTextureData(...)`, backend `loadDataInternal(...)`, then `submitTextureData(...)`.
GL reserves immutable storage and uploads mip/layer data with DSA. Vulkan allocates a VMA image, fills staging buffers, copies, and transitions layouts.

For texture changes, audit shared format/mip decisions, layer/face ordering, transparency caches, backend overloads, and upload lifetimes.
For arrays/cubemaps/mip generation/partial-update failures, start in backend `loadDataInternal(...)` overloads.
See [GPU resources](gpu-resources.md) for memory and colour/origin conventions, and [threading](threading.md) for background cache generation.
