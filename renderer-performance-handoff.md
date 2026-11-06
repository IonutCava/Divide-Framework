# Renderer performance handoff

<!-- Copyright (c) 2026 DIVIDE-Studio -->

This is a handoff for a new coding-agent session. It is independent of any
particular agent client: the commands and paths below are local Windows
resources, not Copilot-specific APIs. Read `H:\Divide-Framework\AGENTS.md` and
the relevant guides in `docs\architecture\` before changing the engine.

> **Later update:** The original investigation and scheduler notes below are
> historical. The [continuation update](#continuation-update) records the
> subsequent implemented work, current measurements, and next steps.

## Objective and current state

Improve renderer frame time in the **intentionally instrumented MSVC Debug
editor build**, comparing Vulkan and OpenGL on the same GPU, scene, camera and
settings. The user suggested investigating shared geometry buffers, offsets,
batching and multi-draw, and noted that Debug STL containers can be expensive;
evaluate EASTL or other suitable alternatives where measurements support them.
Preserve rendering correctness and the guarded Vulkan validation/recovery loop.
Do not treat CPU wall time as GPU execution time.

- Checkout: `H:\Divide-Framework`, in place on `features/Rendering_Audit`;
  [PR #49](https://github.com/IonutCava/Divide-Framework/pull/49) is open.
- This original handoff started at `c5cb233693ae77684786180c0ed8213d8cbdba75`
  (`Make unexpected calls terminate`), following `ec192885e` (renderer
  draw-preparation and lifetime fixes). The later continuation supersedes that
  snapshot; inspect the current branch and history.
- Inspect the working tree before changing it. At this handoff, unrelated local
  changes include `imgui.ini`, `docs\architecture\editor.md`,
  `docs\architecture\scene-loading.md`, and the untracked
  `docs\plans\vulkan-debugging-experience.md`. Preserve them.

### Original scheduler prototype (historical)

At this point the scheduler was an unvalidated prototype. It was subsequently
reworked, tested and measured; see the continuation update. Its original
changes were:

- `Source\Managers\Headers\RenderStageSchedule.h` — **untracked** dependency
  graph and atomic predecessor counters.
- `Source\Managers\RenderPassManager.cpp` and
  `Source\Managers\Headers\RenderPassManager.h` — precreate stage tasks, enqueue
  roots and enqueue dependents after the last prerequisite.
- `Source\UnitTests\Test-Engine\TaskPoolTests.cpp` — new graph, concurrent
  completion and callback-progress tests; none has been run.
- `docs\architecture\threading.md` and `docs\architecture\rendering.md` —
  distinguish the unvalidated prototype from the committed waiting-based
  scheduler; update both when the implementation is settled.

First inspect the actual diff, including the untracked header. Review all task
lifetimes, dependency publication and memory ordering, graph validation and
duplicate edges, exactly-once enqueueing if a root completes while other roots
are being queued, worker-only task execution, parent completion and main-thread
callback progress. Compare performance and correctness before deciding whether
to retain the approach. The profiles below do **not** establish that the
prototype will help.

There is a **possible, unconfirmed single-worker starvation** case: a queued
parent might occupy the only worker while newly ready `DONT_CARE_NO_IDLE`
children cannot run through the waiting path. The current renderer pool
normally uses six workers; use a bounded regression and inspect eligibility
before drawing a conclusion.

## Evidence already collected

These are instrumented **CPU wall-clock** measurements, including scheduling,
validation, driver calls, waits and profiling overhead. The parser estimates
exclusive time from nested scopes and only attributes events fully inside a CPU
frame. They are not GPU timestamps; overlapping stage times must not be added.
The WarScene point-light colors and timing are not deterministic.

### Matched restored-shader captures

WarScene, same RX 9070 XT, camera and editor Play-mode setup; warmed captures of
32 complete CPU frames after approximately 100 eligible presents:

| Backend | Debug median | Release medians |
|---|---:|---:|
| OpenGL | 181.05 ms/frame | 78.20, 74.01 ms/frame |
| Vulkan | 517.24 ms/frame | 412.71, 408.90 ms/frame |

These captures precede the current scheduler prototype. Compare actual
configuration and validation state before drawing conclusions across build
types. The source and workload were brought back to the real shaders after the
earlier stub experiment.

Representative Debug per-frame main-thread scope medians:

| Scope | OpenGL | Vulkan |
|---|---:|---:|
| `TaskPool::wait`, inclusive / self | 91.46 / 87.14 ms | 90.47 / 86.98 ms |
| `bindShaderResources`, inclusive | 35.63 ms | 52.91 ms |
| Draw API scope | 9.30 ms, about 7,853 draws | 78.18 ms, about 6,192 indexed-indirect calls |
| `vkQueueSubmit` | — | 158.26 ms |
| Vulkan command-buffer begin | — | 35.80 ms |

`TaskPool::wait` can execute eligible work and service callbacks; it is not
pure idle time. `vkQueueSubmit` is wall time in the call, including possible
validation-layer and driver work; it does not identify GPU execution cost.
Binding Vulkan vertex and index buffers each accounted for about 1 ms/frame,
not the dominant measured cost.

In the same captures, analysis of individual stage-record scopes found:

| Stage | OpenGL record median | Vulkan record median |
|---|---:|---:|
| Shadow | 37.35 ms | 36.66 ms |
| Reflection | 17.56 ms | 17.23 ms |
| Refraction | 18.35 ms | 18.65 ms |
| Display | 33.17 ms | 32.81 ms |
| Node preview | 0.21 ms | 0.21 ms |

Reflection/refraction began after roughly 37 ms, and display/preview after
roughly 56–57 ms, consistent with the existing dependency graph. Waiting-stage
workers previously helped eligible pool work and otherwise slept in bounded
intervals; their wall time is not evidence of equivalent active CPU use.
Whether not-yet-ready tasks occupying workers actually delays the producer's
parallel work remains an experiment, not an established result.

### Shader-stub experiment

Temporary shader stubs were tested and fully reverted. Warmed Debug CPU
medians, in milliseconds per frame:

| Backend | Normal, two runs | All stages stubbed | Fragment-only stubbed |
|---|---:|---:|---:|
| OpenGL | 175.72, 175.16 | 174.46 | 173.39 |
| Vulkan | 502.24, 501.64 | 499.21 | 492.00 |

All-stage stubs collapsed geometry and disabled compute, so these are not
equivalent rendering workloads and do not prove GPU shaders are inexpensive.
Variability also prevents claiming a statistically established speedup.

### Batching and other leads

- Conservative Vulkan indirect batching reduced an earlier capture from about
  7,788 to 6,188 indexed-indirect calls/frame. Its compatibility checks include
  shared vertex/index **buffer handles**, consecutive indirect records,
  matching draw state and push/uniform values, and shader restrictions.
- A geometry sort-key tie-breaker did **not** measurably reduce those calls in
  the tested scene. Do not equate fewer memory allocations or vertex/index
  binds with fewer `VkBuffer` handles or batched draws.
- True geometry pooling needs compatible layouts/index widths, adjusted
  `firstIndex`/`baseVertex`, actual shared buffer handles, and reuse only after
  GPU completion. Audit ordering, instances, descriptors and shader
  `DrawIndex` semantics before implementing it.
- A **suspected, unconfirmed** long-run task leak exists in
  `RenderQueue::sort`: it allocates a parent task before checking for bins
  above the 64-item parallel threshold, but enqueues it only when such a bin
  exists. `TaskPool::AllocateTask` reserves a slot in a 65,536-entry
  thread-local pool until execution. Reproduce with a bounded, non-hanging
  Catch2 regression before calling it a cause of current frame time.
- Startup readiness and deterministic scene lighting were separate pending
  investigations; do not broaden this frame-time task without a user request.

## Correctness harness and safety

The existing harness is `H:\Divide-Framework\Tools\FrameworkSmokeTest.ps1`.
It builds the instrumented editor, runs `[render_smoke]` tests by default,
starts an owned/debugger-supervised process, records state and artifacts, and
enforces progress, validation and recovery checks. Read
`docs\architecture\rendering.md` and `docs\architecture\gpu-resources.md` for
the complete contract.

The GPU is an AMD Radeon RX 9070 XT, UUID
`00000000030000000000000000000000`. The two existing local roots, if retained,
are under:

```text
%USERPROFILE%\.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files\warscene-harness
%USERPROFILE%\.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files\warscene-opengl
```

These artifacts are local, not committed. The original snapshot was Vulkan
attempt 229 and OpenGL attempt 101; subsequent work advanced both. Re-read each
`state.json` and confirm the currently approved attempt limits before running.
If the artifacts or approval are unavailable, ask the user rather than
resetting state or switching roots to evade limits. Never weaken validation or
watchdogs, change Windows TDR settings, or pass either approval switch without
explicit user approval. Vulkan and OpenGL runs, GPU capture replay, and other
work on this GPU must not overlap.

On device loss, suspected GPU hang or qualifying abnormal exit, the supervisor
waits for a fresh logical device on the recorded UUID, valid submission and
completed fence before proceeding. Three same-signature device-loss repairs
without progress require user approval. Read the persisted failure, recovery
and timeline artifacts rather than bypassing a stopped state.

From `H:\Divide-Framework`, set `$vkMaxAttempts` and `$glMaxAttempts` to the
user-approved limits, then run these separately. `-Revalidate` is needed after
a prior successful run:

```powershell
$art = Join-Path $env:USERPROFILE '.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files'
$gpu = '00000000030000000000000000000000'
if (-not $vkMaxAttempts -or -not $glMaxAttempts) { throw 'Set the currently approved attempt limits first.' }

