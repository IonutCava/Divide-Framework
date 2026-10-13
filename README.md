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
### Minimum CPU requirements:
- x86: SSE4.2
- ARM: NEON

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

## Features:

* OpenGL 4.6 (AZDO) renderer

* Experimental Vulkan renderer

* C++17/20

* Windows only (but with functional Linux platform code).

## KTX2 migration (initial implementation)

- KTX-Software is pinned to **4.4.2**. Screenshots are lossless, uncompressed
  sRGB KTX2 files with bottom-left orientation, not PNG files.
- Native rendering accepts 1D/2D/3D, array and cubemap KTX2 files: R/RG/RGBA
  UNORM8, UNORM16, half-float and float, with their supplied mip counts.
  Explicit bottom-left orientation and identity swizzle are required; unsupported
  encodings fail rather than falling back to an ordinary-image backend path.
- `DivideAlpha` metadata records the classifier version, alpha interpretation,
  thresholds and classification. Transparent (0–1) and translucent (2–250)
  categories each tolerate four pixels independently. All-zero opacity logs an
  error and is transparent only. Missing classification is unknown until decoded
  base-level pixels are classified; synthetic and data alpha are excluded.
- Single-file PNG/JPEG and uncompressed 24-bit BMP inputs automatically use precision-preserving source
  decoding, semantic mip generation and a lossless KTX2 master representation.
  Colour mips filter sRGB in linear space; normal mips renormalize; data channels
  filter independently. Alpha coverage uses the engine discard threshold, not
  the opacity classifier. Supplied mip chains remain intact unless regeneration
  is requested. Cube faces filter independently, without seam-aware filtering.
- The source cache stores exact source bytes and settings alongside its KTX2
  payload in one atomically published entry. Warm hits still read source bytes,
  but avoid image decoding and encoding. Identical concurrent requests coalesce;
  conversion concurrency is limited to two. Input/base-plus-mips are limited to
  32 MiB each; encoded payloads to 64 MiB. Disk failure falls back to the unified
  in-memory representation, never ordinary-image backend loading.
- GPU uploads select a format supported by the active OpenGL/Vulkan device.
  Eligible sampled 8-bit colour and XYZ normal textures use BC7 through
  KTX-Software's public UASTC encoder/transcoder. BC4 (R) and BC5 (RG) require
  explicit lossy-data permission; packed data remains lossless by default.
  Original alpha classification, sRGB interpretation and supplied mip levels
  are retained independently of compression.
  BC7 uses 16 bytes per 4x4 block (versus 64 bytes for RGBA8); BC4/BC5 use
  8/16 bytes per block. Tiny mip tails still occupy a complete block.
  Block-aligned tile encoding bounds scratch memory for large textures.
  GPU-target entries cache the native blocks separately from lossless masters,
  keyed by exact source pixels, topology, target format and encoder policy.
  Warm disk hits avoid encoding/transcoding; corrupt or unavailable entries
  are rebuilt in memory. Cache publication is atomic and quota-limited.
- CPU-readable, precision-sensitive 16-bit/float, writable, render-target and
  region-update textures retain their native representation. Unsupported
  compression targets fall back to that representation. There is no universal
  fastest format: BC7 prioritizes channel/alpha fidelity and reduced bandwidth,
  not maximum compression ratio or a guaranteed frame-rate improvement.
  HDR/16-bit inputs are not quantized to feed the 8-bit Basis encoder.
  External Basis/supercompressed KTX2 inputs still require an explicit import
  step; this GPU-compression path consumes the existing lossless native masters.
- DDS is an import format, not a cache format. Source and GPU-target caching
  use KTX2 representations; the engine no longer generates DDS cache files.
  Existing DDS assets remain readable through the temporary DevIL importer.
  Supported single 2D DDS images enter the typed KTX pipeline after decoding;
  other DDS formats/topologies retain their legacy import path until the
  replacement codec is integrated.
  `ImageData::encodeKTX` exports supported native single-2D images as lossless
  KTX2 bytes, retaining precision, mip levels and alpha metadata. Legacy direct
  DDS imports and GPU-compressed upload copies cannot be exported through it.
  Typed DDS import and generated mip chains are limited to 32 MiB each.
- **Still pending:** ordered multi-source array/cube importing,
  remaining codec/consumer migration and removal of legacy image libraries.

Import-codec selection: libpng **1.6.59** for 8/16-bit PNG
(reuses the existing dependency; the pinned registry's LodePNG port is obsolete),
libjpeg-turbo 3.2.0 for JPEG, existing SDL's native BMP decoder, and DirectXTex
(`1acf4ebe6330e06b7eb52e8db63d4f824ad77427`) for TGA/DDS/Radiance HDR.
DirectXTex is the non-header-only exception needed for maintained, complete DDS
precision/BC support; it belongs to import tooling, not backend texture loading.
libpng, libjpeg-turbo and native SDL BMP decoding are integrated for single-source
imports. BMP alpha/bitfield/other pixel encodings require explicit conversion;
TGA/HDR and unsupported DDS layouts remain on legacy paths until their codecs are integrated. Comma-separated source
lists likewise remain on the legacy path; native KTX2 files cannot be mixed with
such lists. Legacy lists must have matching formats, dimensions and mip layouts;
incompatible mixed DDS/source-image lists require offline normalization.
KTX cache controls replace DDS cache controls; old configuration
keys remain readable for compatibility. Lossless master encoding uses the
area-box mip filter. Obsolete DDS output-format, filter and asynchronous
conversion options have been removed; KTX GPU compression uses the policy above.
`_compressKTXForGPU=false` disables lossy GPU compression;
`_preserveKTXCPUData=true` explicitly preserves exact GPU samples;
`_allowLossyKTXData=true` permits compression of otherwise protected data channels.
`_useKTXPipeline=false` is a temporary explicit migration opt-out.
KTX2 writing does not require an additional screenshot codec.

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
