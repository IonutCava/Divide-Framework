# Vulkan test-and-fix harness: approved design

<!-- Copyright (c) 2026 DIVIDE-Studio -->

Status: implemented. This document preserves the approved design and initial
investigation context; it is not the current repair log. The maintained runner
contract and usage are in [Rendering](../architecture/rendering.md#vulkan-smoke-diagnosis),
with [readback and recovery](../architecture/gpu-resources.md#vulkan-smoke-readback-and-recovery)
and [scene selection](../architecture/scene-loading.md) documented separately.
Both backends support supervised capture and start in Play mode. Rendering fixes
and the smoke harness are committed separately; attempt evidence remains local.

## Agreed requirements

- Run locally on Windows using the MSVC Debug editor build with Optick and internal function profiling.
- On the first Vulkan warning or error, automatically capture debugger context, then terminate the failed process. If suitable debugger tooling is unavailable, flush diagnostic evidence and terminate immediately instead. The user approved one exception: log the `BestPractices-specialuse-extension` warning for `VK_EXT_custom_border_color` without terminating, while keeping the existing extension enabled.
- Ignore diagnostic TODO/STUBBED messages as failure triggers. Do not ignore a Vulkan warning/error merely because its text contains "TODO".
- Use the Default project, editor enabled, existing rendering settings, and a 300-frame smoke test after scene loading. Capture the scene and the final main editor framebuffer for review.
- A device loss does not by itself stop the loop: wait for verified recovery, then continue diagnosing and fixing. Pause for user approval if the same device loss recurs after three attempted repairs without progress.
- Default to at most 25 repair iterations, with earlier stops for other repeated no-progress failures. The user approved a 200-attempt limit for the existing Games/WarScene loop; pass `-MaxAttempts 200` when continuing it. Failed or interrupted builds do not consume an iteration.
- Existing uncommitted Vulkan changes may be revised as necessary. Preserve their original diff and leave unrelated user changes alone.
- Preserve compatible evidence for a later equivalent OpenGL run and rendering-difference investigation. OpenGL parity fixes are not part of this initial loop.

## Initial evidence (historical)

At planning time, the working tree already contained changes in Vulkan resources,
render targets, device/swapchain handling, RendererTests, configuration, AGENTS.md,
and editor state. The user authorized revising those changes while preserving
unrelated work. The original no-commit instruction was subsequently replaced by
explicit authorization to commit and push renderer fixes first, followed by a
standalone harness commit.

The inspected logs contain:

- `VUID-vkCmdBeginRendering-pRenderingInfo-09589`: a depth resolve image is tracked as read-only where attachment layout is required.
- `VUID-vkCmdBeginRendering-pRenderingInfo-09593`: a colour resolve image has the corresponding mismatch.
- Non-Vulkan TODO/STUBBED diagnostics logged as errors.

These are reproduction leads, not proof of the complete driver-crash cause. Archive existing logs before launching anything: Console::Start truncates the two main log files.

Relevant integration points:

- `Source\Platform\Video\RenderBackend\Vulkan\VKWrapper.cpp`: debug callback, message filters, validation-layer and synchronization-validation setup.
- `Source\Platform\Video\RenderBackend\Vulkan\vkSwapChain.cpp`: queue submission and actual presentation results.
- `Source\Core\Kernel.cpp`: command-line options, scene startup, warmup and frame lifecycle.
- `Source\Core\Application.cpp`: shutdown and profiler state changes; profiling currently can disable API debugging.
- `Source\Core\Console.cpp`: buffered logging and flushing; the callback currently toggles shared console flags.
- `Source\EngineMain.cpp` and `Source\Executable\main.cpp`: application result and process exit-code propagation.
- `Source\UnitTests\Test-Engine\RendererTests.cpp`: existing engine regression tests.

MSVC is installed at `C:\Program Files\Microsoft Visual Studio\18\Insiders`. Its x64 developer environment provides MSVC 19.51, CMake/CTest 4.3.1 and Ninja 1.13.2. WinDbg 1.2610.1001.0 is installed; its bundled x64 CDB passed a scripted stack-capture check, although it is not on PATH. Resolve its location from the installed package rather than hard-coding the versioned WindowsApps path.

The local machine reports Windows 11 and an AMD Radeon RX 9070 XT. Vulkan SDK 1.4.363.0 includes the Khronos validation manifest and DLL; activation during the renderer run still needs verification. Radeon GPU Detective 1.6.3.7, the Radeon Developer Panel, and RenderDoc 1.47 are installed. RenderDoc's Vulkan layer reports that it is registered.

## Design

### 1. Opt-in engine diagnostic mode

Add a small, explicit runtime smoke-test mode rather than changing normal editor behavior. Proposed inputs are backend, project, run identifier/output directory, target frame count, and failure policy. Validate inputs and reject incompatible settings.

Force the requested backend for the run without persisting edits to XML or silently falling back to OpenGL. Require the validation layer and synchronization validation to be active for Vulkan; missing validation is a preflight failure, never a successful run. Keep validation active throughout profiling state changes. Start with existing core/synchronization checks; GPU-assisted validation is a separate, optional diagnostic escalation.

Capture all Vulkan warning/error messages in this mode before existing best-practice suppression filters can hide them. Keep ordinary interactive behavior and its filters unchanged outside this mode. Record informational messages without making them fatal. The approved custom-border advisory remains a warning in the evidence; every other warning and every error remains fatal.

Use a thread-safe first-failure latch and bounded diagnostic capture. Record the full first message, severity/type, VUID/message ID, object names/handles, thread, frame/presentation counters, current stage/debug labels where already available, and recent bounded rendering context. Avoid per-frame heap churn.

Write and flush the failure record synchronously before breaking or terminating. Do not rely on asynchronous GUI console callbacks or a periodically tailed log to stop invalid GPU work. Audit callback reentrancy, concurrent messages, early initialization, teardown, and long message handling.

Never destroy Vulkan objects or wait for device idle inside the validation callback. With an attached automated debugger, break at the failure site, capture context, and terminate without resuming application execution. Without that debugger, terminate immediately after diagnostic capture. Returning VK_TRUE from the callback is not the shutdown mechanism.

For a reported `VK_ERROR_DEVICE_LOST`, investigate optional `VK_EXT_device_fault` or `VK_KHR_device_fault` support and capture available fault data before the failing logical device is destroyed. Check extension and feature support first; capture outside the validation callback and bound the whole attempt with the supervisor. Record unavailable or failed fault collection explicitly, and never deliberately trigger device loss to obtain a dump.

### 2. Bounded debugger and process supervision

Prefer an existing x64 Windows debugger capable of scripted exception handling, matching symbols and local context capture. Treat the intentional validation breakpoint separately from loader/system breakpoints and genuine crashes.

On failure, capture all-thread stacks, exception context, loaded module versions, and a local minidump when supported, then terminate the owned application. Bound debugger capture time independently; if capture hangs, terminate the owned process tree and retain whatever evidence exists. No manual continue prompt.

If compatible tooling is unavailable, explicitly record that stack/dump capture was unavailable and use the accepted immediate-termination path. Ask before installing optional debugger software; no installation is part of this plan.

The supervisor owns one renderer process at a time and addresses cleanup by its exact PID/process tree, never by executable name. Refuse concurrent harness runs or a conflicting existing renderer using the same logs/configuration. Do not terminate an unrelated user-launched application.

The installed packaged CDB can launch outside the supervisor's job. Copy its
runtime into the attempt, verify both debugger and controlled child job
membership in preflight, and keep the real renderer at a bounded gate before
platform/GPU initialization until its PID, debugger parent and job membership
are verified. Track verified process identities for bounded cleanup if job
inheritance fails.

Limits are 180 seconds for startup, 30 seconds without an eligible present after readiness, 120 seconds by default for the rendering phase, 10 seconds for debugger capture, and 15 seconds for normal shutdown. Each eligible present is recorded for the no-progress watchdog. The overall render deadline is adjustable with `-RenderTimeoutSeconds` from 120 to 7200 seconds for slower instrumented scenes; the no-progress limit remains unchanged. Log each timeout distinctly. Compilation has a separate bounded timeout, initially 45 minutes.

Fail closed on missing/corrupt results, unexpected exit, crash, timeout, or contradictory evidence. Distinguish validation failure, build/test failure, startup failure, device loss, suspected GPU hang, and normal completion.

On device loss, an observed driver reset, or a suspected GPU hang, preserve the available evidence and enter a recovery-wait state rather than treating the event as a terminal loop failure. Terminate only the owned failed process tree; do not try to resume its lost Vulkan device. A debugger/fault-data capture is attempted first when feasible and bounded by the supervisor.

Wait before any new full renderer attempt. In a separate, bounded probe process, identify the same physical GPU, create a fresh Vulkan logical device, submit minimal valid work, and confirm its fence completes. A detected display-driver event or `vulkaninfo` enumeration alone is not sufficient proof. Record each result; limit and back off probes so a sick driver is not hammered. A successful probe is evidence of responsiveness, not a guarantee that the next renderer run is safe.

Keep waiting with periodic probes until recovery or user cancellation; waiting and probes do not consume the repair iterations. After recovery, diagnose and repair from the preserved evidence before rebuilding and making the next renderer attempt. If the same device-loss signature recurs without increased scene-presentation progress after three attempted repairs, pause for user approval; repeat that pause after each further three such repairs. The approved iteration cap still applies.

A user-mode watchdog cannot guarantee preventing a driver reset or recover a system-wide hang. Persist the current state and evidence so the user can resume after a supervisor or operating-system restart; do not create an automatic startup task, reboot the machine, or alter Windows TDR settings.

### 3. Reliable success contract

Count 300 successful main-window scene presentations only after scene readiness, excluding splash/loading/warmup and auxiliary editor windows. Do not substitute `GFXDevice::FrameCount()`, which increments independently of successful presentation.

Record successful present results and meaningful completion/progress evidence; present acceptance alone is not proof of correct pixels. Classify suboptimal/out-of-date/recreation outcomes explicitly rather than calling every present attempt a success.

Capture a completed scene render image near the end using the existing screenshot/readback path where appropriate. Ensure its asynchronous work completes and the output exists before shutdown. Prefer a scene image rather than a desktop screenshot that could capture unrelated windows.

For faster diagnosis of startup or shutdown repairs, `-DiagnosticPresents` may
request 1–299 presentations with the same supervision and capture checks.
Such a run consumes an attempt but cannot complete the 300-present success
contract; the full run remains required afterward. Successful diagnostics
record their progress and break the repeated-failure sequence. After a
non-device no-progress stop, `-ApproveStalledNonDeviceLoss` permits one
diagnostic attempt only when explicitly approved by the user; a failed attempt
stops again without discarding its failure or recovery evidence.

Also capture the final main framebuffer after editor ImGui rendering. In the
Vulkan smoke run, copy the acquired swapchain image before presentation, wait
for the frame fence, and save it without including unrelated desktop windows.
The image excludes OS decorations and detached windows. Promote the pending
image only after a clean exit. An equivalent OpenGL framebuffer capture remains
follow-up work for a later comparison.

Request normal shutdown after the success target. Continue observing validation through teardown. Publish success only after no Vulkan warnings except the explicitly logged custom-border advisory, no Vulkan errors, the required scene presentations, both images, clean shutdown and a matching successful exit/result record. If visual output is blank or incorrect, keep it marked for review rather than claiming rendering correctness.

Use distinct machine-readable failure reasons and process exit status. Audit Engine::RunInternal result propagation so a runtime failure cannot accidentally return success.

### 4. Local runner and evidence

Use a small PowerShell runner for deterministic configure/build/test/launch/capture operations. This is operational tooling, not an external automated unit-test framework. The coding agent owns diagnosis and code changes; the runner does not implement a blind text-replacement fixer or require embedded credentials.

Use the existing presets from one x64 Visual Studio developer environment:

```text
cmake --preset windows-msvc-editor-instrumentation -DBUILD_TESTING_INTERNAL=ON -DRUN_TESTING_INTERNAL=OFF
cmake --build --preset windows-msvc-debug-editor-instrumentation --parallel 2
ctest --preset test-windows-msvc-debug-editor-instrumentation
```

During iteration, build the smallest affected targets. The user requested deferring new unit tests until both Vulkan and OpenGL render correctly; use the runner's `-SkipUnitTests` option during repairs if appropriate. After both backends work, add the relevant Catch2 coverage and run focused tests plus the full internal CTest preset. Configure `RUN_TESTING_INTERNAL=OFF` while building so tests do not run implicitly. Unit tests do not replace the real GPU smoke test. Never run a stale executable after a failed build; record executable identity and build outcome.

Reuse Ninja's incremental build and the project's precompiled headers. Benchmark higher local build parallelism against the `--parallel 2` baseline while monitoring memory; do not assume it will improve every incremental build. MSVC precompiled-header flags can prevent sccache hits, so do not install a compiler cache or disable precompiled headers without a measured benefit.

Resolve CMake executable outputs and runtime working directory from the build configuration rather than guessing a binary path. Preserve the existing scene/MSAA/features; do not disable features to manufacture a pass.

Store per-attempt artifacts outside tracked source files in an explicit session output directory: build/test logs, copied console/errors/CEGUI logs, validation records, debugger transcript/dump, run configuration, executable/source-state identity, GPU/driver/SDK/layer versions, readiness and presentation counts, timings, exit reason, and image.

Snapshot logs before a new launch and after each exit. Use run IDs and file boundaries so stale log messages cannot fail a new attempt. Preserve the initial dirty baseline and attributable iteration diffs without making commits. Dumps stay local; no automatic upload. Do not clear caches, delete unrelated files, or overwrite the user's baseline evidence.

### Additional tools and skills

- **Vulkan SDK:** `vulkaninfo` reports device, extension and layer availability; `vkconfig` helps inspect layer configuration. Both are discoverable locally. `vkconfig` can modify per-user Vulkan settings, so do not change them unattended; prefer process-scoped or explicit engine configuration. See the [Vulkan Configurator documentation](https://vulkan.lunarg.com/doc/view/1.4.363.0/windows/vkconfig.html).
- **WinDbg/CDB:** the installed WinDbg package includes an x64 CDB suitable for scripted exception handling and stack capture. Resolve the package location at runtime; debugger and minidump integration with the harness still needs implementation and verification.
- **AMD Radeon GPU Detective and Developer Panel:** installed RGD 1.6.3.7 can analyze GPU-crash dumps with execution markers and resource information. Pass `-AmdCaptureExe` to the runner to own capture per attempt and wait for late reports before classification. The installed Adrenalin 26.10.1 meets its reported driver requirement. Its [quickstart](https://github.com/GPUOpen-Tools/radeon_gpu_detective/blob/master/documentation/source/index.rst) requires Crash Analysis mode and a TDR; a normal validation abort or device loss without a TDR may not produce a dump. Do not deliberately induce a TDR.
- **AMD RGD MCP server:** installed locally and verified with MCP 1.30.0; its current package is incompatible with MCP 2.x. Point `RGD_EXE_PATH` to the installed `rgd.exe` and `RGD_DOCS_PATH` to `H:\Tools\rgd_mcp_server-1.0.0.2\docs` when registering it. Review its configuration and data handling before enabling it; never automatically upload dumps or source.
- **RenderDoc:** version 1.47 is installed and its Vulkan layer is registered. Inspect captured Vulkan frames, resources and image contents after the application reaches a capturable frame. Capture can change timing and cannot replace validation or guarantee a capture before an immediate failure.
- **Windows event logs:** inspect display-driver recovery events when available, correlating them by time with local logs. Their absence does not prove that the GPU has recovered.
- **Existing `setup-cpp-language-server` skill:** useful for C++ navigation during implementation; it is not a GPU validation or crash-analysis tool.

Do not install, configure, or enable optional tools without user approval. Crash dumps and frame captures may contain sensitive application data and remain local.

### 5. Agent-driven repair loop

1. Establish the initial source/configuration baseline and preflight, without launching an unprotected renderer.
2. Implement and verify fail-fast diagnostics and process supervision first.
3. Build the renderer and execute one protected smoke attempt; defer unit tests until the smoke test passes.
4. Diagnose the first causal warning/error from its VUID, stack and resource history; prioritize it over downstream cascades.
5. Apply one coherent root-cause repair, then rebuild and retest. Add focused Catch2 coverage after both backends render correctly.
6. Preserve every attempt and compare its failure signature and achieved rendering progress.
7. After device loss or an observed reset, wait for the recovery probe before the next renderer attempt. Pause for approval after three repeated no-progress device-loss repairs. Stop on success, the approved iteration cap, an unrecoverable safety event, missing prerequisites, conflicting user edits, or other repeated no progress. Report unresolved evidence rather than relaxing validation.

Proposed no-progress rule: after three consecutive attempted repairs with the same normalized first-failure signature and no increased scene-presentation progress, pause for user approval for a recurring device loss; stop for other failures. Normalize unstable handles/timestamps, but keep the VUID, stage, resource role and relevant layout/state. Count failed engine tests and completed builds toward the approved iteration budget; failed or interrupted builds and recovery probes do not count.

Do not suppress VUIDs, downgrade warning severity, disable synchronization validation, bypass assertions globally, hide features, or delete shader caches as a substitute for a diagnosed fix. If a repair requires changing rendering behavior or supported features, seek approval.

## Regression coverage and documentation

Add Catch2 tests for new engine policy/state logic: warning/error classification versus TODO diagnostics, first-failure behavior, runtime option validation, scene/presentation eligibility, target completion, device-loss/recovery state transitions, result propagation, and relevant renderer invariants. Test debugger-independent policy without deliberately causing GPU faults.

Do not add Python tests or automated tests for the PowerShell runner, CMake helpers, CI or debugger environment. Verify operational failure handling with controlled manual checks that do not submit intentionally invalid GPU work: missing validation/debugger, synthetic engine diagnostic and device-loss events, failed and successful recovery-probe results, early process exit and a bounded unresponsive helper owned by the check.

Update the relevant rendering/GPU-resource guides and AGENTS.md routing/validation notes with the supported procedure. Keep temporary failure reports in session artifacts. Update copyright headers on touched files while retaining attribution.

## OpenGL follow-up

The runner supports `-Backend OpenGL` with a separate output root and an
`-ExpectedGPU` UUID from the Vulkan run. OpenGL must be selected in the XML
configuration; the context must expose a matching device UUID. Attempt 1
completed 301 eligible presents, captured the scene and main editor framebuffer,
and exited cleanly on the same GPU. Its images have the same dimensions as
Vulkan attempt 15, with the same project and editor layout.

The images look broadly similar on visual inspection, but equal frame numbers
do not guarantee equivalent simulation or exposure. The scene is not
geometry-rich, and editor timing and logs differ. Preserve images now, but defer
image-difference thresholds, parity fixes and any claim of equivalent rendering
to the subsequent comparison task.

## Review boundary

This plan describes a manually invoked local harness plus an agent repair procedure, not a cloud workflow or a group of background agents. The user authorized implementation and protected local GPU runs; it does not authorize unattended installation, scheduled tasks, TDR changes, commits, or pushes.
