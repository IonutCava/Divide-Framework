

#include "Headers/SceneViewWindow.h"
#include "Editor/Headers/Utils.h"
#include "Editor/Headers/Editor.h"

#include "Editor/Widgets/Headers/ImGuiExtensions.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include <imgui_internal.h>
#include <IconsForkAwesome.h>

namespace Divide
{

    SceneViewWindow::SceneViewWindow(Editor& parent, const Descriptor& descriptor)
        : NodePreviewWindow(parent, descriptor)
    {
        _originalName = descriptor.name;
    }

    void SceneViewWindow::drawInternal()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        bool play = !_parent.simulationPaused();
        _descriptor.name = (play ? ICON_FK_PLAY_CIRCLE : ICON_FK_PAUSE_CIRCLE) + _originalName;
        ImGui::Text("Play:");
        ImGui::SameLine();
        if (ImGui::ToggleButton("Play", &play))
        {
            Attorney::EditorSceneViewWindow::simulationPaused(_parent, !play);
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Toggle scene playback");
        }

        ImGui::SameLine();
        const bool enableStepButtons = !play;
        if (button(enableStepButtons,
            ICON_FK_FORWARD,
            "When playback is paused, advanced the simulation by 1 full frame"))
        {
            Attorney::EditorSceneViewWindow::editorStepQueue(_parent, 2);
        }

        ImGui::SameLine();

        if (button(enableStepButtons,
            ICON_FK_FAST_FORWARD,
            Util::StringFormat("When playback is paused, advanced the simulation by {} full frame", Config::TARGET_FRAME_RATE).c_str()))
        {
            Attorney::EditorSceneViewWindow::editorStepQueue(_parent, Config::TARGET_FRAME_RATE + 1);
        }

        bool readOnly = false;

        bool enableGizmo = Attorney::EditorSceneViewWindow::editorEnabledGizmo(_parent);
        TransformSettings settings = _parent.getTransformSettings();

        const F32 ItemSpacing = ImGui::GetStyle().ItemSpacing.x;
        static F32 TButtonWidth = 10.0f;
        static F32 RButtonWidth = 10.0f;
        static F32 SButtonWidth = 10.0f;
        static F32 NButtonWidth = 10.0f;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::SameLine(window->Size.x * 0.49f);
        if (play)
        {
            PushReadOnly();
            readOnly = true;
        }

        if (button(true, ICON_FK_CAMERA_RETRO, "Copy the player's camera snapshot to the editor camera"))
        {
            Attorney::EditorSceneViewWindow::copyPlayerCamToEditorCam(_parent);
        }

        ImGui::SameLine();
        if (button(true, ICON_FK_HOME, "Set the editor's camera's position to (0, 0, 0) and view direction to (0, -1, 0)"))
        {
            Attorney::EditorSceneViewWindow::setEditorCamLookAt(_parent, VECTOR3_ZERO, WORLD_Z_NEG_AXIS, WORLD_Y_AXIS);
        }

