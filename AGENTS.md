# Agent Instructions for Divide-Framework

<!-- Copyright (c) 2026 DIVIDE-Studio -->

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
- Preserve intentional formatting-only expressions used to align related code, even when they are functionally redundant (for example, `x * 1`, `y * 2`, `z * 3`). Do not simplify or remove these alignment anchors when editing nearby code.
- Put copyright headers before include guards; files using `#pragma once` may keep that directive first, with the copyright header immediately after it.
- Put opening braces for scopes on their own line.
- End every source, configuration, and documentation file with a newline.
- Prefer engine numeric aliases (`U8`, `U16`, `U32`, `U64`, `I8`, `I16`, `I32`, `I64`, `F32`, `D64`) over standard fixed-width aliases. Use `size_t` for container sizes and indices.
- Keep common or heavy headers in `Source/EngineIncludes_pch.h` rather than adding them to individual implementation files or broadly included headers.
- Name `static constexpr` functions in PascalCase.
- Preserve CMake variables/presets; expose new feature flags through `CMakePresets.json`.
- For platform changes, inspect `Source/Platform/` and platform cache variables in `CMakePresets.json`, such as `WINDOWS_OS_BUILD` and `MAC_OS_BUILD`.
- Editing `Source/EngineIncludes_pch.h` or `Source/CEGUIIncludes_pch.h` can trigger a full rebuild.
- In code that includes engine headers, use the engine's numeric conversion helpers (`to_U32`, `to_I32`, `to_U64`, `to_F32`, `to_size`, ...) from `Source/Platform/Headers/PlatformDataTypes.h` instead of `static_cast` to numeric types; they validate conversions at runtime. Sources that deliberately avoid engine headers (for example `Source/UnitTests/ShaderMathRunner.cpp` and `Source/Rendering/Lighting/Headers/ShadowPassState.h`) keep standard casts.

## Build and agent/CI validation

- Clone with `--recurse-submodules`; vcpkg and third-party dependencies require it.
- Select a matching configure/build preset from `CMakePresets.json` (for example `macos-clang-editor` or `windows-msvc-editor`). VSCode's CMake extension also supports preset selection.
- Configure with `cmake --preset <configurePreset>`.
- Build the smallest affected target with `cmake --build --preset <buildPreset> --target <target>`.
- Presets control options such as `ENABLE_OPTICK_PROFILER`, `ENABLE_FUNCTION_PROFILING`, `ENABLE_MIMALLOC`, and `editor-build`.
- Internal Catch2 tests live in `Source/UnitTests`; `BUILD_TESTING_INTERNAL` and `RUN_TESTING_INTERNAL` control them.
- Consult `.github/workflows/Windows.yml`, `.github/workflows/Linux.yml`, `.github/workflows/MacOS.yml`, and their referenced actions for CI build configuration.
- Start with the smallest relevant build/test selection; expand when results require it.
- Unit tests are for engine code only. Use the existing Catch2 C++ test framework in `Source/UnitTests`.
  Do not add or retain Python-based or other external-tool-driven automated test harnesses.
  Do not add or retain automated tests for CMake helpers, build-timing tools, CI orchestration, or the build environment.
- Distinguish checks actually run from checks not run or blocked. Local success does not establish success on other platforms or in CI.
- On runtime failures (asserts, crashes, GPU/device-loss, startup issues), inspect `Logs/errors.log` and `Logs/console.log` before concluding root cause. If files are locked while debugging, stop the process and re-read them immediately after exit.
- Documentation-only changes require no build unless they affect an executable example or have a dedicated documentation check.

### Canonical validation and dependency provisioning

The project requires CMake >= 3.29.2, Ninja, and a C++23 compiler; CI pins tool
versions in `.github/actions/common-config/action.yml`.
For features and fixes, use Windows/MSVC Debug editor with both Optick and internal
function profiling from an x64 Visual Studio developer shell:

```shell
cmake --preset windows-msvc-editor-instrumentation -DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF
cmake --build --preset windows-msvc-debug-editor-instrumentation --parallel 2
ctest --preset test-windows-msvc-debug-editor-instrumentation
```

For a targeted run on a pushed branch:

```shell
gh workflow run Windows.yml --ref <branch> -f validation_only=true
```

This targeted run does not report a full daily-matrix status. CTest failures fail
CI. Tests depend on the full engine executable/dependency graph and are not GPU
rendering validation.

