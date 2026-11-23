# Agent Instructions for Divide-Framework

<!-- Copyright (c) 2026 DIVIDE-Studio -->

## Project overview

- Divide-Framework is a C++23 game engine and editor. Engine code is under `Source/`; assets are under `Assets/`.
- `Source/EngineMain.cpp` starts the application. Executable entry points are `Source/Executable/main.cpp` and `Source/ProjectManager/ProjectManager.cpp`.
- CMake configuration lives in the repository root and `Source/CMakeLists.txt`. Dependencies use recursive submodules and vcpkg.
- Treat source code and build configuration as authoritative when notes disagree.

## Documentation

- Keep `README.md` focused on the project, dependencies, and human build/run instructions.
- Keep project-wide agent guidance here. Put subsystem details in `docs/architecture/` and reusable procedures in `.github/skills/`.
- Keep this file focused on stable concepts and coding standards. Omit temporary findings and one-off implementation details.
- Update relevant guides when a code change affects documented behavior. Link new architecture guides from `docs/architecture/index.md` and the routing table below.
- Record durable architectural decisions in `docs/decisions/` when needed. Keep temporary notes in session artifacts.
- Use source paths and symbols in documentation. Distinguish implemented behavior from planned work.

## Coding standards

- Favor data-oriented designs, contiguous storage, batching, and parallel work where they fit. Keep hot paths simple and avoid unnecessary allocations.
- Keep system boundaries generic. Feature-specific policy belongs to the owning scene or subsystem, not shared infrastructure.
- Prefer suitable EASTL containers and algorithms, using existing engine aliases where available. Preserve required ordering, lifetime, and thread-safety guarantees.
- Use the `Attorney` pattern for controlled access to private or protected members. Attorney helpers must be declared as `class` and expose only the required operations.
- Keep code portable across Windows, Linux, and macOS on x64 and arm64. Guard platform-specific APIs and provide portable alternatives where needed.
- Reuse `APP_LIB_DIVIDE`, `APP_EXE_DIVIDE`, and `APP_EXE_PROJECT_MANAGER` in `Source/CMakeLists.txt`. Keep project and third-party compiler flags in their shared CMake lists.
- Prefer engine numeric aliases (`U8`, `U16`, `U32`, `U64`, `I8`, `I16`, `I32`, `I64`, `F32`, `D64`) and `size_t` for container sizes and indices. In engine code, use numeric conversion helpers from `PlatformDataTypes.h` rather than unchecked numeric casts.
- Keep common or heavy headers in the appropriate precompiled header. Avoid adding them to broadly included headers.
- Put opening braces on their own lines. Put copyright headers before include guards; files using `#pragma once` may keep it first. End source, configuration, and documentation files with a newline.
- Prefer clear function and variable names so code explains itself. Keep comments sparse. Comment only non-obvious algorithms or constraints, such as SIMD operations or hash steps; do not narrate straightforward code.

## Build and validation

- Clone with `--recurse-submodules`. The project requires CMake 3.29.2 or newer, Ninja, and a C++23 compiler.
- Use a matching configure, build, and test preset from `CMakePresets.json`. Build the smallest affected target and run the relevant tests:

  ```shell
  cmake --preset <configure-preset>
  cmake --build --preset <build-preset> --target <target>
  ctest --preset <test-preset>
  ```

- Engine unit tests use Catch2 under `Source/UnitTests`. Add focused tests for new engine behavior. Do not add external-tool-driven test harnesses or tests for build scripts and CI orchestration.
- Start with targeted checks and expand only when their results require it. Report which checks ran; local success does not establish success on other platforms or in CI.
- For runtime failures, inspect `Logs/errors.log` and `Logs/console.log` before concluding the cause.
- Platform CI workflows are `.github/workflows/Windows.yml`, `.github/workflows/Linux.yml`, and `.github/workflows/MacOS.yml`.

## Architecture guides

Start with [the architecture index](docs/architecture/index.md), then read the guide for the affected subsystem.

| Area | Guide |
|---|---|
| Rendering stages, command submission, lighting, and feature integration | [Rendering](docs/architecture/rendering.md) |
| Renderer profiling and frame-time investigations | [Performance diagnosis](docs/architecture/rendering.md#performance-diagnosis) |
| Supervised graphics smoke tests and GPU recovery | [Rendering smoke diagnosis](docs/architecture/rendering.md#framework-smoke-diagnosis), [GPU resources](docs/architecture/gpu-resources.md#vulkan-smoke-readback-and-recovery) |
| Shader bindings, reflection, and caches | [Shaders and descriptors](docs/architecture/shader-descriptors.md) |
| GPU allocation, uploads, and device capabilities | [GPU resources](docs/architecture/gpu-resources.md) |
| Mesh import, LoD, vertex layout, and texture caching | [Asset pipeline](docs/architecture/asset-pipeline.md) |
| Scene loading, transforms, bounds, XML, and saves | [Scene loading](docs/architecture/scene-loading.md) |
| Editor windows, docking, properties, and output | [Editor windows](docs/architecture/editor.md) |
| Task pools, asynchronous work, and synchronization | [Threading](docs/architecture/threading.md) |
| Client connections and heartbeat timers | [Networking](docs/architecture/networking.md) |
| Assertions and fatal unexpected calls | [Assertion contract](docs/architecture/index.md#assertions-and-fatal-calls) |
