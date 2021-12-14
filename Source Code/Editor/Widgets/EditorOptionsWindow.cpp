#include "stdafx.h"

#include "Headers/UndoManager.h"

#include "Editor/Headers/Editor.h"
#include "Headers/EditorOptionsWindow.h"
#include "Core/Headers/PlatformContext.h"

#include <IconFontCppHeaders/IconsForkAwesome.h>

namespace Divide {
    EditorOptionsWindow::EditorOptionsWindow(PlatformContext& context)
        : PlatformContextComponent(context),
          _fileOpenDialog(false, true)
    {
        //ImGuiFs::Dialog::ExtraWindowFlags |= ImGuiWindowFlags_Tooltip;
    }

    void EditorOptionsWindow::update([[maybe_unused]] const U64 deltaTimeUS) {
    }

    void EditorOptionsWindow::draw(bool& open) {
        if (!open) {
            return;
        }
        const ImVec2 CenterPos(ImGui::GetMainViewport()->Pos.x + ImGui::GetIO().DisplaySize.x * 0.5f,
                               ImGui::GetMainViewport()->Pos.y + ImGui::GetIO().DisplaySize.y * 0.5f);
        const ImVec2 FileDialogPos(ImGui::GetMainViewport()->Pos.x + 20, 
                                   ImGui::GetMainViewport()->Pos.y + 20);

        ImGui::SetNextWindowPos(CenterPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        U32 flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
        if (!_openDialog) {
            flags |= ImGuiWindowFlags_Tooltip;
        } else {
            flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        }

        if (!ImGui::Begin("Editor options", nullptr, flags))
        {
            ImGui::End();
            return;
        }
        
        static F32 axisWidth = _context.editor().infiniteGridAxisWidth(), gridLineWidth = _context.editor().infiniteGridScale();
        ImGui::Text(ICON_FK_PLUS_SQUARE_O);
        ImGui::SameLine();
        if (ImGui::SliderFloat("Grid axis width", &axisWidth, 0.01f, 10.0f, "%.3f")) {
            _context.editor().infiniteGridAxisWidth(axisWidth);
        }
        ImGui::SameLine();
        if (ImGui::SliderFloat("Grid scale", &gridLineWidth, 0.01f, 10.0f, "%.3f")) {
            _context.editor().infiniteGridScale(gridLineWidth);
        }
        static UndoEntry<I32> undo = {};
        const I32 crtThemeIdx = to_I32(Attorney::EditorOptionsWindow::getTheme(_context.editor()));
        I32 selection = crtThemeIdx;
        if (ImGui::Combo("Editor Theme", &selection, ImGui::GetDefaultStyleNames(), ImGuiStyle_Count)) {

            ImGui::ResetStyle(static_cast<ImGuiStyleEnum>(selection));
            Attorney::EditorOptionsWindow::setTheme(_context.editor(), static_cast<ImGuiStyleEnum>(selection));

            undo._type = GFX::PushConstantType::INT;
            undo._name = "Theme Selection";
            undo._oldVal = crtThemeIdx;
            undo._newVal = selection;
            undo._dataSetter = [this](const I32& data) {
                const ImGuiStyleEnum style = static_cast<ImGuiStyleEnum>(data);
                ImGui::ResetStyle(style);
                Attorney::EditorOptionsWindow::setTheme(_context.editor(), style);
            };
            _context.editor().registerUndoEntry(undo);
            ++_changeCount;
        }

        ImGui::Separator();

        string externalTextEditorPath = Attorney::EditorOptionsWindow::externalTextEditorPath(_context.editor());
        ImGui::InputText("Text Editor", externalTextEditorPath.data(), externalTextEditorPath.size(), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        const bool openDialog = ImGui::Button("Select");
        if (openDialog) {
            ImGuiFs::Dialog::WindowLTRBOffsets.x = FileDialogPos.x;
            ImGuiFs::Dialog::WindowLTRBOffsets.y = FileDialogPos.y;
            _openDialog = true;
        }
        ImGui::Separator();

        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            open = false;
            assert(_changeCount < _context.editor().UndoStackSize());
            for (U16 i = 0; i < _changeCount; ++i) {
                if (!_context.editor().Undo()) {
                    NOP();
                }
            }
            _changeCount = 0u;
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            open = false;
            _changeCount = 0u;
            if (!_context.editor().saveToXML()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "Save failed!", Time::SecondsToMilliseconds<F32>(3.0f), true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Defaults", ImVec2(120, 0))) {
            _changeCount = 0u;
            Attorney::EditorOptionsWindow::setTheme(_context.editor(), ImGuiStyle_DarkCodz01);
            ImGui::ResetStyle(ImGuiStyle_DarkCodz01);
        }
        ImGui::End();

        if (_openDialog) {
            const char* chosenPath = _fileOpenDialog.chooseFileDialog(openDialog, nullptr, nullptr, "Choose text editor", ImVec2(-1, -1), FileDialogPos);
            if (strlen(chosenPath) > 0) {
                Attorney::EditorOptionsWindow::externalTextEditorPath(_context.editor(), chosenPath);
            }
            if (_fileOpenDialog.hasUserJustCancelledDialog()) {
                _openDialog = false;
            }
        }
    }
}