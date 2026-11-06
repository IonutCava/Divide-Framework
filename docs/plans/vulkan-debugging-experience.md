# Debugging Divide's Vulkan renderer with an agent

<!-- Copyright (c) 2026 DIVIDE-Studio -->

This is an untracked working account for a potential X post, not a claim that
the renderer is fully validated. The facts below describe a local Windows run
on an AMD Radeon RX 9070 XT using the MSVC Debug editor build.

## The problem

Divide's Vulkan renderer produced many validation messages and could crash the
GPU driver. Existing OpenGL behavior was not sufficient evidence that the
Vulkan image-layout and synchronization assumptions were correct. The initial
logs already showed depth and colour MSAA resolve images being used as
attachments while tracked in a read-only layout.

The goal was to get the Default project and editor running, render 300 eligible
scene frames, capture the output, and shut down cleanly without unapproved
Vulkan validation warnings or errors. A present count alone was not considered
proof that the image was correct.

## How the plan came together

The developer asked for a reviewable plan and an interview before implementation.
Together we defined the success criteria, the debugger and evidence to retain,
the permitted changes, and safety limits:

- Use the Windows/MSVC editor build with Optick and function profiling enabled.
- Fail on the first Vulkan warning or error, except one explicitly approved
  best-practices advisory for `VK_EXT_custom_border_color`. The advisory remained
  visible in the logs; errors and other warnings were not suppressed. Ordinary
  engine TODO/STUBBED messages were not Vulkan validation failures.
- Keep each attempt's logs, source state, executable identity and diagnostics.
  Build failures did not consume the 25-iteration repair budget.
- After a device loss or suspected GPU hang, retain evidence and wait for a fresh
  logical device on the same GPU to submit work and complete a fence. Pause for
  approval after three repeated device-loss repairs without presentation progress.
- Do not deliberately induce a GPU reset, change Windows TDR settings, or upload
  crash dumps. Existing local Vulkan changes could be revised; unrelated user
  changes were preserved.

The approved plan is in `docs/plans/vulkan-test-fix-plan.md`. The loop is
**agent-driven**, not an unattended self-rewriting program: the PowerShell
runner builds and supervises an attempt, while the coding agent examines the
evidence, changes code, and invokes the next attempt.

## Tools used

- **CMake, Ninja, MSVC and the existing instrumentation presets** built the
  editor incrementally; build parallelism was adjusted for the developer's
  hardware.
- **Khronos Vulkan validation**, including synchronization and best-practices
  checks, identified the first offending message. The engine's smoke mode
  flushed the details before stopping, rather than allowing a cascade of errors.
- **WinDbg's CDB** captured the breakpoint, all-thread stacks and a local
  minidump. Engine render/debug-scope labels helped connect failures to the
  rendering work being replayed.
- **AMD Radeon Developer Panel and Radeon GPU Detective** captured and analyzed
  a real GPU crash. Its marker tree placed a vertex-shader wave in a reflection
  cubemap prepass and identified a suspected page-faulting load.
- **Windows process jobs and a dedicated Vulkan recovery probe** bounded owned
  processes and checked the same GPU's ability to accept new work after a reset.
  The engine's console and error logs were archived around each launch.
- **Catch2** covered smoke-mode policy and renderer invariants after the first
  successful smoke run. RenderDoc and the RGD MCP server were installed and
  checked as available tools; this account does not claim that a RenderDoc
  capture or an MCP analysis was used to establish the result.

After the runs, no Radeon developer/capture or debugger process remained, and
no RenderDoc capture opt-in was set in the process, user or machine
environment. The existing RenderDoc layer registration and installed tools
were left in place. The runner did not request persistent AMD clock or driver
experiments; hidden driver state could not be independently verified.

## What changed

The evidence pointed to several independent issues rather than one obvious
single-line fix:

1. The render-target and texture code was corrected to track resolve-image
   usage through attachment writes and subsequent shader reads. Dynamic
   rendering attachment state is reset between passes, and the previous-screen
   blit no longer relies on a transition that bypasses the tracked state.
2. Sampleable render targets are initialized and cleared before their first
   shader-read use, instead of transitioning undefined contents directly into
   a read-only layout. Small images can be suballocated by VMA rather than
   being forced into dedicated allocations.
3. Frame-in-flight, per-node ring/dirty-range, submission and descriptor error
   paths were tightened. Stale per-node ring selection was a strong suspect
   after the RGD report, but the report did **not** prove the exact GPU-side root
   cause of that page fault. It would be inaccurate to present the ring change
   as a proven explanation for every driver crash.
4. Once a run rendered 300 frames, the debugger exposed a separate shutdown
   assertion: a Vulkan shader program was being unloaded twice. Resource
   cleanup was corrected before the run was accepted as successful.
5. To capture the complete main editor framebuffer, the Vulkan smoke path
   reads the swapchain after ImGui, waits for GPU completion and saves a PNG.
   The first version exposed its own invalid presentation-to-transfer barrier;
   validation caught it, and the capture was changed to transition directly
   from the final colour attachment to the transfer source.

The debugger itself also needed hardening. The packaged CDB initially escaped
the supervisor's Windows job, so a failed capture left the renderer running
until its timeout. A locally copied debugger runtime was verified with a
controlled child-process preflight, and the real renderer now waits at a
pre-GPU gate until its process identity and job membership are checked.

## Observed result and limits

- Scene-only attempts 11 and 12 completed 300 eligible Vulkan presents and
  exited cleanly. Attempt 12 also passed the focused smoke tests.
- After adding the editor capture and fixing its barrier, attempt 15 completed
  **301 eligible presents**, captured both scene and main-editor images, and
  exited cleanly. No unapproved Vulkan warning or error was recorded. Its
  focused Catch2 run passed 728 assertions in 25 test cases.
- After adding the OpenGL comparison path, Vulkan attempt 16 again completed
  301 eligible presents, captured both images and exited cleanly without
  unapproved Vulkan warnings or errors. Its focused Catch2 run passed
  750 assertions in 27 test cases.
- The image showed the editor, sky and grid. The Default scene was not a
  geometry-rich stress test. One successful configuration on one GPU does not
  establish correctness on other GPUs, operating systems, scenes, or drivers.
- A matching OpenGL run used the same GPU UUID and completed 301 eligible
  presents with a clean exit and both images. Both main-editor captures are
  3840 x 2071; their layouts, sky and grid look broadly similar, but the
  differing timing and UI logs make this a visual comparison, not a proof of
  pixel or rendering parity.
- The main framebuffer images include ImGui but not operating-system window
  decorations or detached editor windows. The scene captures are 1920 x 1080.
- The complete Windows/MSVC Debug editor instrumentation build succeeded, and
  the separate full internal CTest run passed **150 of 150 tests**. These
  results do not establish success on other platforms or in CI.

## Scene-loading coverage

The smoke runner can target a named project and scene, allowing scene-specific
validation beyond the Default project runs described above. For example,
`-Project Games -Scene WarScene` selects `Projects/Games/Scenes/WarScene.xml`
and its `assets.xml` node outline. See the
[scene-loading guide](../architecture/scene-loading.md)
for the selection, resource creation and serialized node-data path. A passing
Default-scene run does not establish that WarScene loads or renders correctly;
that requires a separate run and visual inspection of its capture.

Attempt-specific logs and dumps are local session artifacts, not part of this
document. Review them for sensitive paths or application data before sharing.