# Run these separately, never simultaneously.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Tools\FrameworkSmokeTest.ps1 `
  -Backend Vulkan -OutputRoot (Join-Path $art 'warscene-harness') `
  -Project Games -Scene WarScene -BuildJobs 2 -RenderTimeoutSeconds 900 `
  -MaxAttempts $vkMaxAttempts -Revalidate

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Tools\FrameworkSmokeTest.ps1 `
  -Backend OpenGL -OutputRoot (Join-Path $art 'warscene-opengl') `
  -ExpectedGPU $gpu -Project Games -Scene WarScene -BuildJobs 2 `
  -RenderTimeoutSeconds 900 -MaxAttempts $glMaxAttempts -Revalidate
```

Smoke mode selects the requested OpenGL or Vulkan backend in memory without
changing `XML\config.xml`; Vulkan acceptance requires core and synchronization
validation. `-SkipUnitTests` is a deliberate diagnostic deferral, not evidence
that tests passed.

For quick profiling, add `-DiagnosticPresents 200` to one command and omit
`-MinimumPresents`. A diagnostic success does **not** meet acceptance. Final
acceptance on each backend requires at least 300 successful eligible presents,
the expected GPU, `result.txt` success, `scene.png`, `editor.png`, normal
shutdown, no fatal Vulkan validation messages, and visual inspection. Known
`STUBBED` diagnostics and a shutdown networking disconnect have appeared, so
do not describe entire logs as error-free.

