# Editor windows

<!-- Copyright (c) 2026 DIVIDE-Studio -->

See the [index](index.md) for debugging routes and note-maintenance guidance.

## Scope and ownership

- `Source/Editor/Editor.cpp` owns the ImGui contexts, creates the docked
  windows and renders the editor dockspace.
- `Source/Editor/Widgets/DockedWindow.cpp` opens each window before invoking
  its content; `NodePreviewWindow` and `SceneViewWindow` provide the preview
  and scene tabs.
- `Source/Editor/Widgets/DockedWindows/PropertyWindow.cpp`,
  `Source/Editor/Headers/Utils.inl`, and `Source/Editor/UndoManager.cpp`
  handle property editing and undo/redo.
- `Source/Editor/Widgets/DockedWindows/OutputWindow.cpp` receives console
  entries through `OutputLogQueue`.
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

## Property edits and undo

`EditorComponent` fields carry their owner GUID and an undo/redo notification.
For fields using that notification, `PropertyEditState` retains the value at
each ImGui item's activation and records a changed edit when that item
deactivates. A reset is a separate action, including when it follows an active
edit. Scalar, compound, matrix and colour field paths must preserve the whole
original value and the field's setter and notification; unchanged edits do not
enter the history.

`UndoManager` bounds its history, clears redo when a new edit is registered,
and invokes the field notification after applying an undo or redo.
`Editor::onRemoveComponent` removes that component's entries from both stacks,
so their callbacks do not outlive the component. Spotlight field notifications
also restore the inner/outer angle invariant and invalidate shadow state.
Other editor widgets use their existing undo paths; do not assume every widget
uses `PropertyEditState`.

Dropdown fields explicitly typed as `UINT`/`DWORD` persist their index through
`EditorComponent` XML serialization; untyped legacy dropdowns remain unchanged.
Missing fields retain their current/default value. Out-of-range indices report
an error without replacing the value, as does malformed numeric text.
`RenderingComponent` uses this for **World AO
Participation**, preserving Automatic rather than serializing its resolved
boolean. Its setter is also used by undo/redo. See [Rendering](rendering.md) for
the per-renderable-node draw-mask and cache-invalidation contract.

## Gizmo and console updates

Spatial-change notifications may originate from scene workers.
`GizmoSelectionRefresh` coalesces them, and `Editor::update` refreshes the
selection on the main thread once the gizmo is no longer being manipulated.
The pending refresh is retained during manipulation.

The output window's console callback queues complete entries in a bounded
`OutputLogQueue`; the UI thread drains them into its display ring. Overflow
retains the newest entries. Console callback locking and lifetime constraints
are described in [Threading](threading.md#entry-points-and-task-pools).
