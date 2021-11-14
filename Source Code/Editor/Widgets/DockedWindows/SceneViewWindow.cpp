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
        ImGui::SameLine();

        const bool enableGizmo = Attorney::EditorSceneViewWindow::editorEnabledGizmo(_parent);
        TransformSettings settings = _parent.getTransformSettings();

        const F32 ItemSpacing = ImGui::GetStyle().ItemSpacing.x;
        static F32 TButtonWidth = 10.0f;
        static F32 RButtonWidth = 10.0f;
        static F32 SButtonWidth = 10.0f;
        static F32 NButtonWidth = 10.0f;

        F32 pos = SButtonWidth + ItemSpacing + 25;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::SCALE, "S", "Scale", true)) {
            settings.currentGizmoOperation = ImGuizmo::SCALE;
        }
        SButtonWidth = ImGui::GetItemRectSize().x;

        pos += RButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::ROTATE, "R", "Rotate", true)) {
            settings.currentGizmoOperation = ImGuizmo::ROTATE;
        }
        RButtonWidth = ImGui::GetItemRectSize().x;

        pos += TButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(!enableGizmo || settings.currentGizmoOperation == ImGuizmo::TRANSLATE, "T", "Translate", true)) {
            settings.currentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        TButtonWidth = ImGui::GetItemRectSize().x;

        pos += NButtonWidth + ItemSpacing + 5;
        ImGui::SameLine(window->ContentSize.x - pos);
        if (button(false, "N", "Select", true)) {
            
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
                    ImGui::InputFloat3("Position", &settings.snap[0]);
                    break;
                case ImGuizmo::ROTATE:
                    ImGui::InputFloat("Angle", &settings.snap[0]);
                    break;
                case ImGuizmo::SCALE:
                    ImGui::InputFloat("Scale", &settings.snap[0]);
                    break;
                case ImGuizmo::BOUNDS: break;
            }
            ImGui::PopItemWidth();
        }

        _parent.setTransformSettings(settings);
    }

    const Rect<I32>& SceneViewWindow::sceneRect() const noexcept {
        return  _sceneRect;
    }

    const vec2<I32>& SceneViewWindow::getWindowOffset() const noexcept {
        return _windowOffset;
    }
}