Shared setup restores vcpkg binary archives with the supported `files` provider and
`actions/cache`, replacing the removed `x-gha` provider. Cache identity includes
OS, host/target architecture, compiler/toolchain, pinned vcpkg revision, manifest,
and overlays. vcpkg checks package ABIs before reuse. Per-run keys save new archives
after configuration, before engine compilation, even if compilation/tests later fail.
Installed trees and engine outputs are not cached. Compare archive counts and
package-restoration logs across two runs to distinguish cold and warm builds.
Checkout and cache actions use Node.js 24-compatible releases (v5); keep nested
composite-action references aligned to avoid deprecated Node.js 20 runtimes.

Actions-backed Copilot setup installs Ubuntu prerequisites and configures
`linux-gcc` dependencies before its firewall activates, without destructive
checkout cleanup. It shares matching Linux CI caches, not Windows binaries. Cold
setup can exceed the 59-minute limit; prime caches in regular CI if needed. Setup
must reach the default branch to affect future sessions.

On configured Ubuntu agents, supplementary checks are:

```shell
cmake --build --preset linux-gcc-debug --parallel 2
ctest --preset test-linux-gcc-debug
```

These do not replace Windows instrumented validation. Outside Actions-backed
sessions, provision equivalent tools/dependencies; configure with
`cmake --preset linux-gcc -DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF`
if needed. Upstream downloads and redirect mirrors (including SourceForge) may
require firewall access; prefer preloading dependencies over disabling the firewall.

ASan/UBSan configuration applies to project targets, not preinstalled vcpkg
dependencies. `ConfigureSanitizers.cmake` runs after manifest provisioning;
its late `VCPKG_BUILD_TYPE` cache assignment does not rebuild or instrument
dependencies. Do not report sanitizer runs as dependency instrumentation.

Windows matrix steps invoke CMake/CTest presets directly after MSVC setup.
Do not reinitialize MSVC through `run-cmake`: its automatic `vcpkg env` lookup
can select Visual Studio's bundled, manifest-only vcpkg instead of the pinned
repository checkout.

### Dependency configuration

bzip2 and FreeType overlays preload byte-identical source archives from Debian
and FreeType's official GitHub mirror, respectively, using the unchanged
upstream SHA-512 hashes. They reuse build logic and patches from the pinned
vcpkg submodule. When updating vcpkg, keep these overlay versions/features in
sync with those builtin ports and recheck archive hashes; do not weaken hashes
or substitute merely source-equivalent archives.

Optick is fetched and linked only when `ENABLE_OPTICK_PROFILER` is enabled;
the Windows and Linux instrumentation presets enable it. Catch2 is installed
and discovered only with `BUILD_TESTING_INTERNAL=ON` (the vcpkg `tests`
feature). mimalloc remains installed and discoverable in every build;
`ENABLE_MIMALLOC` still controls the existing allocator integration.
OpenAL, NRI and imgui-node-editor remain available.

SDL_mixer is built for WAV, OGG/Vorbis, FLAC and MIDI/FluidSynth, without the
other optional audio codecs. For `.mid` and `.midi` playback, set
`DIVIDE_MIDI_SOUNDFONT` to a readable SoundFont file; missing or unreadable
fonts are reported as load errors. SoundFonts are not bundled: choose one
whose license permits your intended use and distribution.

The Assimp overlay builds only the engine's supported 3DS, ASE, FBX, MD2,
MD5, OBJ, X, COLLADA and GLTF importers. All exporters are disabled; the
unused IFC/Blender triangulation and 3MF export dependencies are removed.
DevIL remains the DDS loader, with optional image-codec features disabled.
NVTT remains available for DDS cache compression until a replacement
covers that functionality as well.
The NVTT overlay removes upstream `-march=native`: cached dependency archives
must use the target toolchain's baseline, not the cache producer's CPU ISA.

Boost.Asio uses monotonic `steady_timer` deadlines, without its optional
deadline-timer and coroutine/spawn features. Scene registration uses a local
forwarding shared-pointer factory instead of Boost.Functional.

