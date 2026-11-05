# Editor windows

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Scope and ownership

- `Source/Editor/Editor.cpp` owns the ImGui contexts, creates the docked
  windows and renders the editor dockspace.
- `Source/Editor/Widgets/DockedWindow.cpp` opens each window before invoking
  its content; `NodePreviewWindow` and `SceneViewWindow` provide the preview
  and scene tabs.
- ImGui persists window positions and docking in `imgui.ini`. The smoke runner
  reads this layout but disables saving it so diagnostics do not rewrite it.

## Docking invariants

Scene View uses `SceneViewWindow::WINDOW_NAME` with the stable ImGui
`###AnimatedTitlePlayState` identity. Its play/pause icon is part of the
visible title, not the identity; using `##` instead creates separate windows
and can detach the tab when playback changes. The supplied layout places
Scene View and Node Preview in the same dock node. `DockedWindow` queues Scene
View's initial tab selection after its dock tab is registered, then stops
requesting selection so later user choices are preserved. Window focus alone
does not reliably override the saved selected tab. Respect saved user layouts:
profiles without a saved dock node may need to dock the windows once.

Scene View displays the composited game frame, including the in-game GUI; see
the [GPU resource colour-space convention](gpu-resources.md#colour-space-convention)
for the render target and sampling contract.

The scene and node-preview cameras are separate; see
[Scene loading](scene-loading.md) for the distinction between paused editor
and Play-mode camera selection.
