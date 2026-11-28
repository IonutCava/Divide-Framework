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
- ImGui persists window positions and docking in `imgui.ini`.

## Teardown without a rendering API

Headless contexts may create the editor without initializing a rendering API.
Release rendering resources only when initialization acquired them, and do not
call resource-pool accessors that require a live graphics device during
headless teardown.

## Docking invariants

Keep ImGui window identities stable and separate from any changing title text.
Respect saved docking and selection state; initialization should not keep
overriding later user choices.

Scene View displays the composited game frame, including the in-game GUI; see
the [GPU resource colour-space convention](gpu-resources.md#colour-space-convention)
for the render target and sampling contract.

The scene and node-preview cameras are separate; see
[Scene loading](scene-loading.md) for the distinction between paused editor
and Play-mode camera selection.

## Property edits and undo

`EditorComponent` fields carry their owner GUID and an undo/redo notification.
`PropertyEditState` records one edit from activation through deactivation.
Reset actions remain separate, and unchanged values do not enter history.

`UndoManager` bounds its history, clears redo when a new edit is registered,
and invokes field notifications after applying an undo or redo. Removing a
component removes its callbacks from both stacks. Preserve the field's full
value, setter, and notifications when adding undo support.

## Gizmo and console updates

Spatial-change notifications may originate from scene workers.
`GizmoSelectionRefresh` coalesces them, and `Editor::update` refreshes the
selection on the main thread once the gizmo is no longer being manipulated.
The pending refresh is retained during manipulation.

The output window's console callback queues complete entries in a bounded
`OutputLogQueue`; the UI thread drains them into its display ring. Overflow
retains the newest entries. Console callback locking and lifetime constraints are described in
[Threading](threading.md#console-output).
