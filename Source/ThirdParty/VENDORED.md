# Vendored third-party libraries

These small, rarely changing libraries are copied verbatim (only the files the engine uses)
from the listed upstream commit instead of being fetched at configure time.
They are wired up in `CMakeHelpers/ImportPackages.cmake`.

To update one, re-copy the listed files from the new upstream commit and update the commit here.

| Folder | Upstream | Commit | License | Files |
|---|---|---|---|---|
| `flat_hash_map` | https://github.com/skarupke/flat_hash_map | `2c4687431f978f02a3780e24b8b701d22aa32d9c` | BSL-1.0 (`LICENSE_1_0.txt`) | `bytell_hash_map.hpp`, `flat_hash_map.hpp` |
| `MemoryPool` | https://github.com/cacay/MemoryPool | `1ab4683e38f24940afb397b06a2d86814d898e63` | MIT (file headers) | `StackAlloc.h`, `C-11/MemoryPool.h`, `C-11/MemoryPool.tcc` |
| `TileableVolumeNoise` | https://github.com/IonutCava/TileableVolumeNoise | `9572fe4ea32dd5ebf7d311cb6a5376a5215613c0` | MIT (`LICENSE.md`) | `TileableVolumeNoise.h/.cpp` |
| `CurlNoise` | https://github.com/IonutCava/CurlNoise (fork of rajabala/CurlNoise) | `b8baf020c8d2b449e56ccc5b6a3c7ee6890f9453` | zlib (`LICENSE`); `vmath.hpp` BSD-3-Clause (header) | `CurlNoise/Curl.h/.cpp`, `CurlNoise/Noise.h/.cpp`, `CurlNoise/vmath.hpp` |
| `SimpleFileWatcher` | https://github.com/jameswynn/simplefilewatcher | `4bccd086621698f21a6e95f3ff77c559f862184a` | MIT (`License.txt`) | `include/FileWatcher/*.h`, `source/*.cpp` |
| `fcpp` | https://github.com/IonutCava/fcpp | `f25c3bfc38fa5bef669a0845edeba0882fc81ecb` | BSD-3-Clause (`COPYING`), MIT notices in sources | `cpp1-6.c`, headers, `README` |
| `imgui_club` | https://github.com/ocornut/imgui_club | `da32fb90bfb7e5b2a687b972105cc3b6d3db0321` | MIT (`LICENSE.txt`) | `imgui_memory_editor/imgui_memory_editor.h` |
| `IconFontCppHeaders` | https://github.com/juliettef/IconFontCppHeaders | `8886c5657bac22b8fee34354871e3ade2a596433` | zlib (`licence.txt`) | `IconsForkAwesome.h` |
