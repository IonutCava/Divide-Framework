# Agent Instructions for Divide-Framework

## General guidelines

- Keep agent notes short and actionable. Focus on immediate, verifiable information needed to work in this repository.
- Read only the subsystem guides relevant to the task; start with the [architecture index](docs/architecture/index.md).
- Treat source code and build configuration as authoritative when notes disagree.

## Documentation boundaries

- Keep `README.md` human-facing: purpose, features, screenshots, dependencies, and human build/run instructions.
- Put AI-specific notes and agent/CI validation guidance here or in linked agent references, not in `README.md`.
- Keep subsystem knowledge in `docs/architecture/` and reusable agent procedures in `.github/skills/`. Link rather than duplicate.
- Every PR/change must keep the relevant documentation up to date alongside the implementation. Review the affected guides even when no documentation edit is needed.
- If no relevant guide or entry exists, add one following the existing pattern: subsystem scope, source paths/symbols, ownership/data flow, invariants, safe-change guidance, and known limitations. Link new guides from `docs/architecture/index.md` and this file's routing table.
- Use source paths and symbols rather than brittle line numbers.
- Record durable architectural decisions and rationale in `docs/decisions/` when needed; do not create empty placeholders.
- Keep temporary task logs, hypotheses, and unverified findings in session artifacts, not repository instructions.
- Label planned features separately from implemented behavior. Recheck volatile capability claims against the current branch before relying on them.

## Codebase and entry points

- Small C++23 game engine/editor; engine code is under `Source/`, assets under `Assets/`.
- `Source/EngineMain.cpp` starts the application with `Application::start("config.xml", ...)`.
- `Source/CMakeLists.txt` builds `Divide-Framework-Lib` from `EngineMain.cpp` and `ENGINE_SOURCE_CODE`, plus the engine/editor and ProjectManager executables.
- Executable entry points: `Source/Executable/main.cpp` and `Source/ProjectManager/ProjectManager.cpp`.
- Build wiring: `CMakeLists.txt`, `Source/CMakeLists.txt`, `Source/CMakeHelpers/GlobSources.cmake`.
- Dependencies: recursive submodules and vcpkg (`vcpkg.json`, presets/toolchain); the human dependency list is in `README.md`.

## Conventions and safe changes

- Start with a data-oriented design: favor batch processing, cache-friendly access, parallelism, and minimal overhead. Keep OOP, virtual dispatch, and large objects minimal; they are acceptable for high-level orchestration, not the default for performance-sensitive engine code.
- Avoid unnecessary memory allocations. Prefer appropriately sized arrays, reusable capacity/high-water-mark allocations, and existing arena allocators. Keep `new`/`delete` and `malloc`/`free` out of the main rendering loop except where unavoidable and explicitly justified; reuse or preallocate storage instead.
- Keep code cross-platform across Windows, Linux, and macOS on both x64 and arm64. Avoid unguarded platform APIs or architecture-specific assumptions; provide appropriate portable paths for SIMD, alignment, and platform functionality.
- Reuse target variables in `Source/CMakeLists.txt`: `APP_LIB_DIVIDE`, `APP_EXE_DIVIDE`, `APP_EXE_PROJECT_MANAGER`.
- Keep compile flags centralized in `DIVIDE_COMPILE_OPTIONS` / `THIRD_PARTY_COMPILE_OPTIONS`; reuse shared lists instead of duplicating warning flags. Preserve third-party warning suppressions.
- Preserve CMake variables/presets; expose new feature flags through `CMakePresets.json`.
- For platform changes, inspect `Source/Platform/` and platform cache variables in `CMakePresets.json`, such as `WINDOWS_OS_BUILD` and `MAC_OS_BUILD`.
- Editing `Source/EngineIncludes_pch.h` or `Source/CEGUIIncludes_pch.h` can trigger a full rebuild.

## Build and agent/CI validation

- Clone with `--recurse-submodules`; vcpkg and third-party dependencies require it.
- Select a matching configure/build preset from `CMakePresets.json` (for example `macos-clang-editor` or `windows-msvc-editor`). VSCode's CMake extension also supports preset selection.
- Configure with `cmake --preset <configurePreset>`.
- Build the smallest affected target with `cmake --build --preset <buildPreset> --target <target>`.
- Presets control options such as `ENABLE_OPTICK_PROFILER`, `ENABLE_FUNCTION_PROFILING`, `ENABLE_MIMALLOC`, and `editor-build`.
- Internal Catch2 tests live in `Source/UnitTests`; `BUILD_TESTING_INTERNAL` and `RUN_TESTING_INTERNAL` control them.
- Consult `.github/workflows/Windows.yml`, `.github/workflows/Linux.yml`, `.github/workflows/MacOS.yml`, and their referenced actions for CI build configuration.
- Start with the smallest relevant build/test selection; expand when results require it.
- Distinguish checks actually run from checks not run or blocked. Local success does not establish success on other platforms or in CI.
- Documentation-only changes require no build unless they affect an executable example or have a dedicated documentation check.

## Subsystem routing

| Task | Read |
|---|---|
| Command submission, render stages, lighting, PostFX, feature integration | [Rendering](docs/architecture/rendering.md) |
| GLSL/SPIR-V, reflection, shader caches, descriptor bindings | [Shaders and descriptors](docs/architecture/shader-descriptors.md) |
| GL/Vulkan allocation, uploads, device capabilities, presentation conventions | [GPU resources](docs/architecture/gpu-resources.md) |
| Mesh import, LoD, vertex layout, texture decode/cache | [Asset pipeline](docs/architecture/asset-pipeline.md) |
| Task pools, dirty ranges, asynchronous work, queue synchronization | [Threading](docs/architecture/threading.md) |

## Continuous integration

CI checks `master` and all open pull requests daily at 00:00 UTC (GitHub may delay scheduled runs).
Linux, Windows, and macOS builds run only for head commits not previously queued for that target.
Commit statuses record queued and completed builds, including on fork PRs; unchanged commits are
not rebuilt, even after a failed build. Failed dispatches are retried at the next daily check.
The first check builds every current target. PRs are built at their head commit, not a test merge.
The platform workflows use `workflow_dispatch` for the scheduler, not push or PR triggers.
These workflows must be present on the repository's default branch before scheduling takes effect.
