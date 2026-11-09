// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "ECS/Components/Headers/EditorComponent.h"
#include "Editor/Headers/UndoManager.h"
#include "Editor/Widgets/DockedWindows/Headers/SceneViewWindow.h"
#include "Platform/Video/Headers/GFXDevice.h"

#include <IconsForkAwesome.h>
#include "imgui-node-editor/imgui_node_editor_internal.h"

namespace Divide
{
    namespace Attorney
    {
        class EditorComponentTestAccess
        {
        public:
            static void Save(const EditorComponent& component, boost::property_tree::ptree& tree)
            {
                component.saveToXML(tree);
            }

            static void Load(EditorComponent& component, const boost::property_tree::ptree& tree)
            {
                component.loadFromXML(tree);
            }
        };
    }

    TEST_CASE("Typed dropdown fields preserve automatic and explicit values through XML and undo", "[editor][serialization][worldAO]")
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{app};
        EditorComponent component{context, ComponentType::RENDERING, "RENDERING"};
        U32 value = 0u;
        U32 notifications = 0u;
        component.onChangedCbk([&notifications](std::string_view) { ++notifications; });
        EditorComponentField field{};
        field._name = "World AO Participation";
        field._type = EditorComponentFieldType::DROPDOWN_TYPE;
        field._basicType = PushConstantType::UINT;
        field._range = {0.f, 3.f};
        field._data = &value;
        component.registerField(MOV(field));

        for (const U32 saved : {0u, 1u, 2u})
        {
            value = saved;
            boost::property_tree::ptree tree;
            Attorney::EditorComponentTestAccess::Save(component, tree);
            CHECK(tree.get<U32>("RENDERING.World__AO__Participation") == saved);
            value = (saved + 1u) % 3u;
            Attorney::EditorComponentTestAccess::Load(component, tree);
            CHECK(value == saved);
        }

        boost::property_tree::ptree missing;
        missing.put("RENDERING.Unrelated", 0u);
        value = 0u;
        Attorney::EditorComponentTestAccess::Load(component, missing);
        CHECK(value == 0u);

        boost::property_tree::ptree invalid;
        invalid.put("RENDERING.World__AO__Participation", 3u);
        value = 2u;
        Attorney::EditorComponentTestAccess::Load(component, invalid);
        CHECK(value == 2u);
        invalid.put("RENDERING.World__AO__Participation", "not-an-index");
        Attorney::EditorComponentTestAccess::Load(component, invalid);
        CHECK(value == 2u);

