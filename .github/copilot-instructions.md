## Copilot / AI agent instructions for Divide-Framework

Keep this short and actionable. Focus on immediate, verifiable information an AI assistant needs to be productive in this repository.

1) Big picture
   - This is a small game engine / editor. Primary code lives under `Source/` and assets under `Assets/`.
   - The main static library target is built from `engineMain.cpp` + `ENGINE_SOURCE_CODE` and exposed as `Divide-Framework-Lib` (see `Source/CMakeLists.txt`).
   - Two primary executables: the engine/editor (`Executable/main.cpp`) and the `ProjectManager` (`ProjectManager/ProjectManager.cpp`).
   - Runtime configuration is loaded from `config.xml` at startup (see `Source/engineMain.cpp` where `Application::start("config.xml", ...)` is called).

2) How to build / common workflows
   - Repo expects recursive submodules (vcpkg, third-party). Clone with `--recurse-submodules`.
   - The project uses CMake presets: see `CMakePresets.json`. In VSCode use the CMake extension and pick a Configure + Build preset (examples: `macos-clang-editor`, `windows-msvc-editor`).
   - CLI pattern (typical):
     - Configure: `cmake --preset <configurePreset>`
     - Build:     `cmake --build --preset <buildPreset>`
     - Many presets combine flags like `ENABLE_OPTICK_PROFILER`, `ENABLE_FUNCTION_PROFILING`, `ENABLE_MIMALLOC`, or `editor-build`.
   - vcpkg is used for dependencies; the project expects vcpkg integration via the presets/toolchain.

3) Important files / entry points to reference in patches
   - Architecture & build wiring: `CMakeLists.txt`, `Source/CMakeLists.txt`, `CMakeHelpers/GlobSources.cmake`.
   - Engine entry / lifecycle: `Source/engineMain.cpp` — Init / Run / RunInternal and how `Application` is started/stopped.
   - Executables: `Executable/main.cpp`, `ProjectManager/ProjectManager.cpp`.
   - Precompiled headers: `EngineIncludes_pch.h`, `CEGUIIncludes_pch.h` (referenced from `Source/CMakeLists.txt`).
   - Assets & config: `Assets/` and `config.xml` in the repo root are used at runtime.

4) Project-specific conventions & patterns
   - C++20 is used (some code reflects older style). Expect a mixture of DoD-friendly structs and OOP interfaces (NonCopyable, NonMovable, etc.).
   - Target names are defined as variables in `Source/CMakeLists.txt` (e.g. `APP_LIB_DIVIDE`, `APP_EXE_DIVIDE`, `APP_EXE_PROJECT_MANAGER`) — prefer editing these variables rather than hardcoding target names elsewhere.
   - Compile flags and third-party flag lists are centralized in `Source/CMakeLists.txt` (search for `DIVIDE_COMPILE_OPTIONS` / `THIRD_PARTY_COMPILE_OPTIONS`). Avoid changing them lightly; tests and CI rely on many of these warnings being suppressed in third-party code.
   - Tests are guarded by `BUILD_TESTING_INTERNAL` and `RUN_TESTING_INTERNAL` CMake variables and use Catch2.

5) Integration points and external dependencies
   - Many third-party libraries managed through vcpkg and submodules (see README third-party list and `vcpkg.json`).
   - PhysX / GPU acceleration targets may be copied post-build (see `add_custom_command` in `Source/CMakeLists.txt`). When changing native library handling, check Windows/Unix copy/post-build logic.

6) Quick tips for code changes
   - Small, local changes: run the matching CMake preset and build only the target (`cmake --build --preset <buildPreset> --target <target>`).
   - If you edit PCH headers (`EngineIncludes_pch.h`, `CEGUIIncludes_pch.h`) expect a full rebuild.
   - When touching platform code, inspect `Source/Platform/*` and `CMakePresets.json` for platform-specific cache variables (e.g. `WINDOWS_OS_BUILD`, `MAC_OS_BUILD`).

7) What to look for during PR assistance
   - Keep builds green with existing presets; CI has platform-specific workflows (Windows/Linux/Mac).
   - Prefer changes that keep CMake variables and presets intact; if you add a new feature flag, add it to `CMakePresets.json` so developers can enable/disable consistently.
   - Unit tests live under `Source/UnitTests` — enable `BUILD_TESTING_INTERNAL` to compile them.

8) Useful search tokens for navigating the codebase
   - `Application`, `engineMain`, `ProjectManager`, `Executable/main.cpp`, `EngineIncludes_pch.h`, `CMakeHelpers`, `config.xml`, `Assets/`, `vcpkg`.

If any section is unclear or you want me to include more specific examples (exact build-presets or CI badge links), tell me which platform or workflow to expand and I will update this file.
