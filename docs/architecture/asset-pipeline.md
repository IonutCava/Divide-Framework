# Asset pipeline

<!-- Copyright (c) 2026 DIVIDE-Studio -->

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
Assimp's global-scale processing applies source-unit conversion before cached
geometry is written; vegetation descriptor scales apply to those converted
vertices, not the original authoring units.

Animated submeshes share a skeleton and one inverse-bind matrix per bone. Repeated
bone bindings reuse the same index when their matrices agree within the import
tolerance; conflicting offsets or more than `MAX_BONE_COUNT_PER_NODE` weighted
bones fail import explicitly rather than producing incorrect skinning.

One model owns one shared `VertexBuffer`. Submeshes store partitions, triangle ranges, and material data rather than separate vertex allocations; `SubMesh::geometryBuffer()` resolves the parent's buffer.
The mesh scene node is a hierarchy root, not an additional geometry draw; its
submeshes issue the draws. When a mesh material template is propagated, each
submesh acquires its own resource reference and releases it on replacement or
unload. `FromHandle` transfers an existing resource reference to a scene node;
use `FromSharedHandle` when the caller retains ownership. Vegetation chunks,
tree meshes and submesh instances each own a separate reference, and repeated
transform children acquire one reference per node.

`Material::setTexture` consumes one reference for a valid texture handle.
Replacing a slot releases its previous reference, even if the new handle names
the same texture; callers sharing or reassigning a handle must acquire a
separate reference first.
Imported texture colour space is selected independently per slot: colour maps
may use sRGB, while normal, opacity, and other data maps remain linear.
Vegetation keeps the imported tree submesh materials intact and creates
per-vegetation material variants with the tree instancing shader and chunk
capacity. Material cloning preserves the source's base shader selection.

`VertexBuffer` owns a GPU vertex buffer and index buffer. `commitData()` compacts the CPU layout to present attributes.
Full layout changes call `setBuffer(...)`; incremental changes call `updateBuffer(...)`.
Indices may use 16-bit packing when vertex count permits. CPU copies can be discarded when dynamic updates are unnecessary.

Before changing geometry upload, check cache compatibility, attribute-presence tracking, compact layout, index width, LoD/partition correctness, and parent/submesh ownership.

## Texture flow

`Texture::loadInternal()` treats comma-separated asset lists as layers or cubemap faces, accumulates bytes into `ImageTools::ImageData`, and calls `CheckTextureTransparency` to inspect/cache transparency and translucency for each source.
`Texture::Texture()` requires a single asset directory for every entry in the
list. Serialized material paths must therefore name that one current directory,
not a comma-separated list of directories.

- Alpha inspection reduces independent per-partition summaries across all base-level
  layers, faces, and volume slices. Four non-opaque pixels are ignored; an all-zero
  alpha channel is ignored unless another layer has visible alpha. Alpha used for
  non-opacity data is not inspected. Per-source versioned metadata identifies the
  decoded file, timestamp, size, format, dimensions, and layer count; version-1
  classifications are not reused. Writers publish completed cache files by rename.
- `ImageData` retains base-level decompressed DDS pixels per image layer for CPU
  inspection. Component indexing uses the source element size and layer/slice;
  floating-point colours are clamped to the normalized range before conversion.
  Compressed mip inspection beyond the base level is not supported.
  CPU inspection pixels have the same row orientation as the uploaded image.
  DevIL's DXT-payload flip leaves its decoded buffer unchanged, so DDS loading
  separately flips decoded RGBA rows (per slice) for bottom-left origin.
  Placement maps must not sample the unflipped decoded buffer.
- STB decodes standard formats; DevIL handles DDS compatibility.
- NVTT can generate DDS cache files; conversion may run on the `HIGH_PRIORITY` task pool.
  `DDSCache::Write` serializes writers for each cache path, creates parent directories for
  nested source names, and publishes completed temporary files by rename. Existing entries
  are preserved; failed conversions or moves are reported and their temporary files removed.
  Once the first layer uses a DDS cache, later layers wait for and load their own DDS
  entries; a failed conversion cannot fall back to an incompatible source-image layer.
  A no-mipmap conversion writes a single-level DDS header.
- The NVTT overlay fixes its bundled STB BMP decoder's memory-input offset validation.
  BMPs with extended headers or padding must remain convertible without changing channel
  order, alpha, or orientation; offsets before the header or beyond the memory input
  are rejected.
