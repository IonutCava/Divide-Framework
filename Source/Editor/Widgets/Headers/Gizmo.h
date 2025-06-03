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

#pragma once
#ifndef DVD_EDITOR_GIZMO_H_
#define DVD_EDITOR_GIZMO_H_

#include "ECS/Components/Headers/TransformComponent.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

#include <imgui.h>
#include <ImGuizmo.h>

struct ImGuiContext;

namespace Divide {
    class Editor;
    class Camera;
    class DisplayWindow;
    class SceneGraphNode;
    struct SizeChangeParams;

    namespace GFX
    {
        class CommandBuffer;
        struct MemoryBarrierCommand;
    };

    namespace Attorney {
        class GizmoEditor;
    }
    struct TransformSettings {
        ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
        F32 snapTranslation[3] = { 1.f, 1.f, 1.f };
        F32 snapScale[3] = { 1.f, 1.f, 1.f };
        F32 snapRotation[3] = { 1.f, 1.f, 1.f };
        U8 previousAxisSelected[3] = { 0u, 0u, 0u }; //0 = all, 1 = x, 2 = y, 3 = z
        U8 currentAxisSelected = 0u;
        bool useSnap = false;
    };

    [[nodiscard]] FORCE_INLINE bool IsTranslationOperation(const TransformSettings& settings) {
        return settings.currentGizmoOperation == ImGuizmo::OPERATION::TRANSLATE ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::TRANSLATE_X ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::TRANSLATE_Y ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::TRANSLATE_Z;
    }

    [[nodiscard]] FORCE_INLINE bool IsRotationOperation(const TransformSettings& settings) {
        return settings.currentGizmoOperation == ImGuizmo::OPERATION::ROTATE ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::ROTATE_X ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::ROTATE_Y ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::ROTATE_Z;
    }

    [[nodiscard]] FORCE_INLINE bool IsScaleOperation(const TransformSettings& settings) {
        return settings.currentGizmoOperation == ImGuizmo::OPERATION::SCALE ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::SCALE_X ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::SCALE_Y ||
               settings.currentGizmoOperation == ImGuizmo::OPERATION::SCALE_Z;
    }

    class Gizmo {
        friend class Attorney::GizmoEditor;

    public:
        explicit Gizmo(Editor& parent, ImGuiContext* targetContext, F32 clipSpaceSize);
        ~Gizmo();

        [[nodiscard]] ImGuiContext& getContext() noexcept;
        [[nodiscard]] const ImGuiContext& getContext() const noexcept;

        void enable(bool state) noexcept;

        [[nodiscard]] bool needsMouse() const;
        [[nodiscard]] bool isHovered() const noexcept;
        [[nodiscard]] bool isUsing() const noexcept;
        [[nodiscard]] bool isEnabled() const noexcept;
        [[nodiscard]] bool isActive() const noexcept;

        [[nodiscard]] bool onMouseButtonPressed(Input::MouseButtonEvent& argInOut) noexcept;
        [[nodiscard]] bool onMouseButtonReleased(Input::MouseButtonEvent& argInOut) noexcept;
        [[nodiscard]] bool onKeyUp(Input::KeyEvent& argInOut);
        [[nodiscard]] bool onKeyDown(Input::KeyEvent& argInOut);

    protected:
        void update(U64 deltaTimeUS);
        void render(const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
        void renderSingleSelection();
        void renderMultipleSelections();
        void updateSelections();
        void updateSelections(const vector<SceneGraphNode*>& nodes);
        void setTransformSettings(const TransformSettings& settings) noexcept;
        [[nodiscard]] const TransformSettings& getTransformSettings() const noexcept;
        void onSceneFocus(bool state) noexcept;

        void updateSelectionsInternal();

        void onResolutionChange(const SizeChangeParams& params) noexcept;
        void onWindowSizeChange(const SizeChangeParams& params) noexcept;

        void onKeyInternal(const Input::KeyEvent& arg, bool pressed);

    private:
        struct SelectedNode {
            TransformComponent* tComp = nullptr;
            TransformValues _initialValues;
        };
        void applyTransforms(const SelectedNode& node, const float3& position, const vec3<Angle::DEGREES_F>& euler, const float3& scale);

    private:
        Editor& _parent;
        bool _enabled = false;
        bool _wasUsed = false;
        bool _shouldRegisterUndo = false;
        vector<SelectedNode> _selectedNodes;
        ImGuiContext* _imguiContext = nullptr;
        TransformSettings _transformSettings;

        mat4<F32> _workMatrix;
        mat4<F32> _gridMatrix;
        mat4<F32> _localToWorldMatrix;
        mat4<F32> _deltaMatrix;
        F32 _clipSpaceSize{1.f};
    };

    FWD_DECLARE_MANAGED_CLASS(Gizmo);

    namespace Attorney
    {
        class GizmoEditor
        {
            static void render(Gizmo* gizmo, const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut)
            {
                gizmo->render(camera, targetViewport, bufferInOut, memCmdInOut);
            }

            static void updateSelections(Gizmo* gizmo, const vector<SceneGraphNode*>& nodes)
            {
                gizmo->updateSelections(nodes);
            }

            static void updateSelections(Gizmo* gizmo)
            {
                gizmo->updateSelections();
            }

            static void update(Gizmo* gizmo, const U64 deltaTimeUS)
            {
                gizmo->update(deltaTimeUS);
            }

            static void setTransformSettings(Gizmo* gizmo, const TransformSettings& settings) noexcept
            {
                gizmo->setTransformSettings(settings);
            }

            static const TransformSettings& getTransformSettings(const Gizmo* gizmo) noexcept
            {
                return gizmo->getTransformSettings();
            }

            static void onSceneFocus(Gizmo* gizmo, const bool state) noexcept
            {
                gizmo->onSceneFocus(state);
            }

            static void onResolutionChange(Gizmo* gizmo, const SizeChangeParams& params ) noexcept
            {
                gizmo->onResolutionChange(params);
            }

            static void onWindowSizeChange(Gizmo* gizmo, const SizeChangeParams& params) noexcept
            {
                gizmo->onWindowSizeChange(params);
            }

            friend class Divide::Editor;
        };
    }
} //namespace Divide

#endif //DVD_EDITOR_GIZMO_H_
