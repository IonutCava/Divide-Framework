/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef _DIVIDE_EDITOR_INL_
#define _DIVIDE_EDITOR_INL_

namespace Divide {

inline bool Editor::isInit() const noexcept {
    return _mainWindow != nullptr;
}

inline bool Editor::inEditMode() const noexcept {
    return running() && simulationPaused();
}

inline void Editor::setTransformSettings(const TransformSettings& settings) const noexcept {
    Attorney::GizmoEditor::setTransformSettings(_gizmo.get(), settings);
}

inline const TransformSettings& Editor::getTransformSettings() const noexcept {
    return Attorney::GizmoEditor::getTransformSettings(_gizmo.get());
}

 U32 Editor::stepQueue() const noexcept {
     return _stepQueue;
}

inline bool Editor::simulationPaused() const noexcept {
    return _simulationPaused;
}

template<typename T>
void Editor::registerUndoEntry(const UndoEntry<T>& entry) {
    _undoManager->registerUndoEntry(entry);
}

inline size_t Editor::UndoStackSize() const noexcept {
    return _undoManager->UndoStackSize();
}

inline size_t Editor::RedoStackSize() const noexcept {
    return _undoManager->RedoStackSize();
}

inline void Editor::toggleMemoryEditor(const bool state) noexcept {
    _showMemoryEditor = state;
}

inline const RenderTargetHandle& Editor::getEditorTarget() const noexcept {
    return _editorRTHandle;
}

inline const RenderTargetHandle& Editor::getNodePreviewTarget() const noexcept {
    return _nodePreviewRTHandle;
}

} //namespace Divide

#endif //_DIVIDE_EDITOR_INL_