        F32 pos = (2*SButtonWidth) + ItemSpacing + 25;
        ImGui::SameLine( window->Size.x - pos);
        if (button(!enableGizmo || !IsScaleOperation(settings), ICON_FK_EXPAND, "Scale", true))
        {
            switch (settings.previousAxisSelected[2])
            {
                case 0u: settings.currentGizmoOperation = ImGuizmo::SCALE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::SCALE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::SCALE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::SCALE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[2];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }

        SButtonWidth = ImGui::GetItemRectSize().x;

        pos += RButtonWidth + ItemSpacing + 1;
        ImGui::SameLine( window->Size.x - pos);
        if (button(!enableGizmo || !IsRotationOperation(settings), ICON_FK_REPEAT, "Rotate", true))
        {
            switch (settings.previousAxisSelected[1])
            {
                case 0u: settings.currentGizmoOperation = ImGuizmo::ROTATE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::ROTATE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::ROTATE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::ROTATE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[1];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }

        RButtonWidth = ImGui::GetItemRectSize().x;

        pos += TButtonWidth + ItemSpacing + 1;
        ImGui::SameLine( window->Size.x - pos);
        if (button(!enableGizmo || !IsTranslationOperation(settings), ICON_FK_ARROWS, "Translate", true))
        {
            switch (settings.previousAxisSelected[0])
            {
                case 0u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE;   break;
                case 1u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_X; break;
                case 2u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Y; break;
                case 3u: settings.currentGizmoOperation = ImGuizmo::TRANSLATE_Z; break;
            }
            settings.currentAxisSelected = settings.previousAxisSelected[0];
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, true);
        }

        TButtonWidth = ImGui::GetItemRectSize().x;

        pos += NButtonWidth + ItemSpacing + 1;
        ImGui::SameLine( window->Size.x - pos);
        if (button(enableGizmo, ICON_FK_MOUSE_POINTER, "Select", true))
        {
            Attorney::EditorSceneViewWindow::editorEnableGizmo(_parent, false);
            settings.currentAxisSelected = 0u;
        }

        NButtonWidth = ImGui::GetItemRectSize().x;

        if (play)
        {
            PopReadOnly();
            readOnly = false;
        }

        const RenderTarget* rt = _parent.context().gfx().renderTargetPool().getRenderTarget(RenderTargetNames::BACK_BUFFER);
        const Texture_ptr& gameView = rt->getAttachment(RTAttachmentType::COLOUR)->texture();

        NodePreviewWindow::drawInternal(gameView.get());

        if (play || !enableGizmo)
        {
            PushReadOnly();
            readOnly = true;
        }
        if (ImGui::RadioButton("Local", settings.currentGizmoMode == ImGuizmo::LOCAL))
        {
            settings.currentGizmoMode = ImGuizmo::LOCAL;
        }

        ImGui::SameLine();
        if (ImGui::RadioButton("World", settings.currentGizmoMode == ImGuizmo::WORLD))
        {
            settings.currentGizmoMode = ImGuizmo::WORLD;
        }

        ImGui::SameLine();
        ImGui::Text("Gizmo Axis [ ");
        ImGui::SameLine();
        Util::PushButtonStyle(true, Util::Colours[0], Util::ColoursHovered[0], Util::Colours[0]);
        if (button(enableGizmo && settings.currentAxisSelected != 1u, Util::FieldLabels[0], "X Axis Only", true))
        {
            settings.currentAxisSelected = 1u;

            switch (settings.currentGizmoOperation)
            {
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

                case ImGuizmo::ROTATE_SCREEN:
                case ImGuizmo::BOUNDS: 
                    break;
            };
        }
        Util::PopButtonStyle();
        ImGui::SameLine();
        Util::PushButtonStyle(true, Util::Colours[1], Util::ColoursHovered[1], Util::Colours[1]);
        if (button(enableGizmo && settings.currentAxisSelected != 2u, Util::FieldLabels[1], "Y Axis Only", true))
        {
            settings.currentAxisSelected = 2u;

            switch (settings.currentGizmoOperation)
            {
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
                case ImGuizmo::ROTATE_SCREEN:
                case ImGuizmo::BOUNDS:
                    break;
            };
        }
        Util::PopButtonStyle();
        ImGui::SameLine();
        Util::PushButtonStyle(true, Util::Colours[2], Util::ColoursHovered[2], Util::Colours[2]);
        if (button(enableGizmo && settings.currentAxisSelected != 3u, Util::FieldLabels[2], "Z Axis Only", true))
        {
            settings.currentAxisSelected = 3u;

            switch (settings.currentGizmoOperation)
            {
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
                case ImGuizmo::ROTATE_SCREEN:
                case ImGuizmo::BOUNDS:
                    break;
            };
        }
        Util::PopButtonStyle();

        ImGui::SameLine();
        Util::PushBoldFont();
        if (button(enableGizmo && settings.currentAxisSelected != 0u, "All", "All Axis", true))
        {
            settings.currentAxisSelected = 0u;

            switch (settings.currentGizmoOperation)
            {
                case ImGuizmo::TRANSLATE:
                case ImGuizmo::TRANSLATE_X:
                case ImGuizmo::TRANSLATE_Y:
                case ImGuizmo::TRANSLATE_Z: 
                    settings.currentGizmoOperation = ImGuizmo::TRANSLATE;
                    settings.previousAxisSelected[0] = 0u;
                    break;
                case ImGuizmo::ROTATE:
                case ImGuizmo::ROTATE_X:
                case ImGuizmo::ROTATE_Y:
                case ImGuizmo::ROTATE_Z:
                    settings.currentGizmoOperation = ImGuizmo::ROTATE;
                    settings.previousAxisSelected[1] = 0u;
                    break;
                case ImGuizmo::SCALE:
                case ImGuizmo::SCALE_X:
                case ImGuizmo::SCALE_Y:
                case ImGuizmo::SCALE_Z:
                    settings.currentGizmoOperation = ImGuizmo::SCALE;
                    settings.previousAxisSelected[2] = 0u;
                    break;
                case ImGuizmo::ROTATE_SCREEN:
                case ImGuizmo::BOUNDS:
                    break;
            };
        }
        Util::PopBoldFont();

        ImGui::SameLine();
        ImGui::Text(" ]");

        ImGui::SameLine(0.f, 25.0f);
        ImGui::Checkbox("Snap", &settings.useSnap);

        const ImGuiInputTextFlags flags = Util::GetDefaultFlagsForSettings(readOnly, false);
        if (settings.useSnap)
        {
            ImGui::SameLine();
            ImGui::Text("Step:");
            ImGui::SameLine();
            ImGui::PushItemWidth(150);
            {
                if (IsTranslationOperation(settings))
                {
                    switch (settings.currentGizmoOperation)
                    {
                        case ImGuizmo::TRANSLATE:
                            for (size_t i = 0; i < 3; ++i)
                            {
                                Util::DrawVecComponent<F32, false>(ImGuiDataType_Float, 
                                    Util::FieldLabels[i], 
                                    settings.snapTranslation[i],
                                    0.f,
                                    0.001f,
                                    1000.f,
                                    0.f,
                                    0.f,
                                    Util::Colours[i],
                                    Util::ColoursHovered[i],
                                    Util::Colours[i],
                                    flags);
                                ImGui::SameLine();
                            }
                            ImGui::Dummy(ImVec2(0, 0));
                            break;
                        case ImGuizmo::TRANSLATE_X:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[0],
                                settings.snapTranslation[0],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[0],
                                Util::ColoursHovered[0],
                                Util::Colours[0],
                                flags);
                            break;
                        case ImGuizmo::TRANSLATE_Y:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[1],
                                settings.snapTranslation[1],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[1],
                                Util::ColoursHovered[1],
                                Util::Colours[1],
                                flags);
                            break;
                        case ImGuizmo::TRANSLATE_Z:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[2],
                                settings.snapTranslation[2],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[2],
                                Util::ColoursHovered[2],
                                Util::Colours[2],
                                flags);
                            break;
                        default: DIVIDE_UNEXPECTED_CALL(); break;
                    }
                }
                else if (IsRotationOperation(settings))
                {
                    switch (settings.currentGizmoOperation)
                    {
                        case ImGuizmo::ROTATE:
                            for (size_t i = 0; i < 3; ++i)
                            {
                                Util::DrawVecComponent<F32, false>(ImGuiDataType_Float, 
                                    Util::FieldLabels[i], 
                                    settings.snapTranslation[i],
                                    0.f,
                                    0.001f,
                                    1000.f,
                                    0.f,
                                    0.f,
                                    Util::Colours[i],
                                    Util::ColoursHovered[i],
                                    Util::Colours[i],
                                    flags);
                                ImGui::SameLine();
                            }
                            ImGui::Dummy(ImVec2(0, 0));
                            break;
                        case ImGuizmo::ROTATE_X:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[0],
                                settings.snapRotation[0],
                                0.f,
                                0.001f,
                                180.f,
                                0.f,
                                0.f,
                                Util::Colours[0],
                                Util::ColoursHovered[0],
                                Util::Colours[0],
                                flags);
                            break;
                        case ImGuizmo::ROTATE_Y:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[1],
                                settings.snapRotation[1],
                                0.f,
                                0.001f,
                                180.f,
                                0.f,
                                0.f,
                                Util::Colours[1],
                                Util::ColoursHovered[1],
                                Util::Colours[1],
                                flags);
                            break;
                        case ImGuizmo::ROTATE_Z:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[2],
                                settings.snapRotation[2],
                                0.f,
                                0.001f,
                                180.f,
                                0.f,
                                0.f,
                                Util::Colours[2],
                                Util::ColoursHovered[2],
                                Util::Colours[2],
                                flags);
                            break;
                        default: DIVIDE_UNEXPECTED_CALL(); break;
                    }
                }
                else if (IsScaleOperation(settings))
                {
                    switch (settings.currentGizmoOperation)
                    {
                        case ImGuizmo::SCALE:
                            for (size_t i = 0; i < 3; ++i)
                            {
                                Util::DrawVecComponent<F32, false>(ImGuiDataType_Float, 
                                    Util::FieldLabels[i], 
                                    settings.snapScale[i],
                                    0.f,
                                    0.001f,
                                    1000.f,
                                    0.f,
                                    0.f,
                                    Util::Colours[i],
                                    Util::ColoursHovered[i],
                                    Util::Colours[i],
                                    flags);
                                ImGui::SameLine();
                            }
                            ImGui::Dummy(ImVec2(0, 0));
                            break;
                        case ImGuizmo::SCALE_X:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[0],
                                settings.snapScale[0],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[0],
                                Util::ColoursHovered[0],
                                Util::Colours[0],
                                flags);
                            break;
                        case ImGuizmo::SCALE_Y:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[1],
                                settings.snapScale[1],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[1],
                                Util::ColoursHovered[1],
                                Util::Colours[1],
                                flags);
                            break;
                        case ImGuizmo::SCALE_Z:
                            Util::DrawVecComponent<F32, false>(ImGuiDataType_Float,
                                Util::FieldLabels[2],
                                settings.snapScale[2],
                                0.f,
                                0.001f,
                                1000.f,
                                0.f,
                                0.f,
                                Util::Colours[2],
                                Util::ColoursHovered[2],
                                Util::Colours[2],
                                flags);
                            break;
                        default: DIVIDE_UNEXPECTED_CALL(); break;
                    }
                }
            }
            ImGui::PopItemWidth();
        }

        _parent.setTransformSettings(settings);
        if (play || !enableGizmo)
        {
            PopReadOnly();
            readOnly = false;
        }

        ImGui::SameLine( window->Size.x * 0.95f);

        bool enableGrid = _parent.infiniteGridEnabledScene();
        if (ImGui::Checkbox(ICON_FK_PLUS_SQUARE_O" Infinite Grid", &enableGrid))
        {
            _parent.infiniteGridEnabledScene(enableGrid);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Toggle the editor XZ grid on/off.\nGrid sizing is controlled in the \"Editor options\" window (under \"File\" in the menu bar)");
        }
    }
} //namespace Divide
