// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "imgui-node-editor/imgui_node_editor_internal.h"

namespace Divide
{
    TEST_CASE("Vendored node editor preserves node positions", "[node_editor]")
    {
        namespace ed = ax::NodeEditor;

        ImGuiContext* previousGui = ImGui::GetCurrentContext();
        ImGuiContext* gui = ImGui::CreateContext();
        SCOPE_EXIT
        {
            ImGui::DestroyContext(gui);
            ImGui::SetCurrentContext(previousGui);
        };
        REQUIRE(gui != nullptr);
        ImGui::SetCurrentContext(gui);

        ed::Config config;
        config.SettingsFile = nullptr;
        ed::EditorContext* previousEditor = ed::GetCurrentEditor();
        ed::EditorContext* editor = ed::CreateEditor(&config);
        SCOPE_EXIT
        {
            ed::SetCurrentEditor(previousEditor);
            ed::DestroyEditor(editor);
        };
        REQUIRE(editor != nullptr);
        ed::SetCurrentEditor(editor);

        const ed::NodeId node(1);
        ed::SetNodePosition(node, ImVec2(12.75f, 43.25f));
        const ImVec2 position = ed::GetNodePosition(node);
        CHECK(position.x == 12.f);
        CHECK(position.y == 43.f);

        ed::SetNodePosition(node, ImVec2(21.f, 31.f));
        const ImVec2 moved = ed::GetNodePosition(node);
        CHECK(moved.x == 21.f);
        CHECK(moved.y == 31.f);
    }

    TEST_CASE("Vendored node editor draws borders with the requested thickness", "[node_editor]")
    {
        namespace ed = ax::NodeEditor;

        ImDrawListSharedData shared;
        shared.InitialFlags = 0;
        shared.InitialFringeScale = 1.f;
        ImDrawList drawList(&shared);
        drawList._ResetForNewFrame();

        ed::Detail::Node node(nullptr, ed::NodeId(1));
        node.m_Bounds = ImRect(ImVec2(10.f, 20.f), ImVec2(110.f, 120.f));
        node.DrawBorder(&drawList, IM_COL32_WHITE, 3.f);

        REQUIRE(drawList.VtxBuffer.Size > 0);
        ImVec2 min = drawList.VtxBuffer[0].pos;
        ImVec2 max = min;
        for (const ImDrawVert& vertex : drawList.VtxBuffer)
        {
            min.x = ImMin(min.x, vertex.pos.x);
            min.y = ImMin(min.y, vertex.pos.y);
            max.x = ImMax(max.x, vertex.pos.x);
            max.y = ImMax(max.y, vertex.pos.y);
        }

        CHECK(min.x >= 8.9f);
        CHECK(min.x <= 9.1f);
        CHECK(max.x >= 110.9f);
        CHECK(max.x <= 111.1f);
        CHECK(min.y >= 18.9f);
        CHECK(min.y <= 19.1f);
        CHECK(max.y >= 120.9f);
        CHECK(max.y <= 121.1f);
    }
}