- The NVTT overlay widens NVTT's debug `isValidPtr` bound from the legacy 8 TB Windows
  user space to 48-bit addresses. Debug and ASan heaps can allocate above 8 TB; the old
  bound hit `int 3` during mip generation (a hidden-dialog hang under Windows ASan CI).
- NVTT DDS cache creation is compiled out on macOS, although shared DDS loading still exists.
- `ImageMip<T>` stores typed elements but reports `LayerData::_size` in bytes;
  Vulkan staging and OpenGL upload spans both consume that byte count.

Creation proceeds through decode/load, dimensions/layers/format/mip policy, `prepareTextureData(...)`, backend `loadDataInternal(...)`, then `submitTextureData(...)`.
File-backed arrays validate layer counts and formats before upload. All image layers
must have matching, nonempty mip chains with compatible dimensions and payload sizes.
Read-only file-backed manual mip chains allocate only their supplied levels.
Compressed image data cannot use automatic GPU mip generation, so incomplete
automatically mipmapped compressed chains are restricted to their supplied
levels rather than sampling uninitialized storage; the loader warns when
automatic mipmapping was requested. Procedural, region-updatable, and
shader-writable manual textures retain storage for levels populated later.
Mipmapping disabled explicitly uploads only the base level, and the full mip
count includes volume depth. The authored
`birch-leaf-diff.dds` advertises its existing complete mip chain in its DDS header.
Cube face counts are converted to cube counts when allocating backend storage.
GL reserves immutable storage and uploads mip/layer data with DSA. Vulkan allocates a VMA image, fills staging buffers, copies, and transitions layouts.

For texture changes, audit shared format/mip decisions, layer/face ordering, transparency caches, backend overloads, and upload lifetimes.
For arrays/cubemaps/mip generation/partial-update failures, start in backend `loadDataInternal(...)` overloads.
See [GPU resources](gpu-resources.md) for memory and colour/origin conventions, and [threading](threading.md) for background cache generation.

## Terrain material data

`TerrainDescriptor` loads texture-layer and per-channel tile factors from the
terrain descriptor and alpha-map XML. `Terrain::load` generates a
`CURRENT_TILE_FACTORS` GLSL array with four entries per texture layer;
`BuildTerrainTileFactorArrayHeader` escapes its literal brace for the formatter.
The descriptor's `vegetation.vegetationTextureLocation` supplies the common
directory for the grass billboard texture array.
Each configured `treeMeshN` may specify a nonnegative relative `density`
weight; its default is 1 and zero disables that species. Eligible positions in
each chunk are assigned among enabled species according to those weights. If
there are enough positions, every enabled species occurs in the chunk.
Scale and rotation stay associated with their mesh even when XML slots are
missing. Tree rotations in the terrain XML are in degrees and are converted
to radians before building instance orientations. Tree cache names fingerprint
the ordered mesh names, weights, scales, and rotations, and tree cache payloads
identify the species of each candidate.
`Terrain::loadResources` initializes each terrain chunk's vegetation through
indexed parallel partitions; chunk tasks must not capture the loop variable by
reference. Chunk IDs are local to each terrain and index that terrain's
vegetation buffers. It returns to `Terrain::load` after initialization rather than
re-entering the terrain loader.
Vegetation generation rejects roots below the scene-configured global water
height. Caches are keyed by that height and reject incompatible or malformed
headers, counts and payload sizes before use. Map samples use coordinates
normalized to terrain bounds and the map's own dimensions. Tree scale no longer
shrinks toward the permitted slope limit.
Generated vegetation positions are currently rendered in world space without
applying the terrain node's transform; moving or transforming terrain does not
yet move its vegetation. Dynamic terrain and vegetation following its transform
remain future work, not an intended static-only restriction.
`Terrain::unload` destroys the chunks' vegetation instances before releasing
the shared vegetation resource, so every instance can unregister while its
parent handle is still valid.
Terrain draws its tile rings from its own GPU vertex and index buffers, so its
render preparation bypasses `Object3D`'s separate `VertexBuffer` path. Its
instance count is the sum of the tiles stored in those buffers.
Its emissive placeholder holds its own reference to the shared default 2D-array
texture, matching the material shader's sampler even when the operation is disabled.
The descriptor stores eight layers of factors, but its XML loader does not
currently reject a larger layer count.
