# Third-party dependencies

<!-- Copyright (c) 2026 DIVIDE-Studio -->

`vcpkg.json` manages many dependencies that receive ongoing fixes and updates.
Some larger dependencies are fetched through CMake. Small, infrequently changing
libraries may instead be vendored in `Source/ThirdParty` when their licenses
permit it.

On macOS, solar calculations use the system's local-time API instead of the
`date` library. Unlike the previous system-zone lookup, this also honors a
process `TZ` environment-variable override.

## Vendored library provenance

The following libraries are copied from the listed upstream revisions instead
of being fetched at configure time. Paths are relative to each library's
folder in `Source/ThirdParty`, except where the folder is `.`. Build wiring is
in `Source/ThirdParty/CMakeHelpers/ImportPackages.cmake`.

When updating a library, copy the required upstream files, preserve their
license notices, reapply any compatibility patches, and update the revision
below.

| Library / folder | Upstream | Commit | License | Files |
|---|---|---|---|---|
| `flat_hash_map` | https://github.com/skarupke/flat_hash_map | `2c4687431f978f02a3780e24b8b701d22aa32d9c` | BSL-1.0 (`LICENSE_1_0.txt`) | `bytell_hash_map.hpp`, `flat_hash_map.hpp` |
| `MemoryPool` | https://github.com/cacay/MemoryPool | `1ab4683e38f24940afb397b06a2d86814d898e63` | MIT (file headers) | `StackAlloc.h`, `C-11/MemoryPool.h`, `C-11/MemoryPool.tcc` |
| `TileableVolumeNoise` | https://github.com/IonutCava/TileableVolumeNoise | `9572fe4ea32dd5ebf7d311cb6a5376a5215613c0` | MIT (`LICENSE.md`) | `TileableVolumeNoise.h/.cpp` |
| `CurlNoise` | https://github.com/IonutCava/CurlNoise (fork of rajabala/CurlNoise) | `b8baf020c8d2b449e56ccc5b6a3c7ee6890f9453` | zlib (`LICENSE`); `vmath.hpp` BSD-3-Clause (header) | `CurlNoise/Curl.h/.cpp`, `CurlNoise/Noise.h/.cpp`, `CurlNoise/vmath.hpp` |
| `SimpleFileWatcher` | https://github.com/jameswynn/simplefilewatcher | `4bccd086621698f21a6e95f3ff77c559f862184a` | MIT (`License.txt`) | `include/FileWatcher/*.h`, `source/*.cpp` |
| `fcpp` | https://github.com/IonutCava/fcpp | `f25c3bfc38fa5bef669a0845edeba0882fc81ecb` | BSD-3-Clause (`COPYING`), MIT notices in sources | `cpp1-6.c`, headers, `README` |
| `imgui_club` | https://github.com/ocornut/imgui_club | `da32fb90bfb7e5b2a687b972105cc3b6d3db0321` | MIT (`LICENSE.txt`) | `imgui_memory_editor/imgui_memory_editor.h` |
| `IconFontCppHeaders` | https://github.com/juliettef/IconFontCppHeaders | `8886c5657bac22b8fee34354871e3ade2a596433` | zlib (`licence.txt`) | `IconsForkAwesome.h` |
| SimpleIni (`.`) | https://github.com/brofield/simpleini | `877f7357d1fa4232f1f3352e5028f99899210b27` (`v4.26`) | MIT (`LICENCE.txt`) | `SimpleIni.h` |
| `concurrentqueue` | https://github.com/cameron314/concurrentqueue | `9afb99746f0f5fc94ac8aef737053ae0481ba8d1` (`v1.0.5`) | BSD-2-Clause or BSL-1.0 (`LICENSE.md`) | `moodycamel/{concurrentqueue,blockingconcurrentqueue,lightweightsemaphore}.h` and internal debug header |
| CTRE (`.`) | https://github.com/hanickadot/compile-time-regular-expressions | `4ab18abfd88ccfc867734bf5d31b13285854577e` (`v3.11.0`) | Apache-2.0 (`CTRE-LICENSE`) | Complete upstream `include/` tree |
| `ImGuizmo` | https://github.com/CedricGuillemet/ImGuizmo | `b796ac3b861afc6e91ca74e4611effd9c9527367` (`1.10`) | MIT (`LICENSE`) | `ImGuizmo.cpp`, `ImGuizmo.h` |
| `sse2neon` | https://github.com/DLTcollab/sse2neon | `92f6de174717aef09033ad21568d5bb9e5470404` (`v1.9.1`) | MIT (`LICENSE`) | `sse2neon.h` |
| `ChaiScript` | https://github.com/IonutCava/ChaiScript | `c150c4ad4171a417b8bac1d8d6616ab656e96858` | BSD-3-Clause (`LICENSE`); MIT (`include/chaiscript/dispatchkit/short_alloc.hpp` notice) | Complete upstream `include/chaiscript/` tree |
| `imgui-node-editor` | https://github.com/thedmd/imgui-node-editor | `c24a8a80484568ce85bd92cfcb5c24646b951ac4` (`v0.9.3`) | MIT (`LICENSE`) | Four implementation files and their headers |

The imgui-node-editor sources retain the `fix-vec2-math-operators`,
`remove-getkeyindex`, and `fix-imgui-v1.92.5` patches from the pinned vcpkg port.
They also include `<exception>` for `std::terminate` and adapt draw calls to
ImGui 1.92.8's parameter order and closed-path flag.
