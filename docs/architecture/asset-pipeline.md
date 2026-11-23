# Asset pipeline

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Entry points

- `Source/Geometry/Importer/MeshImporter.cpp` loads and caches geometry and animation.
- `Source/Geometry/Importer/DVDConverter.cpp` converts imported scene data.
- `Source/Geometry/Shapes/Mesh.cpp` and `SubMesh.cpp` own runtime geometry and partitions.
- `Source/Platform/Video/Buffers/VertexBuffer/VertexBuffer.cpp` manages shared geometry buffers.
- `Source/Platform/Video/Textures/Texture.cpp` loads and creates textures.
- `Source/Utility/ImageTools.cpp` decodes images and manages texture caches.

## Geometry flow and ownership

`MeshImporter` loads a compatible cache when available. Otherwise, Assimp data
passes through `DVDConverter`, which normalizes the source scene, extracts
optional animation and skinning data, optimizes geometry, and builds LoD data
before writing the cache. Import settings that change geometry must also
participate in cache identity.

A mesh owns one shared `VertexBuffer`; submeshes refer to vertex/index
partitions and material data rather than allocating duplicate geometry.
Resource handles represent owned references. Transfer a reference when moving
ownership, and acquire a separate reference when another object must retain it.
Texture assignment consumes an owned reference and releases the previous one
when a slot changes.

`VertexBuffer::commitData()` compacts the CPU layout to the attributes in use.
Full layout changes replace the GPU buffers; incremental changes update them.
Keep attribute-presence metadata, material layouts, indices, and LoD partitions
consistent before publishing a new layout. CPU copies may be discarded when
runtime updates do not need them.

Before changing geometry import or upload, check cache compatibility,
transforms, attribute layouts, index widths, partitions, and ownership.

## Texture flow

`Texture::loadInternal()` decodes assets into `ImageData`, then prepares and
uploads the requested layers, faces, formats, and mip levels. A comma-separated
asset list uses one directory. Keep serialized material paths within that
directory.

Texture colour space is selected per material slot. Colour maps may use sRGB;
normal and other data maps use linear values. Keep decoded CPU pixels in the
same row orientation as the uploaded image so tools that inspect texture data
sample the intended texels.

STB handles standard image formats and DevIL handles DDS. NVTT can generate DDS
caches. Cache readers must reject incompatible data, and writers publish a
completed file only after conversion succeeds. Keep cache keys tied to their
inputs and encoding so stale data is not reused after a format change.

All layers in a file-backed texture must have compatible formats, dimensions,
and mip chains. Read-only manual chains allocate only supplied levels.
Compressed images cannot use automatic GPU mip generation, so do not expose
uninitialized mip levels. Upload sizes are measured in bytes.

Texture creation proceeds through decode/load, format and mip selection,
`prepareTextureData()`, backend upload, and final submission. OpenGL uses
immutable storage and direct uploads. Vulkan uses image allocations, staging
buffers, copies, and explicit layout transitions. See [GPU resources](gpu-resources.md)
for backend memory and lifetime rules, and [Threading](threading.md) for
background cache work.

## Terrain material data

`TerrainDescriptor` supplies terrain texture layers, tile factors, and
vegetation inputs. `Terrain::load()` turns this data into the material and
shader inputs used by terrain chunks. Keep descriptor parsing, generated
shader data, and cache identity in sync when changing these inputs.

Terrain chunks initialize vegetation through indexed parallel work. Chunk tasks
must own stable input data and write only their assigned chunk. The terrain
resource owns the vegetation instances and releases them before releasing
shared resources they reference.

Terrain and vegetation currently use world-space instance data. Do not assume a
terrain transform also transforms its generated vegetation. Check the owning
subsystem before changing that contract.