Jolt's SIMD flags are explicit, selected before vcpkg's manifest installation
through `ConfigureDependencies.cmake` and the overlay's SIMD features.
`ValidateSIMDRequest.cmake` configures a dependency-free preflight using the same
target/compiler detector as the engine. Unsupported requested x86 levels are
rejected before installation rather than downgraded underneath Jolt's public flags.
Preflight forwards direct custom toolchains or vcpkg's chainload toolchain using
absolute paths; the vcpkg provisioning toolchain itself is excluded.
SSE42 (and AUTO's conservative dependency baseline) disable AVX, AVX2, F16C,
FMA, LZCNT and TZCNT; AVX does not enable AVX2-era instructions.
ARM targets disable all x86 options. macOS x86_64 dependencies use AVX on Intel
and SSE42 on detected Rosetta targets. Manual installs for AVX/AVX2/AVX512 builds must
also select the corresponding `--x-feature=jolt-avx`, `jolt-avx2` or `jolt-avx512`.
The macOS x86_64 presets default to AVX on Intel hosts. On Apple Silicon,
`Toggle_Available_Vector_Extensions()` explicitly overrides Rosetta requests
with SSE42 in both preflight and engine configuration. This logged portability
override is the exception to strict rejection of unsupported x86 requests.

### Dependency validation and timing

- The vcpkg `tests` feature is synchronized with `BUILD_TESTING_INTERNAL` before `project()`. For manual dependency installation in a test build, add `--x-feature=tests`.
- Resolve plans with `./vcpkg/vcpkg install --dry-run --triplet <triplet> --overlay-ports=Source/CMakeHelpers/OverlayPorts`; compare tests-off and tests-on plans separately.
- Record timings with `python Source/CMakeHelpers/measure_build.py --scenario cold --output <session-dir>/cold.json --install "<vcpkg install command>" --configure "<cmake configure command>" --build "<cmake build command>"`.
- Repeat with `warm` and `incremental`, keeping triplet, configuration, build parallelism and tool versions identical. For incremental measurements, edit a representative source file before running.
- Scenario labels do not reset caches. Use isolated build/install/download directories and explicitly set `VCPKG_BINARY_SOURCES=clear` for a genuine cold run. Reuse the same install directory in the install and configure commands; otherwise configure will perform an additional installation.
- The tool records each stage's duration, exit status and log, and stops on failure. Do not report a failed download/configure as a successful build-time baseline or claim engine speedups from dependency counts alone.
- Commands also accept JSON arrays of strings. On Windows these arrays are required;
  for example `[".\\vcpkg\\vcpkg.exe", "install", "--triplet", "x64-windows"]`.
  POSIX shell-quoted command strings remain supported on Unix; no shell is executed.

## Subsystem routing

| Task | Read |
|---|---|
| Command submission, render stages, lighting, PostFX, feature integration | [Rendering](docs/architecture/rendering.md) |
| GLSL/SPIR-V, reflection, shader caches, descriptor bindings | [Shaders and descriptors](docs/architecture/shader-descriptors.md) |
| GL/Vulkan allocation, uploads, device capabilities, presentation conventions | [GPU resources](docs/architecture/gpu-resources.md) |
| Mesh import, LoD, vertex layout, texture decode/cache | [Asset pipeline](docs/architecture/asset-pipeline.md) |
| Project/scene selection, scene graph XML, scene activation and saves | [Scene loading](docs/architecture/scene-loading.md) |
| Task pools, dirty ranges, asynchronous work, queue synchronization | [Threading](docs/architecture/threading.md) |
| Client connections, heartbeat timers, networking test fixtures | [Networking](docs/architecture/networking.md) |

## Continuous integration

CI checks `master` and all open pull requests daily at 00:00 UTC (GitHub may delay scheduled runs).
Linux, Windows, and macOS builds run only for head commits not previously queued for that target.
Commit statuses record queued and completed builds; unchanged commits are
not rebuilt, even after a failed build. Failed dispatches are retried at the next daily check.
Platform reporters also publish each matrix job's result under its exact job name
on `ci_sha`, preserving the existing per-job branch-protection requirements when
workflow dispatches execute on a different commit. Missing, skipped, cancelled,
or unsuccessful jobs never produce successful statuses. Keep the job-name lists
in `.github/scripts/report-ci.cjs` aligned with the platform matrices.
Failed Linux builds run existing test executables under GDB in list-only mode
to capture startup crashes, including the faulting instruction and backtrace.
The first check builds every current target. PRs are built at their head commit, not a test merge.
The platform workflows use `workflow_dispatch` for the scheduler, not push or PR triggers.
These workflows must be present on the repository's default branch before scheduling takes effect.

The daily scheduler executes only master and same-repository PR heads. Fork or
unavailable head repositories receive an explicit error status without dispatch:
default-branch runs expose default-branch cache-write credentials to source code,
and ABI hashes do not authenticate archive contents. Fork execution is disabled
until a PR-isolated validation/cache scope is implemented. Do not re-enable it
by merely changing cache key prefixes or omitting cache-save steps.

Platform workflows keep trusted local actions at `github.sha` in the workspace
root. Target sources (`ci_sha`, including fork heads) go in `ci-source`; trusted
vcpkg and CMake steps explicitly consume that directory. Do not check target
sources over the root: subsequent local actions would then come from the PR.
