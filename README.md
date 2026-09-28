[![Windows Builds](https://github.com/IonutCava/Divide-Framework/actions/workflows/Windows.yml/badge.svg)](https://github.com/IonutCava/Divide-Framework/actions/workflows/Windows.yml)
[![Linux Builds](https://github.com/IonutCava/Divide-Framework/actions/workflows/Linux.yml/badge.svg)](https://github.com/IonutCava/Divide-Framework/actions/workflows/Linux.yml)
[![MacOS Builds](https://github.com/IonutCava/Divide-Framework/actions/workflows/MacOS.yml/badge.svg)](https://github.com/IonutCava/Divide-Framework/actions/workflows/MacOS.yml)

# Divide-Framework [![MIT Licensed](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Website: [divide-studio.com](http://www.divide-studio.com)

ToDo List: [Trello.com/divide-todo](https://trello.com/b/mujYqtxR/divide-todo)

Twitter: [x.com/ionutcava](https://x.com/ionutcava)

## The How and the Why
Yup, YAGE. Yet Another Game Engine. A toy engine, mind you, never intended for general release or something people will ever require support with.
It is something I use to experiment on, learn new things, practice, prototype and eventually, try and ship a game or two with.

This code started during my first days in university. The first iteration looked like this: [Youtube link](https://www.youtube.com/watch?v=VWNjdmhz-lM).

Next to no programming experience and it shows in the parts of code that survived since then (all the SceneNode and Resource stuff). Started with a lot of OOP and "Clean Code" with C++98 and currently developed using as much DoD as possible (basic structs and enums, inheritance mostly for interfaces or restrictions {NonCopyable, NonMovable, etc}) but with C++23 for ease of use (constexpr and concepts over templates, threading and filesystem builtin, lambdas over function pointers etc).

It got me through learning old-school OpenGL (1.x, 2.x), core GL (3.x, 4.x) and some AZDO techniques (indirect rendering, bindless textures, persistently mapped buffers, etc).
Currently using it to learn Vulkan 1.3 (why would I learn about framebuffers, subpasses and renderpasses now? Maybe for mobile, but I don't see myself doing that now, and if I did, I'd learn that as required).

The reason it's published on GitHub are:
* so I can store the code somewhere central and keep track of changes.
* if people do as I do and search for various things (functions, enums, etc) on GitHub, maybe my code can help.
* if anyone is struggling with solving a problem that I already banged my head against, feel free to get inspired by it.

If you plan to use any parts of this code in a commercial product, a couple of things:
* Are you sure?
* Please let me know as I'm curious to see where and why and also, I'd highly appreciate it.
  
## How to build & run
### All:
- Clone with recursive submodules (e.g. git clone --recurse-submodules). Needed for vcpkg.
### Visual Studio Code:
- Open the root folder in VSCode (with CMake and C++ extensions installed).
- Under CMake Project Status view, select the desired Configure preset followed by the desired Build preset.
- Build and Run as needed.
- Works on both Windows and Linux.
### Windows:
- Engine / Game builds:
   - Open the root folder in Visual Studio (with CMake tools installed).
   - Select the desired preset from the Build Preset dropdown.
   - Build and Run as needed.
- Project Manager:
   - Same as above (VS/VSCode) but with the ProjectManager subfolder as a root.
   - Alternatively. RUN.bat will attempt to build a release version and launch that.
      - The Project Manager has an option to launch Visual Studio with the proper path set.
### Linux: 
- Only tested the VSCode steps outlined above.
### MacOS: 
- ToDo

## Technical overview for contributors

### Geometry loading and GPU upload
- Mesh assets are loaded through `MeshImporter`, which first tries the engine cache (`.DVDGeom` / `.DVDAnim`) and falls back to Assimp-based conversion when the cache is missing or disabled.
- The Assimp conversion path lives in `Source/Geometry/Importer/DVDConverter.cpp` and applies a fairly aggressive post-process stack: tangent generation, identical-vertex merging, cache-locality improvements, normal generation, triangulation, invalid-data cleanup, mesh optimization, and bounding-box generation.
- Imported meshes are normalized into `Import::SubMeshData` records containing packed vertex attributes, material metadata, triangle/index lists, and optional skinning data.
- The importer then runs meshoptimizer on the data:
  - LoD 0 is remapped and optimized for vertex cache, overdraw, and vertex fetch.
  - Additional LoDs are generated with `meshopt_simplify` when the source mesh is large enough.
- All submeshes for a model share one `VertexBuffer`. `DVDConverter` precomputes the total vertex/index count, allocates a single engine-side vertex/index buffer pair, appends each submesh's indices as partitions, and writes vertex attributes into the shared buffer.
- `VertexBuffer::commitData()` trims the CPU vertex layout down to only the attributes actually used by the mesh before upload. On first upload or layout changes it recreates the GPU buffers with the final compact stride; later dynamic updates only rewrite the affected ranges.
- `Mesh` owns the shared geometry buffer, while `SubMesh` resources only reference partitions inside that buffer plus per-submesh material and bounding information. This keeps imported models from duplicating GPU geometry storage.

### Texture loading and GPU upload
- Textures are loaded through `Texture::loadInternal()`, which splits comma-separated asset lists into array layers or cubemap faces and accumulates them into `ImageTools::ImageData`.
- `ImageTools` uses STB for regular image decoding, DevIL for DDS handling, and NVTT to build DDS cache files for eligible textures. The DDS conversion step can run asynchronously on the high-priority task pool and is stored under the texture metadata/cache path for future loads.
- Import options on each texture control whether DDS caching is used, whether a texture should be treated as a normal map, and whether alpha should be analyzed for transparency/translucency.
- After CPU-side decode, `Texture::createWithData()` calls the backend-specific texture implementation:
  - The generic layer determines dimensions, layer count, mip policy, and base format.
  - The backend reserves storage, uploads each mip/layer payload, and finalizes the resource into a shader-readable state.
- The engine also caches alpha-analysis metadata separately so future loads can skip expensive transparency scans.

### OpenGL backend memory model
- OpenGL buffer memory is managed by a custom allocator in `glMemoryManager`. It creates persistently mapped buffer chunks, suballocates `Block`s from those chunks, and merges free ranges on deallocation.
- `glBufferImpl` requests aligned suballocations from the allocator based on buffer type (vertex/index/uniform/storage), so many logical engine buffers can share larger GL allocations instead of creating one GL buffer object per upload.
- Frequently updated GL buffers stay persistently mapped and are protected with `glLockManager` sync objects to avoid CPU writes racing GPU reads.
- One-shot buffers still use the same allocator path but are treated as static data after upload.
- OpenGL textures are simpler: they rely on driver-owned texture objects created with immutable storage (`glTextureStorage*` / multisample variants). The engine does not pool texture memory on the GL path; it manages texture object lifetime and data upload, while the driver manages the actual residency.

### Vulkan backend memory model
- Vulkan uses AMD's Vulkan Memory Allocator (VMA). A global allocator is created during backend initialization and is then used for both buffers and images.
- `vkBufferImpl` prefers device-local memory for GPU buffers. If a buffer is frequently updated or explicitly host-visible, it requests mapped host access from VMA; otherwise it allocates device memory and uses staging buffers plus transfer commands for uploads.
- Non-mappable Vulkan buffers keep an internal staging buffer and enqueue copy requests into the transfer/graphics submission path. Mappable buffers write directly into the VMA-mapped allocation.
- `vkTexture` allocates images through VMA as dedicated device-preferred allocations, uses staging buffers for texture payload uploads, and performs explicit image layout transitions before and after copies.
- Vulkan texture uploads also generate mipmaps on the GPU when needed, and read/write capable textures opt into the image usage flags required by the requested engine-side `ImageUsage` bits.
- In short:
  - OpenGL uses a custom persistent-mapped suballocator for buffers and raw driver texture objects for images.
  - Vulkan uses VMA-backed allocations, explicit staging copies, and explicit image/buffer synchronization.

## Features:

* OpenGL 4.6 (AZDO) renderer

* Experimental Vulkan renderer

* C++17/20

* Windows only (but with functional Linux platform code).

## Screenshots
Framework Screenshot
![Framework Screenshot](http://divide-studio.co.uk/Editor.png)

Scene Manipulation Screenshot
![Scene Manipulation Screenshot](http://divide-studio.co.uk/Editor2.png)

Vulkan Rendering Backend
![Vulkan Rendering Backend](http://divide-studio.co.uk/VulkanRenderer.png)

Day night cycle
![Day night cycle](http://divide-studio.co.uk/fun2.png)

Editor Grid
![Editor_Grid](http://divide-studio.co.uk/EditorGrid.png)

Sponza rendering
![Sponza rendering](http://divide-studio.co.uk/Rendering.png)

ImGUI Docking
![ImGUI Docking](http://divide-studio.co.uk/Windows.png)

SSR
![SSR](http://divide-studio.co.uk/SSR.png)

Grass/Sky/Fog
![Grass/Sky/Fog](http://divide-studio.co.uk/sky_fog_2.png)

Physically Based Bloom:
![Bloom](http://divide-studio.co.uk/Bloom.png)
# Third Party libs:
```
If I accidentally breached any license, please open an issue and I will address it immediately.
I did try to comply with all of them, but I may have missed something.
```

* EASTL: https://github.com/electronicarts/EASTL
* SDL: https://github.com/libsdl-org
* {fmt}: https://github.com/fmtlib/fmt
* ECS: https://github.com/tobias-stein/EntityComponentSystem
* JoltPhysics: https://github.com/jrouwe/JoltPhysics
* ChaiScript: https://chaiscript.com
* Optick: https://github.com/bombomby/optick
* ReCast: https://github.com/recastnavigation/recastnavigation
* UI
    * CEGUI: http://cegui.org.uk
    * Dear ImGui: https://github.com/ocornut/imgui
    * ImGuizmo: https://github.com/CedricGuillemet/ImGuizmo
    * imgui_club: https://github.com/ocornut/imgui_club
    * ImGuiAl: https://github.com/leiradel/ImGuiAl
    * imguifilesystem: https://github.com/Flix01/imgui/tree/imgui_with_addons/addons/imguifilesystem
    * imguistyleserializer: https://github.com/Flix01/imgui/tree/imgui_with_addons/addons/imguistyleserializer
    * Node Editor in ImGui: https://github.com/thedmd/imgui-node-editor
    * fontstash: https://github.com/memononen/fontstash
    * IconFontCppHeaders: https://github.com/juliettef/IconFontCppHeaders
* Asset Management:
    * Open-Asset-Importer-Library: https://github.com/assimp/assimp
    * STB: https://github.com/nothings/stb
    * meshoptimizer: https://github.com/zeux/meshoptimizer
    * nvtt: https://github.com/castano/nvidia-texture-tool
    * Frexx CPP (C Preprocessor): https://github.com/bagder/fcpp
    * DevIL: https://openil.sourceforge.net
* OpenAL-soft: https://github.com/kcat/openal-soft
* OpenGL:
    * glbinding: https://github.com/cginternals/glbinding
    * OpenGL Immediate Mode for OpenGL 3.0: https://community.khronos.org/t/glim-opengl-immediate-mode-for-opengl-3-0/56957
* Vulkan:
    * glslang: https://github.com/KhronosGroup/glslang
    * SPIRV-Reflect: https://github.com/KhronosGroup/SPIRV-Reflect
    * VMA: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    * vk-bootstrap: https://github.com/charles-lunarg/vk-bootstrap
    * Vulkan-Descriptor-Allocator: https://github.com/vblanco20-1/Vulkan-Descriptor-Allocator
* NRI: https://github.com/NVIDIA-RTX/NRI
* Memory Management:
    * Fixed Block Allocator: https://www.codeproject.com/Articles/1083210/An-efficient-Cplusplus-fixed-block-memory-allocato
    * Arena Allocator: https://www.codeproject.com/Articles/44850/Arena-Allocator-DTOR-and-Embedded-Preallocated-Buf
    * Memory Pool: https://github.com/cacay/MemoryPool
* moodycamel::ConcurrentQueue: https://github.com/cameron314/concurrentqueue
* Boost (for ASIO and faster regex + 3rd party libs) : https://www.boost.org
* cppGOAP: https://github.com/cpowell/cppGOAP
* CurlNoise: https://github.com/rajabala/CurlNoise
* Tileable Volume Noise: https://github.com/sebh/TileableVolumeNoise/
* simplefilewatcher: https://code.google.com/archive/p/simplefilewatcher/
* simpleini: https://github.com/brofield/simpleini
* GLM: https://github.com/g-truc/glm
* skarupke hash maps: https://github.com/skarupke
* Catch2: https://github.com/catchorg/Catch2
* various 3rd party parser libs:
    * freetype: https://freetype.org
    * freeimage: https://freeimage.sourceforge.io
    * libjpeg: https://libjpeg.sourceforge.net
    * libpng:http://www.libpng.org
    * libtiff: http://www.libtiff.org
    * zlib: https://zlib.net
