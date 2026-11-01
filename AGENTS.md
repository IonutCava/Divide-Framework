# Agent build and validation

Keep AI-specific instructions in this file or `.github/copilot-instructions.md`,
not in README.md. README is the human-facing framework overview.

## Prerequisites

Initialize the pinned vcpkg submodule with `git submodule update --init -- vcpkg`.
The project requires CMake >= 3.29.2, Ninja, and a C++23-capable compiler. Shared CI
setup pins CMake/Ninja in `.github/actions/common-config/action.yml`.

## Canonical validation

For features and fixes, use **Windows/MSVC Debug editor with instrumentation**,
enabling both Optick and internal function profiling. Run from an x64 Visual Studio
developer shell:

```shell
cmake --preset windows-msvc-editor-instrumentation -DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF
cmake --build --preset windows-msvc-debug-editor-instrumentation --parallel 2
ctest --preset test-windows-msvc-debug-editor-instrumentation
```

The Windows workflow runs this validation for pull requests and eligible scheduled
builds. To run only this configuration on a pushed branch:

```shell
gh workflow run Windows.yml --ref <branch> -f validation_only=true
```

CTest failures fail CI; scheduled runs with no recent commits explicitly report
that no build or tests ran. Tests currently depend on the engine executable and
full dependency graph. Unit tests are not GPU rendering validation.

## Dependency cache

Shared setup restores vcpkg binary archives through `actions/cache` and the
supported `files` provider, not the removed `x-gha` provider. Cache identity includes
OS, host/target architecture, compiler/toolchain, the pinned vcpkg revision,
manifest, and overlay ports. vcpkg additionally checks each package's ABI before
using an archive. Per-run keys allow later runs to save newly built binaries.

Archives are saved after successful configuration, before engine compilation, so a
later compile or test failure does not discard prepared dependencies. Only
archives are cached, not installed trees or engine build outputs. Cold builds
still require upstream downloads and compilation; check the setup summary's archive
count and vcpkg's package-restoration logs to distinguish cold and warm runs. Run
the targeted workflow twice on the same branch/toolchain to measure cache reuse.

## Agent environments

For Actions-backed Copilot, `.github/workflows/copilot-setup-steps.yml` installs
Ubuntu prerequisites and configures Linux dependencies before the agent firewall
activates. It deliberately avoids CI's destructive workspace cleanup. Its cache
is shared with matching Linux CI jobs; it cannot consume Windows binaries. A cold
setup may exceed Copilot's 59-minute setup limit: prime the Linux cache in normal CI
first. Setup must be merged into the default branch to apply to future sessions.

Ubuntu agents can run supplementary checks:

```shell
cmake --build --preset linux-gcc-debug --parallel 2
ctest --preset test-linux-gcc-debug
```

These do not replace the Windows instrumented validation. If the agent environment
is not already configured, first run `cmake --preset linux-gcc
-DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF`.

The Copilot setup workflow applies only to Actions-backed sessions. Other cloud
sandboxes, local agents, and custom environments need equivalent tool/dependency
provisioning. Downloads after setup may require firewall access to upstream hosts
and redirect mirrors (including SourceForge); prefer preloading dependencies over
disabling the firewall.