        EditorComponentField& registered = component.fields().front();
        UndoEntry<U32> edit{};
        edit._name = registered._name;
        edit._oldVal = 0u;
        edit._newVal = 2u;
        edit._dataSetter = [&registered](const U32& state) { registered.set<U32>(state); };
        edit._onChangedCbk = registered._onUndoRedo;
        UndoManager history{4u};
        history.registerUndoEntry(edit);
        CHECK(history.Undo());
        CHECK(value == 0u);
        CHECK(history.Redo());
        CHECK(value == 2u);
        CHECK(notifications == 2u);
    }

    TEST_CASE("Scene View uses the frame with in-game UI", "[editor][scene_view]")
    {
        const RenderTargetID previousScreen = RenderTargetNames::SCREEN;
        const RenderTargetID previousBackBuffer = RenderTargetNames::BACK_BUFFER;
        SCOPE_EXIT
        {
            RenderTargetNames::SCREEN = previousScreen;
            RenderTargetNames::BACK_BUFFER = previousBackBuffer;
        };

        RenderTargetNames::SCREEN = 42u;
        RenderTargetNames::BACK_BUFFER = 73u;
        CHECK(SceneViewWindow::GameViewRenderTarget() == RenderTargetNames::BACK_BUFFER);
        CHECK(SceneViewWindow::GameViewRenderTarget() != RenderTargetNames::SCREEN);
    }

    TEST_CASE("Scene View keeps its saved dock when the playback title changes", "[editor][docking]")
    {
        ImGuiContext* previousGui = ImGui::GetCurrentContext();
        ImGuiContext* gui = ImGui::CreateContext();
        SCOPE_EXIT
        {
            ImGui::DestroyContext(gui);
            ImGui::SetCurrentContext(previousGui);
        };
        REQUIRE(gui != nullptr);
        ImGui::SetCurrentContext(gui);
        ImGui::GetIO().IniFilename = nullptr;

        const std::string ini =
            "[Window][" ICON_FK_EYE " Node Preview]\n"
            "Pos=100,100\nSize=400,300\nDockId=0x00000008,0\n\n"
            "[Window][AnimatedTitlePlayState]\n"
            "Pos=100,100\nSize=400,300\nDockId=0x00000008,1\n";
        ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());

        const ImGuiID windowID = ImHashStr(SceneViewWindow::WINDOW_NAME);
        const std::string pausedName = std::string(ICON_FK_PAUSE_CIRCLE) + SceneViewWindow::WINDOW_NAME;
        const std::string playingName = std::string(ICON_FK_PLAY_CIRCLE) + SceneViewWindow::WINDOW_NAME;
        CHECK(ImHashStr(pausedName.c_str()) == windowID);
        CHECK(ImHashStr(playingName.c_str()) == windowID);

        const ImGuiWindowSettings* scene = ImGui::FindWindowSettingsByID(windowID);
        const ImGuiWindowSettings* preview = ImGui::FindWindowSettingsByID(ImHashStr(ICON_FK_EYE " Node Preview"));
        REQUIRE(scene != nullptr);
        REQUIRE(preview != nullptr);
        CHECK(scene->DockId != 0u);
        CHECK(scene->DockId == preview->DockId);
    }

    TEST_CASE("Scene View is selected initially without stealing later tab choices", "[editor][docking]")
    {
        ImGuiContext* previousGui = ImGui::GetCurrentContext();
        ImGuiContext* gui = ImGui::CreateContext();
        SCOPE_EXIT
        {
            ImGui::DestroyContext(gui);
            ImGui::SetCurrentContext(previousGui);
        };
        REQUIRE(gui != nullptr);
        ImGui::SetCurrentContext(gui);

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.DisplaySize = ImVec2(1024.f, 768.f);
        io.DeltaTime = 1.f / 60.f;

        constexpr const char* previewName = ICON_FK_EYE " Node Preview";
        const std::string pausedName = std::string(ICON_FK_PAUSE_CIRCLE) + SceneViewWindow::WINDOW_NAME;
        const std::string playingName = std::string(ICON_FK_PLAY_CIRCLE) + SceneViewWindow::WINDOW_NAME;
        const ImGuiID dockID = ImHashStr("SceneViewTestDock");
        const ImGuiID editorID = ImHashStr("Editor test");
        const ImGuiID previewID = ImHashStr(previewName);
        const ImGuiID sceneID = ImHashStr(SceneViewWindow::WINDOW_NAME);
        const ImGuiID savedPreviewTabID = ImHashStr("#TAB", 0u, previewID);
        const I32 previewOrder = GENERATE(0, 1);
        const I32 sceneOrder = GENERATE(0, 1);
        bool focusPending = true;
        bool selectPreview = false;
        const char* sceneName = pausedName.c_str();

        const string savedLayout = Util::StringFormat(
            "[Window][{}]\nPos=0,0\nSize=800,600\nDockId=0x{:08X},{}\n\n"
            "[Window][AnimatedTitlePlayState]\nPos=0,0\nSize=800,600\nDockId=0x{:08X},{}\n\n"
            "[Docking][Data]\nDockSpace ID=0x{:08X} Window=0x{:08X} Pos=0,0 Size=800,600 Selected=0x{:08X}\n",
            previewName, dockID, previewOrder, dockID, sceneOrder, dockID, editorID, savedPreviewTabID);
        ImGui::LoadIniSettingsFromMemory(savedLayout.c_str(), savedLayout.size());

        const auto drawFrame = [&](const bool focusOtherWindow)
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(ImVec2(800.f, 600.f));
            ImGui::Begin("Editor test", nullptr, ImGuiWindowFlags_NoDocking);
            ImGui::DockSpace(dockID);
            if (selectPreview)
            {
                ImGui::SetNextWindowFocus();
                selectPreview = false;
            }
            ImGui::Begin(previewName);
            ImGui::End();
            ImGui::Begin(sceneName);
            DockedWindow::FocusOnFirstDraw(focusPending);
            ImGui::End();
            if (focusOtherWindow)
            {
                ImGui::SetNextWindowFocus();
            }
            ImGui::Begin("Other tool window");
            ImGui::End();
            ImGui::End();
            ImGui::Render();
        };

        drawFrame(true);
        drawFrame(false);
        drawFrame(false);
        ImGuiDockNode* dock = ImGui::DockBuilderGetNode(dockID);
        REQUIRE(dock != nullptr);
        const ImGuiWindow* scene = ImGui::FindWindowByID(sceneID);
        const ImGuiWindow* preview = ImGui::FindWindowByID(previewID);
        REQUIRE(scene != nullptr);
        REQUIRE(preview != nullptr);
        REQUIRE(preview->TabId == savedPreviewTabID);
        CHECK_FALSE(focusPending);
        CHECK(dock->SelectedTabId == scene->TabId);

        selectPreview = true;
        sceneName = playingName.c_str();
        drawFrame(false);
        drawFrame(false);
        CHECK(dock->SelectedTabId == preview->TabId);
        CHECK(scene->DockId == dockID);
        CHECK(preview->DockId == dockID);
    }

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
