# Divide-Framework

**Work In Progress**

Website: [divide-studio.com](http://www.divide-studio.com)

ToDo List: [Trello.com](https://trello.com/b/mujYqtxR/divide-todo)

Yup, YAGE. Yet Another Game Engine. A toy engine, mind you, never intended for general release or something people will ever require support with.
Something I use to experiment on, learn new things, practice, prototype and eventually, try and ship a game or two with.

This code started during my first days in uni. The very first iteration looked like this: [Youtube link](https://www.youtube.com/watch?v=VWNjdmhz-lM).

Next to no programming experience and it shows in the parts of code that survived since then (all the SceneNode and Resource stuff).

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

# Third Party libs:
```
If I accidentely breached any license, please open an issue and I will address it immediately.
I did try to comply with all of them, but I may have missed something    .
```

* EASTL: https://github.com/electronicarts/EASTL
* SDL: https://github.com/libsdl-org
* {fmt}: https://github.com/fmtlib/fmt
* ECS: https://github.com/tobias-stein/EntityComponentSystem
* PhysX: https://github.com/NVIDIAGameWorks/PhysX
* ChaiScript: https://chaiscript.com
* Optick: https://github.com/bombomby/optick
* ReCast: https://github.com/recastnavigation/recastnavigation
* UI
    * Dear ImGui: https://github.com/ocornut/imgui
    * ImGuizmo: https://github.com/CedricGuillemet/ImGuizmo
    * imgui_club: https://github.com/ocornut/imgui_club
    * ImGuiAl: https://github.com/leiradel/ImGuiAl
    * imguifilesystem: https://github.com/Flix01/imgui/tree/imgui_with_addons/addons/imguifilesystem
    * imguistyleserializer: https://github.com/Flix01/imgui/tree/imgui_with_addons/addons/imguistyleserializer
    * Node Editor in ImGui: https://github.com/thedmd/imgui-node-editor
    * CEGUI: http://cegui.org.uk
    * fontstash: https://github.com/memononen/fontstash
    * IconFontCppHeaders: https://github.com/juliettef/IconFontCppHeaders
* Asset Management:
    * Open-Asset-Importer-Library: https://github.com/assimp/assimp
    * STB: https://github.com/nothings/stb
    * meshoptimizer: https://github.com/zeux/meshoptimizer
    * nvtt: https://github.com/castano/nvidia-texture-tool
    * Frexx CPP (C Preprocessor): https://github.com/bagder/fcpp
    * DevIL: https://openil.sourceforge.net
* OpenAL: https://www.openal.org
* OpenGL:
    * glbinding: https://github.com/cginternals/glbinding
    * OpenGL Immediate Mode for OpenGL 3.0: https://community.khronos.org/t/glim-opengl-immediate-mode-for-opengl-3-0/56957
* Vulkan:
    * glslang: https://github.com/KhronosGroup/glslang
    * SPIRV-Reflect: https://github.com/KhronosGroup/SPIRV-Reflect
    * VMA: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    * vk-bootstrap: https://github.com/charles-lunarg/vk-bootstrap
    * Vulkan-Descriptor-Allocator: https://github.com/vblanco20-1/Vulkan-Descriptor-Allocator
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
* glm (mostly for 3rd party libs): https://github.com/g-truc/glm
* skarupke hash maps: https://github.com/skarupke
* C++ Unit Test Framework: https://github.com/cppocl/unit_test_framework
* various 3rd party parser libs:
    * freetype: https://freetype.org
    * freeimage: https://freeimage.sourceforge.io
    * libjpeg: https://libjpeg.sourceforge.net
    * libpng:http://www.libpng.org
    * libtiff: http://www.libtiff.org
    * zlib: https://zlib.net