Inspect each `attempt-N\environment.json`, `events.log`, `result.txt`,
`debugger.log`, `logs-after\`, and the images; on failures also inspect
`failure.txt`, dumps and `Logs\errors.log` / `Logs\console.log` after exit.
`attempt-229` (Vulkan) and `attempt-101` (OpenGL) each reported 301 presents
and successful shutdown **before** the unvalidated scheduler prototype.
Attempt 229 skipped the harness's focused tests; the prior full instrumented
suite passed **353/353** after the assertion/XML changes. Those results do not
validate the prototype.

## Build and tests

Use the Visual Studio x64 developer environment and CMake/CTest in the **same
`cmd.exe` process**. The existing configuration uses
`windows-msvc-editor-instrumentation`, with build preset
`windows-msvc-debug-editor-instrumentation` and test preset
`test-windows-msvc-debug-editor-instrumentation`. For example, from PowerShell:

```powershell
& $env:ComSpec /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cd /d H:\Divide-Framework && cmake --build --preset windows-msvc-debug-editor-instrumentation --target Divide-Test-Engine --parallel 2 && ctest --preset test-windows-msvc-debug-editor-instrumentation -R "[Rr]ender.stage|Render task waits" --output-on-failure'
```

If configuration is missing, first run `cmake --preset
windows-msvc-editor-instrumentation -DBUILD_TESTING_INTERNAL=ON
-DRUN_TESTING_INTERNAL=OFF` in the same initialized developer shell. Run the
full internal CTest suite after focused checks. New engine functionality needs
Catch2 tests and current copyright headers; do not add Python/external
automated test harnesses to the repository.

## CPU profiling with Optick

Optick GUI: `H:\Optick\Optick.exe`. The instrumented editor exposes its Optick
listener on TCP port 31318. The repository provides CPU-only capture and
summary utilities:

```text
Tools\Profiling\optick_capture.py
Tools\Profiling\optick_summary.py
```

Start one supervised diagnostic run in an attached terminal. In another
terminal, wait until its `state.json` has `phase=running` and its `attempt`
points at the new attempt, then run:

```powershell
$art = Join-Path $env:USERPROFILE '.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files'
$gpu = '00000000030000000000000000000000'
$state = Get-Content (Join-Path $art 'warscene-harness\state.json') -Raw | ConvertFrom-Json
if ($state.phase -ne 'running') { throw 'Wait for the new supervised renderer.' }
$output = Join-Path $art ('vulkan-cpu-{0}.opt' -f (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
python .\Tools\Profiling\optick_capture.py --attempt $state.attempt `
  --output $output --start-after 100 --frames 32 --expected-gpu $gpu
if ($LASTEXITCODE -ne 0) { throw 'Optick capture failed; do not summarize stale data.' }
python .\Tools\Profiling\optick_summary.py $output |
  Set-Content -Encoding UTF8 ($output + '.summary.txt')
if ($LASTEXITCODE -ne 0) { throw 'Optick summary failed.' }
```

For OpenGL, use its state root and a different output filename. The capture
helper waits for presents, verifies the supervisor PID, GPU UUID and ownership
of the Optick port, and fails on a recorded renderer failure. Wait for the
supervisor to finish and inspect its outcome before using the result. Match
resolution, scene, camera, shaders, validation and cache warmth, and repeat
before/after measurements. Captures can be 750–850 MB or larger; check space.

Reference summaries are `...\files\debug-matched-gl-summary.txt` and
`debug-matched-vk-summary.txt`. Raw `debug-matched-gl.opt`,
`debug-matched-vk.opt` and `release-matched-{gl,vk}-{1,2}.opt` captures were
archived under
`Z:\Copilot-Archives\Divide-Framework\5f98b6d5-9531-4b97-b834-14eec32764db\files`.
The parser reports estimated per-frame wall-time scopes, not GPU timings, and
ETW sampling/context-switch collection was not requested.

**Optick GPU capture is not known to be safe here:** attempt 184 produced
`VUID-vkCmdWriteTimestamp-None-00830` involving query 0. Reset order, query-ring
reuse/capacity and queue synchronization also need audit. CPU-only capture is
a diagnostic workaround, not a fix. Do not suppress validation or patch only
the generated `_deps` checkout to make a GPU capture appear successful.

## Other local graphics tools

- **RenderDoc:** `C:\Program Files\RenderDoc\renderdocui.exe`,
  `qrenderdoc.exe` and `renderdoccmd.exe`. A historical Vulkan capture is
  `...\files\warscene-harness\attempt-187\renderdoc\retained.rdc` (frame 23,
  about 1.33 GB); it predates later renderer changes. The harness supports
  Vulkan-only `-RenderDocInjectExe` at its verified pre-GPU gate. The existing
  `...\files\renderdoc-war-control.py`, run with RenderDoc's embedded Python,
  verifies PID/GPU, requests a frame and retains it. See the script and
  `docs\architecture\rendering.md`; do not launch the renderer outside the
  guard. Capture-only injection skips optional Vulkan tool-property enumeration
  because this installed RenderDoc/validation combination previously corrupted
  it; validation remains on. A previous OpenGL capture attempt stalled, so do
  not remove the Vulkan-only guard merely to repeat it.
- **Agentic RenderDoc MCP:** local source
  `H:\Tools\agentic-renderdoc`, isolated Python environment
  `H:\Tools\agentic-renderdoc\.venv`, and installed RenderDoc extension under
  `%APPDATA%\qrenderdoc\extensions\agentic_renderdoc`. An MCP-capable client
  may expose `Instance`, `Eval`, `Get-Texture` and `Search-API`; discover or
  configure it using that client's own MCP settings and the local README.
  The existing stdio registration uses
  `H:\Tools\agentic-renderdoc\.venv\Scripts\python.exe` with arguments
  `-m server` and environment `AGENTIC_RENDERDOC_HOME=C:\Program Files\RenderDoc`.
  `Instance` can open/close a headless capture worker; `Eval` can inspect
  action trees, draw state and buffers; `Get-Texture` reads render targets.
  Replay queries must set the intended frame event, and each event replay can
  be expensive. The localhost extension accepts unauthenticated Python
  execution: the user chose **on demand only**, not automatic loading. Stop
  agent-owned replay workers before another GPU run. A replay timer, when
  supported, measures replayed GPU work, not the live CPU frame wall time.
- **Radeon Developer Tool Suite:**
  `H:\Tools\RadeonDeveloperToolSuite-2026-10-02-1872`. It includes
  `RadeonDeveloperPanel.exe`, `RadeonDeveloperPanelCLI.exe`,
  `RadeonGPUProfiler.exe`, `RadeonGPUAnalyzer.exe`, `RadeonMemoryVisualizer.exe`
  and `rgd.exe`; see its local `RGP_README.md` and other README files. RGP
  supports Vulkan/RX 9000 GPU profiling, but no current equivalent RGP profile
  has been established. Any new capture must respect the supervisor and GPU
  exclusivity rules. Optional crash capture uses the harness's
  `-AmdCaptureExe` pointing to `RadeonDeveloperPanelCLI.exe`.
- **Radeon GPU Detective MCP:** source/package and instructions are in
  `H:\Tools\rgd_mcp_server-1.0.0.2`. It analyzes `.rgd` crash dumps, not
  ordinary frame-time profiles. The module was importable by the system Python
  at handoff. Its README describes client-neutral stdio setup with
  `python -m rgd_mcp`, `RGD_EXE_PATH` pointing to the suite's `rgd.exe`, and
  `RGD_DOCS_PATH` pointing to the package's `docs` directory; verify whether
  the chosen client exposes it before relying on it. Do not assume another
  client's MCP configuration is inherited.
- **WinDbg/CDB and Vulkan validation:** the supervisor automatically provisions
  its debugger, records `debugger.log` and failure dumps, and retains Vulkan
  core/synchronization validation. Use those artifacts and the engine logs
  before retrying after a crash. Vulkan SDK observed at
  `C:\VulkanSDK\1.4.363.0`.

### Optional guarded RenderDoc capture

Only when a new Vulkan capture is needed, use the same guarded root and add
`-RenderDocInjectExe`. For example:

```powershell
$art = Join-Path $env:USERPROFILE '.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files'
if (-not $vkMaxAttempts) { throw 'Set the currently approved Vulkan attempt limit first.' }
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Tools\FrameworkSmokeTest.ps1 `
  -Backend Vulkan -OutputRoot (Join-Path $art 'warscene-harness') `
  -Project Games -Scene WarScene -BuildJobs 2 -RenderTimeoutSeconds 900 `
  -MaxAttempts $vkMaxAttempts -Revalidate -DiagnosticPresents 200 `
  -RenderDocInjectExe 'C:\Program Files\RenderDoc\renderdoccmd.exe'
```

In another terminal, wait until the **new** attempt has reached `phase=running`,
then start the existing controller. It waits for the gated target file, verifies
the renderer PID and GPU, triggers one capture after the requested eligible
present, and records the outcome. It does not attach to an arbitrary process.

```powershell
$art = Join-Path $env:USERPROFILE '.copilot\session-state\5f98b6d5-9531-4b97-b834-14eec32764db\files'
$gpu = '00000000030000000000000000000000'
$state = Get-Content (Join-Path $art 'warscene-harness\state.json') -Raw | ConvertFrom-Json
if ($state.phase -ne 'running') { throw 'Wait for the new supervised renderer.' }
$env:DIVIDE_RENDERDOC_ATTEMPT = $state.attempt
$env:DIVIDE_RENDERDOC_GPU = $gpu
$env:DIVIDE_RENDERDOC_PRES = '20'
$env:DIVIDE_RENDERDOC_STATUS = Join-Path $state.attempt 'renderdoc-control.log'
& 'C:\Program Files\RenderDoc\qrenderdoc.exe' --python (Join-Path $art 'renderdoc-war-control.py')
if ($LASTEXITCODE -ne 0) { throw 'RenderDoc capture failed; inspect the controller log.' }
```

Wait for the supervisor to exit, review its result and the controller log, and
check `attempt-N\renderdoc\capture.json` before opening `retained.rdc`. The
capture and any replay remain local and may contain sensitive application data.
Close only agent-owned replay workers before another GPU run.

## Original suggested continuation (historical)

The scheduler was implemented and measured after this initial handoff.
Follow the current continuation below rather than repeating the prototype
investigation.

## Continuation update

The living implementation plan is
[`docs/plans/renderer-performance-plan.md`](docs/plans/renderer-performance-plan.md).
The reference workload remains WarScene on the RX 9070 XT and Ryzen 9950X3D,
with the saved camera and rendering features enabled. The Debug target is
at most 33 ms and the Release target is below 16.6 ms; neither target has
yet been demonstrated. Comparisons below are CPU frame medians in instrumented
Release perf mode with API validation disabled unless labelled GPU. Separate
captures vary; differences should not be added together.

### Changes and observed effects, in order

| Step | Change | Observed effect |
|---|---|---|
| 1 | Added opt-in perf mode, Optick summaries, per-stage scopes, GPU timestamps and workload counters. | Established matched CPU/GPU measurements; instrumentation itself is not a claimed speedup. |
| 2 | Changed shared task completion wakeups from `notify_one` to `notify_all`. | Vulkan Release 121 → 54 ms; Debug 217 → 131 ms in the recorded runs. |
| 3 | Compacted visible tree instances on the GPU and patched indirect instance counts. | Vulkan Release about 53 → 41 ms; OpenGL 47.4 → 43.6 ms. |
| 4 | Fixed OpenGL buffer-binding aliasing and query-ring/statistics overhead. | Restored Hi-Z/tree-compaction correctness; GL pipeline-statistics queries became opt-in. The 43.6 ms GL measurement is from the corrected render. Storage and uniform buffers use separate namespaces, storage slots are unique across stages, and GL's flat-slot limit is not applied to Vulkan's per-stage limit. |
| 5 | Tested a node-update bitset and 12 instead of 6 render workers. | Bitset was within noise and was reverted because node indices can exceed its fixed capacity; 12 workers regressed and was also reverted. |
| 6 | Separated CPU recording dependencies from GPU submission order and patched shadow indices after recording. | Vulkan Release 41.3 → 31.4 ms; OpenGL 43.6 → 33.5 ms; Vulkan Debug 133.8 → 112.1 ms. |
| 7 | Allocate render-queue/prepare parent tasks only when needed. | Fixed pooled-task slot exhaustion; no measurable short-run frame-time gain. |
| 8 | Matched Vulkan's MSAA shading frequency to OpenGL's per-pixel behavior, as requested. | Vulkan GPU 31.3 → 21.8 ms; Release frame time stayed about 32 ms and became CPU-bound. Sample-qualified shaders still execute per sample. |
| 9 | Profiled per-draw uniform churn and tested fast push-constant indices. | About 10k buffer writes/frame were attributed to uniform commits. The experiment broke terrain/vegetation on both APIs and was reverted, so no speedup is claimed. |

WarScene point lights move and have random colours; compare geometry, shadows,
culling and validation, not exact colour pixels. Earlier validated+CDB numbers
are not directly comparable to the later validation-off perf-mode numbers.

### Stabilization measurements

After reverting the push-constant experiment and unsafe fixed-size tracking
and completing the OpenGL binding adjustment, 600-present Release diagnostics
retained the reference terrain, forest, grass, building, water and shadows.
For 64 captured frames after roughly 160 presents, CPU median/p95 was
**34.35/41.36 ms on Vulkan** and **34.58/38.00 ms on OpenGL**. The average
sum of timed GPU command-buffer scopes over 64 frames was 20.43 ms and
25.05 ms, respectively; this is not a full GPU-frame measurement. Both reported
1002 HiZ-culled items, 58 overflow and 3 skipped. These are validation-off,
instrumented captures; compare them cautiously with earlier runs.
The final Debug build and full internal suite passed 365/365, including MSAA
defaults, binding-namespace and instance-count patch shader coverage. The
<16.6 ms Release and ≤33 ms Debug goals remain unmet.

Validation-on WarScene acceptance completed 301 presents and exited normally
on OpenGL (attempt 127) and Vulkan (attempt 270). The scene and editor captures
show the reference geometry, vegetation, water and shadows on both APIs.
Vulkan recorded approved custom-border best-practice advisories; the logs also
contain existing `STUBBED` messages and shutdown networking disconnects.

One earlier Vulkan run reported Windows heap corruption `0xc0000374` during
shutdown. A subsequent longer run exited cleanly after the fixed bitset was
reverted, but no root cause has been established; investigate a recurrence
with debugger evidence rather than attributing it to a GPU fault or claiming
the bitset was the cause.

### Next steps

1. Rebaseline on the current branch before further work; keep CPU and GPU
   timings separate and run GPU captures sequentially. Investigate the
   node-update set only with a growable, tested design if profiling justifies it.
2. Reduce the remaining per-draw uniform/descriptor cost using a design with
   explicit instance and pass semantics. The reverted `PushData1[3]` approach
   must not be reintroduced without finding its failure and testing both APIs.
3. Profile shadow recording and consider splitting it before attempting early
   Vulkan translation. Investigate geometry pooling, multi-draw and texture
   arrays only with explicit allocation, lifetime and fallback rules.
4. Investigate the existing per-stage light-buffer overwrite across multiple
   passes and verify the intended `getLoDLevel(DISPLAY)` semantics. Do not
   describe an unverified data race as established.
5. Continue toward the original Debug and Release targets; retain the current
   visuals by default, including OpenGL-matched MSAA shading, and use the plan's
   tests and validation gates.
