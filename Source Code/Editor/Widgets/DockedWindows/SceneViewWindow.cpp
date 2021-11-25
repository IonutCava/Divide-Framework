#include "stdafx.h"

#include "Headers/SceneViewWindow.h"

#include "Editor/Headers/Editor.h"

#include "Editor/Widgets/Headers/ImGuiExtensions.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include <imgui_internal.h>

namespace Divide {

    SceneViewWindow::SceneViewWindow(Editor& parent, const Descriptor& descriptor)
        : DockedWindow(parent, descriptor)
    {
    }

    void SceneViewWindow::drawInternal() {

        const auto button = [](const bool disabled, const char* label, const char* tooltip, const bool small = false) -> bool {
            if (disabled) {
                PushReadOnly();
            }
            const bool ret = small ? ImGui::SmallButton(label) : ImGui::Button(label);

            if (disabled) {
                PopReadOnly();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(tooltip);
            }

            return ret;
        };

        bool play = !_parent.simulationPauseRequested();
        ImGui::Text("Play:"); ImGui::SameLine(); ImGui::ToggleButton("Play", &play);
        if (play) {
            Attorney::EditorSceneViewWindow::editorStepQueue(_parent, 2);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle scene playback");
        }
        ImGui::SameLine();
        const bool enableStepButtons = !play;
        if (button(!enableStepButtons,
                   ">|",
                    "When playback is paused, advanced the simulation by 1 full frame"))
        {
            Attorney::EditorSceneViewWindow::editorStepQueue(_parent, 2);
        }

        ImGui::SameLine();

        if (button(!enableStepButtons,
                   ">>|",
                   Util::StringFormat("When playback is paused, advanced the simulation by %d full frame", Config::TARGET_FRAME_RATE).c_str()))
        {
            Attorney::EditorSceneViewWindow::editorStepQueue(_parent, Config::TARGET_FRAME_RATE + 1);
        }

        ImGui::SameLine();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::SameLine();
        bool autoFocusEditor = Attorney::EditorSceneViewWindow::autoFocusEditor(_parent);
        ImGui::Text("Auto focus:"); ImGui::SameLine(); ImGui::ToggleButton("Auto Focus Editor", &autoFocusEditor);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If off, the first click outside of the scene view will act as a \"focus\" click. (i.e. not be passed down to editor widgets.)");
        }
        Attorney::EditorSceneViewWindow::autoFocusEditor(_parent, autoFocusEditor);
        ImGui::SameLine();

        bool emissiveSelections = Attorney::EditorSceneViewWindow::emissiveSelections(_parent);
        ImGui::Text("Emissive Selections:"); ImGui::SameLine(); ImGui::ToggleButton("Emissive Selections", &emissiveSelections);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If on, selected scene nodes will have an emissive component attached to them for easier navigation.\nDisable if editing materials!");
        }
        Attorney::EditorSceneViewWindow::emissiveSelections(_parent, emissiveSelections);

        bool enableGizmo = Attorney::EditorSceneViewWindow::editorEnabledGizmo(_parent);
        TransformSettings settings = _parent.getTransformSettings();

        const F32 ItemSpacing = ImGui::GetStyle().ItemSpacing.x;
        static F32 TButtonWidth = 10.0f;
        static F32 RButtonWidth = 10.0f;
        static F32 SButtonWidth = 10.0f;
        static F32 NButtonWidth = 10.0f;
        static F32 XButtonWidth = 10.0f;
        static F32 YButtonWidth = 10.0f;
        static F32 ZButtonWidth = 10.0f;
        static F32 AButtonWidth = 10.0f;

        ImGui::SameLine(window->ContentSize.x / 2);

        if (button(false, "[_]<|", "Copy the player's camera snapshot to the editor camera"))
        {
            Attorney::EditorSceneViewWindow::copyPlayerCamToEditorCam(_parent);
        }

        F32 pos = SButtonWidth + ItemSpacing + 25;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::SCALE || settings.currentGizmoOperation == ImGuizmo::SCALE_X || settings.currentGizmoOperation == ImGuizmo::SCALE_Y || settings.currentGizmoOperation == ImGuizmo::SCALE_Z, 
                    "S",
                    "Scale",
                    true))
        {
            switch (settings.previousAxisSelected[2]) {
                case 0u: settings.currentGizmoOperation = ImGuizmo::SCALE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::SCALE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::SCALE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::SCALE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[2];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }
        SButtonWidth = ImGui::GetItemRectSize().x;

        pos += RButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::ROTATE || settings.currentGizmoOperation == ImGuizmo::ROTATE_X || settings.currentGizmoOperation == ImGuizmo::ROTATE_Y || settings.currentGizmoOperation == ImGuizmo::ROTATE_Z,
                    "R",
                    "Rotate",
                    true))
        {
            switch (settings.previousAxisSelected[1]) {
                case 0u: settings.currentGizmoOperation = ImGuizmo::ROTATE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::ROTATE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::ROTATE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::ROTATE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[1];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }
        RButtonWidth = ImGui::GetItemRectSize().x;

        pos += TButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::TRANSLATE || settings.currentGizmoOperation == ImGuizmo::TRANSLATE_X || settings.currentGizmoOperation == ImGuizmo::TRANSLATE_Y || settings.currentGizmoOperation == ImGuizmo::TRANSLATE_Z,
                    "T",
                    "Translate",
                    true))
        {
            switch (settings.previousAxisSelected[0]) {
                case 0u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[0];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }
        TButtonWidth = ImGui::GetItemRectSize().x;

        pos += NButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(false, "N", "Select", true)) {
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, false);
            settings.currentAxisSelected = 0u;
        }
        NButtonWidth = ImGui::GetItemRectSize().x;
       
        const RenderTarget& rt = _parent.context().gfx().renderTargetPool().renderTarget(RenderTargetID(RenderTargetUsage::EDITOR));
        const Texture_ptr& gameView = rt.getAttachment(RTAttachmentType::Colour, 0).texture();

        const I32 w = to_I32(gameView->width());
        const I32 h = to_I32(gameView->height());

        const ImVec2 curPos = ImGui::GetCursorPos();
        const ImVec2 wndSz(ImGui::GetWindowSize().x - curPos.x - 30.0f, ImGui::GetWindowSize().y - curPos.y - 30.0f);

        const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + wndSz.x, window->DC.CursorPos.y + wndSz.y));
        ImGui::ItemSize(bb);
        if (ImGui::ItemAdd(bb, NULL)) {

            ImVec2 imageSz = wndSz - ImVec2(0.2f, 0.2f);
            ImVec2 remainingWndSize(0, 0);
            const F32 aspectRatio = w / to_F32(h);

            const F32 wndAspectRatio = wndSz.x / wndSz.y;
            if (aspectRatio >= wndAspectRatio) {
                imageSz.y = imageSz.x / aspectRatio;
                remainingWndSize.y = wndSz.y - imageSz.y;
            } else {
                imageSz.x = imageSz.y*aspectRatio;
                remainingWndSize.x = wndSz.x - imageSz.x;
            }

            const ImVec2 uvExtension = ImVec2(1.f, 1.f);
            if (remainingWndSize.x > 0) {
                const F32 remainingSizeInUVSpace = remainingWndSize.x / imageSz.x;
                const F32 deltaUV = uvExtension.x;
                const F32 remainingUV = 1.f - deltaUV;
                if (deltaUV < 1) {
                    const F32 adder = remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace;
                    remainingWndSize.x -= adder * imageSz.x;
                    imageSz.x += adder * imageSz.x;
                }
            }
            if (remainingWndSize.y > 0) {
                const F32 remainingSizeInUVSpace = remainingWndSize.y / imageSz.y;
                const F32 deltaUV = uvExtension.y;
                const F32 remainingUV = 1.f - deltaUV;
                if (deltaUV < 1) {
                    const F32 adder = remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace;
                    remainingWndSize.y -= adder * imageSz.y;
                    imageSz.y += adder * imageSz.y;
                }
            }

            ImVec2 startPos = bb.Min, endPos;
            startPos.x += remainingWndSize.x*.5f;
            startPos.y += remainingWndSize.y*.5f;
            endPos.x = startPos.x + imageSz.x;
            endPos.y = startPos.y + imageSz.y;
            window->DrawList->AddImage((void *)(intptr_t)gameView->data()._textureHandle, startPos, endPos, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            
            const DisplayWindow* displayWindow = static_cast<DisplayWindow*>(window->Viewport->PlatformHandle);
            // We might be dragging the window
            if (displayWindow != nullptr) {
                _windowOffset = displayWindow->getPosition();
                _sceneRect.set(startPos.x, startPos.y, imageSz.x, imageSz.y);
            }
        }
        

        if (ImGui::RadioButton("Local", settings.currentGizmoMode == ImGuizmo::LOCAL)) {
            settings.currentGizmoMode = ImGuizmo::LOCAL;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("World", settings.currentGizmoMode == ImGuizmo::WORLD)) {
            settings.currentGizmoMode = ImGuizmo::WORLD;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &settings.useSnap);
        if (settings.useSnap) {
            ImGui::SameLine();
            ImGui::Text("Step:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150);
            switch (settings.currentGizmoOperation)
            {
                case ImGuizmo::TRANSLATE:
                case ImGuizmo::TRANSLATE_X:
                case ImGuizmo::TRANSLATE_Y:
                case ImGuizmo::TRANSLATE_Z:
                    ImGui::InputFloat3("Position", &settings.snap[0]);
                    break;
                case ImGuizmo::ROTATE:
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    ImGui::InputFloat("Angle", &settings.snap[0]);
                    break;
                case ImGuizmo::SCALE:
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    ImGui::InputFloat("Scale", &settings.snap[0]);
                    break;
                case ImGuizmo::BOUNDS: break;
            }
            ImGui::PopItemWidth();
        }

        ImGui::SameLine(0.f, 25.0f);
        ImGui::Text("Gizmo Axis [ ");
        enableGizmo = Attorney::EditorSceneViewWindow::editorEnabledGizmo(_parent);
        ImGui::SameLine();
        if (button(!enableGizmo || settings.currentAxisSelected == 1u, "X", "X Axis Only", true)) {
            settings.currentAxisSelected = 1u;

            switch (settings.currentGizmoOperation) {
                case ImGuizmo::TRANSLATE: 
                case ImGuizmo::TRANSLATE_X: 
                case ImGuizmo::TRANSLATE_Y: 
                case ImGuizmo::TRANSLATE_Z: 
                    settings.currentGizmoOperation = ImGuizmo::TRANSLATE_X;
                    settings.previousAxisSelected[0] = 1u;
                    break;
                case ImGuizmo::ROTATE:
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    settings.currentGizmoOperation = ImGuizmo::ROTATE_X;
                    settings.previousAxisSelected[1] = 1u;
                    break;
                case ImGuizmo::SCALE:
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    settings.currentGizmoOperation = ImGuizmo::SCALE_X;
                    settings.previousAxisSelected[2] = 1u;
                    break;
            };
        }

        ImGui::SameLine();
        if (button(!enableGizmo || settings.currentAxisSelected == 2u, "Y", "Y Axis Only", true)) {
            settings.currentAxisSelected = 2u;

            switch (settings.currentGizmoOperation) {
                case ImGuizmo::TRANSLATE: 
                case ImGuizmo::TRANSLATE_X: 
                case ImGuizmo::TRANSLATE_Y: 
                case ImGuizmo::TRANSLATE_Z: 
                    settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Y;
                    settings.previousAxisSelected[0] = 2u;
                    break;
                case ImGuizmo::ROTATE:
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    settings.currentGizmoOperation = ImGuizmo::ROTATE_Y;
                    settings.previousAxisSelected[1] = 2u;
                    break;
                case ImGuizmo::SCALE:
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    settings.currentGizmoOperation = ImGuizmo::SCALE_Y;
                    settings.previousAxisSelected[2] = 2u;
                    break;
            };
        }

        ImGui::SameLine();
        if (button(!enableGizmo || settings.currentAxisSelected == 3u, "Z", "Z Axis Only", true)) {
            settings.currentAxisSelected = 3u;

            switch (settings.currentGizmoOperation) {
                case ImGuizmo::TRANSLATE: 
                case ImGuizmo::TRANSLATE_X: 
                case ImGuizmo::TRANSLATE_Y: 
                case ImGuizmo::TRANSLATE_Z: 
                    settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Z;
                    settings.previousAxisSelected[0] = 3u;
                    break;
                case ImGuizmo::ROTATE:
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    settings.currentGizmoOperation = ImGuizmo::ROTATE_Z;
                    settings.previousAxisSelected[1] = 3u;
                    break;
                case ImGuizmo::SCALE:
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    settings.currentGizmoOperation = ImGuizmo::SCALE_Z;
                    settings.previousAxisSelected[2] = 3u;
                    break;
            };
        }

        ImGui::SameLine();
        if (button(!enableGizmo || settings.currentAxisSelected == 0u, "All", "All Axis", true)) {
            settings.currentAxisSelected = 0u;

              switch (settings.currentGizmoOperation) {
                case ImGuizmo::TRANSLATE_X:
                case ImGuizmo::TRANSLATE_Y:
                case ImGuizmo::TRANSLATE_Z: 
                    settings.currentGizmoOperation = ImGuizmo::TRANSLATE;
                    settings.previousAxisSelected[0] = 0u;
                    break;
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    settings.currentGizmoOperation = ImGuizmo::ROTATE;
                    settings.previousAxisSelected[1] = 0u;
                    break;
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    settings.currentGizmoOperation = ImGuizmo::SCALE;
                    settings.previousAxisSelected[2] = 0u;
                    break;
            };
        }
        AButtonWidth = ImGui::GetItemRectSize().x;

        ImGui::SameLine();
        ImGui::Text(" ]");

        _parent.setTransformSettings(settings);
    }

    const Rect<I32>& SceneViewWindow::sceneRect() const noexcept {
        return  _sceneRect;
    }

    const vec2<I32>& SceneViewWindow::getWindowOffset() const noexcept {
        return _windowOffset;
    }
}