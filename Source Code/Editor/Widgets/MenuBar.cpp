#include "stdafx.h"

#include "Headers/MenuBar.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Editor/Headers/Editor.h"

#include "Managers/Headers/SceneManager.h"

#include "Geometry/Shapes/Predefined/Headers/Box3D.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/PostFX/Headers/PreRenderOperator.h"

#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Systems/Headers/ECSManager.h"

#include <ImGuiMisc/imguifilesystem/imguifilesystem.h>


#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Systems/Headers/AnimationSystem.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Material/Headers/MaterialEnums.h"

namespace Divide {
namespace {
    SceneGraphNodeDescriptor g_nodeDescriptor;
    SceneNodeType g_currentNodeType = SceneNodeType::TYPE_OBJECT3D;

    static ResourcePath g_scenePath;

    const string s_messages[] = {
        "Please wait while saving current scene! App may appear frozen or stuttery for up to 30 seconds ...",
        "Saved scene succesfully",
        "Failed to save the current scene"
    };

    struct SaveSceneParams {
        string _saveMessage = "";
        const char* _saveNameOverride = "";
        U32 _saveElementCount = 0u;
        U32 _saveProgress = 0u;
        bool _closePopup = false;
    } g_saveSceneParams;

    const char* UsageToString(const RenderTargetUsage usage) noexcept {
        switch (usage) {
            case RenderTargetUsage::EDITOR: return "Editor";
            case RenderTargetUsage::ENVIRONMENT: return "Environment";
            case RenderTargetUsage::POSTFX_DATA: return "PostFX Data";
            case RenderTargetUsage::SSR_RESULT: return "SSR Result";
            case RenderTargetUsage::HI_Z: return "HI-Z";
            case RenderTargetUsage::HI_Z_REFLECT: return "HI-Z Reflect";
            case RenderTargetUsage::OIT: return "OIT";
            case RenderTargetUsage::OIT_MS: return "OIT_MS";
            case RenderTargetUsage::OIT_REFLECT: return "OIT_REFLECT";
            case RenderTargetUsage::OTHER: return "Other";
            case RenderTargetUsage::REFLECTION_CUBE: return "Cube Reflection";
            case RenderTargetUsage::REFLECTION_PLANAR: return "Planar Reflection";
            case RenderTargetUsage::REFLECTION_PLANAR_BLUR: return "Planar Reflection Blur";
            case RenderTargetUsage::REFRACTION_PLANAR: return "Planar Refraction";
            case RenderTargetUsage::SCREEN: return "Screen";
            case RenderTargetUsage::SCREEN_MS: return "Screen_MS";
            case RenderTargetUsage::SHADOW: return "Shadow";
            case RenderTargetUsage::COUNT: break;
        }

        return "Unknown";
    }

    const char* EdgeMethodName(const PreRenderBatch::EdgeDetectionMethod method) noexcept {
        switch (method) {
            case PreRenderBatch::EdgeDetectionMethod::Depth: return "Depth";
            case PreRenderBatch::EdgeDetectionMethod::Luma: return "Luma";
            case PreRenderBatch::EdgeDetectionMethod::Colour: return "Colour";
            case PreRenderBatch::EdgeDetectionMethod::COUNT: break;
        }
        return "Unknown";
    }
}


MenuBar::MenuBar(PlatformContext& context, const bool mainMenu)
    : PlatformContextComponent(context),
      _isMainMenu(mainMenu),
      _sceneOpenDialog(true, true),
      _sceneSaveDialog(true)
{
    g_scenePath = Paths::g_rootPath + Paths::g_xmlDataLocation + Paths::g_scenesLocation;
}

void MenuBar::draw() {
    if (ImGui::BeginMenuBar())
    {
        drawFileMenu();
        drawEditMenu();
        drawProjectMenu();
        drawObjectMenu();
        drawToolsMenu();
        //drawWindowsMenu();
        drawPostFXMenu();
        drawDebugMenu();
        drawHelpMenu();

        ImGui::EndMenuBar();

       for (vector<Texture_ptr>::iterator it = std::begin(_previewTextures); it != std::end(_previewTextures); ) {
            if (Attorney::EditorGeneralWidget::modalTextureView(_context.editor(), Util::StringFormat("Image Preview: %s", (*it)->resourceName().c_str()).c_str(), (*it).get(), vec2<F32>(512, 512), true, false)) {
                it = _previewTextures.erase(it);
            } else {
                ++it;
            }
        }

        if (_closePopup) {
            ImGui::OpenPopup("Confirm Close");

            if (ImGui::BeginPopupModal("Confirm Close", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Are you sure you want to close the editor? You have unsaved items!");
                ImGui::Separator();

                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _closePopup = false;
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Yes", ImVec2(120, 0))) {

                    ImGui::CloseCurrentPopup();
                    _closePopup = false;
                    _context.editor().toggle(false);
                }
                ImGui::EndPopup();
            }
        }

        if (!_errorMsg.empty()) {
            ImGui::OpenPopup("Error!");
            if (ImGui::BeginPopupModal("Error!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(_errorMsg.c_str());
                if (ImGui::Button("Ok")) {
                    ImGui::CloseCurrentPopup();
                    _errorMsg.clear();
                }
                ImGui::EndPopup();
            }
        }

        if (_newScenePopup) {
            ImGui::OpenPopup("Create New Scene");
            if (ImGui::BeginPopupModal("Create New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char buf[256];
                ImGui::Text("WARNING: All unsaved changes will be lost!");
                if (ImGui::InputText("New Scene Name", &buf[0], 254)) {

                }
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _newScenePopup = false;
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Create", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _newScenePopup = false;
                    string sceneName(buf);
                    if (sceneName.empty()) {
                        _errorMsg.append("Scenes must be named!. \n");
                    } else if (!Util::CompareIgnoreCase(sceneName, Config::DEFAULT_SCENE_NAME)) {
                        FileError ret = copyDirectory(g_scenePath + "/" + Config::DEFAULT_SCENE_NAME, g_scenePath + "/" + sceneName, true, true);
                        if (ret != FileError::NONE) {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                        ret = copyFile(g_scenePath.c_str(),
                                       (Config::DEFAULT_SCENE_NAME + string(".xml")).c_str(),
                                       g_scenePath.c_str(),
                                       (sceneName + ".xml").c_str(),
                                       true);
                        if (ret != FileError::NONE) {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                        Attorney::EditorGeneralWidget::switchScene(_context.editor(), (g_scenePath + "/" + sceneName).c_str());
                    } else {
                        _errorMsg.append("Tried to use a reserved name for a new scene! Try a differeng name. \n");
                    }
                }
                ImGui::EndPopup();
            }
        }

        if (_quitPopup) {
            ImGui::OpenPopup("Confirm Quit");
            if (ImGui::BeginPopupModal("Confirm Quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Are you sure you want to quit?");
                ImGui::Separator();

                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _quitPopup = false;
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Yes", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _quitPopup = false;
                    context().app().RequestShutdown();
                }
                ImGui::EndPopup();
            }
        }
        if (_savePopup) {
            ImGui::OpenPopup("Saving Scene");
            if (ImGui::BeginPopupModal("Saving Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                constexpr U32 maxSize = 40u;
                const U32 ident = MAP(g_saveSceneParams._saveProgress, 0u, g_saveSceneParams._saveElementCount, 0u, maxSize - 5u /*overestimate a bit*/);

                ImGui::Text("Saving Scene!\n\n%s", g_saveSceneParams._saveMessage.c_str());
                ImGui::Separator();

                string progress;
                for (U32 i = 0; i < maxSize; ++i) {
                    progress.append(i < ident ? "=" : " ");
                }
                ImGui::Text("[%s]", progress.c_str());
                ImGui::Separator();

                if (g_saveSceneParams._closePopup) {
                    _savePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        if (_newPrimitiveType != ObjectType::COUNT) {
            ImGui::OpenPopup("Create Primitive");
            if (ImGui::BeginPopupModal("Create Primitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(Util::StringFormat("Create a new [ %s ]?", TypeUtil::ObjectTypeToString(_newPrimitiveType)).c_str());
                ImGui::Separator();

                static char buf[64];
                if (ImGui::InputText("Name", &buf[0], 61)) {
                    g_nodeDescriptor._name = buf;
                }
                
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                    _newPrimitiveType = ObjectType::COUNT;
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (g_nodeDescriptor._name.empty()) {
                    PushReadOnly();
                }
                if (ImGui::Button("Create", ImVec2(120, 0))) {
                    g_currentNodeType = SceneNodeType::TYPE_OBJECT3D;

                    g_nodeDescriptor._componentMask = to_U32(ComponentType::TRANSFORM) |
                        to_U32(ComponentType::BOUNDS) |
                        to_U32(ComponentType::RIGID_BODY) |
                        to_U32(ComponentType::RENDERING) |
                        to_U32(ComponentType::SELECTION) |
                        to_U32(ComponentType::NAVIGATION);
                    g_nodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;

                    ResourceCache* parentCache = _context.kernel().resourceCache();
                    ResourceDescriptor nodeDescriptor(g_nodeDescriptor._name + "_n");
                    switch (_newPrimitiveType) {
                        case ObjectType::SPHERE_3D:
                        {
                            auto node = CreateResource<Sphere3D>(parentCache, nodeDescriptor);
                            node->setResolution(16);
                            node->setRadius(1.0f);
                            g_nodeDescriptor._node = node;
                        } break;
                        case ObjectType::BOX_3D:
                        {
                            auto node = CreateResource<Box3D>(parentCache, nodeDescriptor);
                            node->setHalfExtent(1.0f);
                            g_nodeDescriptor._node = node;
                        } break;
                        case ObjectType::QUAD_3D:
                        {
                            P32 quadMask;
                            quadMask.i = 0;
                            quadMask.b[0] = 1;
                            nodeDescriptor.mask(quadMask);

                            auto node = CreateResource<Quad3D>(parentCache, nodeDescriptor);
                            node->setCorner(Quad3D::CornerLocation::TOP_LEFT, vec3<F32>(0, 1, 0));
                            node->setCorner(Quad3D::CornerLocation::TOP_RIGHT, vec3<F32>(1, 1, 0));
                            node->setCorner(Quad3D::CornerLocation::BOTTOM_LEFT, vec3<F32>(0, 0, 0));
                            node->setCorner(Quad3D::CornerLocation::BOTTOM_RIGHT, vec3<F32>(1, 0, 0));

                            g_nodeDescriptor._node = node;
                        } break;
                        default: 
                            DIVIDE_UNEXPECTED_CALL();
                            break;
                    }
                    if (g_nodeDescriptor._node != nullptr) {
                        g_nodeDescriptor._node->getMaterialTpl()->shadingMode(ShadingMode::BLINN_PHONG);
                        g_nodeDescriptor._node->getMaterialTpl()->baseColour(FColour4(0.4f, 0.4f, 0.4f, 1.0f));
                        g_nodeDescriptor._node->getMaterialTpl()->roughness(0.5f);
                        g_nodeDescriptor._node->getMaterialTpl()->metallic(0.5f);
                        const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
                        activeScene.sceneGraph()->getRoot()->addChildNode(g_nodeDescriptor);
                        Attorney::EditorGeneralWidget::registerUnsavedSceneChanges(_context.editor());
                    }

                    ImGui::CloseCurrentPopup();
                    _newPrimitiveType = ObjectType::COUNT;
                }
                if (g_nodeDescriptor._name.empty()) {
                    PopReadOnly();
                }
                ImGui::EndPopup();
            }
        }
    }
}

void MenuBar::drawFileMenu() {
    bool showSceneOpenDialog = false;
    bool showSceneSaveDialog = false;

    const auto saveSceneCbk = [this](const char* sceneName = "") {
        _savePopup = true;

        const ResourcePath parentFolder = splitPathToNameAndLocation(sceneName).first;
        g_saveSceneParams._saveNameOverride = parentFolder.c_str();
        g_saveSceneParams._closePopup = false;
        g_saveSceneParams._saveProgress = 0u;
        g_saveSceneParams._saveElementCount = Attorney::EditorGeneralWidget::saveItemCount(_context.editor());

        const auto messageCbk = [](const std::string_view msg) {
            g_saveSceneParams._saveMessage = msg;
            ++g_saveSceneParams._saveProgress;
        };

        const auto closeDialog = [this](const bool success) {
            Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), s_messages[success ? 1 : 2], Time::SecondsToMilliseconds<F32>(6));
            g_saveSceneParams._closePopup = true;
        };

        Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), s_messages[0], Time::SecondsToMilliseconds<F32>(6));
        if (!Attorney::EditorGeneralWidget::saveSceneChanges(_context.editor(), messageCbk, closeDialog, g_saveSceneParams._saveNameOverride)) {
            _errorMsg.append("Error occured while saving the current scene!\n Try again or check the logs for errors!\n");
        }
    };

    if (ImGui::BeginMenu("File"))
    {
        const bool hasUnsavedElements = Attorney::EditorGeneralWidget::hasUnsavedSceneChanges(_context.editor());
        const bool isDefaultScene = Attorney::EditorGeneralWidget::isDefaultScene(_context.editor());

        if (ImGui::MenuItem("New Scene", "Ctrl+N", false, true))
        {
            if (hasUnsavedElements && !isDefaultScene) {
                saveSceneCbk();
            }
            _newScenePopup = true;
        }

        showSceneOpenDialog = ImGui::MenuItem("Open Scene", "Ctrl+O");

        const auto& recentSceneList = Attorney::EditorMenuBar::getRecentSceneList(_context.editor());
        if (ImGui::BeginMenu("Open Recent", !recentSceneList.empty()))
        {
            for (size_t i = 0u; i < recentSceneList.size(); ++i) {
                if (ImGui::MenuItem(recentSceneList.get(i).c_str()))
                {
                    Attorney::EditorGeneralWidget::switchScene(_context.editor(), (g_scenePath + "/" + recentSceneList.get(i)).c_str());
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Save Scene", "", false, hasUnsavedElements || isDefaultScene)) {
            if (isDefaultScene) {
                showSceneSaveDialog = true;
            } else {
                saveSceneCbk();
            }
        }
        if (ImGui::MenuItem("Save Scene As", "", false, !isDefaultScene)) {
            showSceneSaveDialog = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Options"))
        {
            GFXDevice& gfx = _context.gfx();
            const Configuration& config = _context.config();
            const U8 maxMSAASamples = gfx.gpuState().maxMSAASampleCount();

            if (ImGui::BeginMenu("MSAA"))
            {
                for (U8 i = 0; 1 << i <= maxMSAASamples; ++i) {
                    const U8 sampleCount = i == 0u ? 0u : 1 << i;
                    if (sampleCount % 2 == 0) {
                        bool msaaEntryEnabled = config.rendering.MSAASamples == sampleCount;
                        if (ImGui::MenuItem(Util::StringFormat("%dx", to_U32(sampleCount)).c_str(), "", &msaaEntryEnabled))
                        {
                            gfx.setScreenMSAASampleCount(sampleCount);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            for (U8 type = 0; type < to_U8(ShadowType::COUNT); ++type) {
                const ShadowType sType = static_cast<ShadowType>(type);
                if (sType != ShadowType::CUBEMAP && 
                    ImGui::BeginMenu(Util::StringFormat("%s ShadowMap MSAA", Names::shadowType[type]).c_str()))
                {
                    const U8 currentCount = sType == ShadowType::LAYERED
                                                        ? config.rendering.shadowMapping.csm.MSAASamples
                                                        : config.rendering.shadowMapping.spot.MSAASamples;

                    for (U8 i = 0; 1 << i <= maxMSAASamples; ++i) {
                        const U8 sampleCount = i == 0u ? 0u : 1 << i;
                        if (sampleCount % 2 == 0) {
                            bool msaaEntryEnabled = currentCount == sampleCount;
                            if (ImGui::MenuItem(Util::StringFormat("%dx", to_U32(sampleCount)).c_str(), "", &msaaEntryEnabled)) {
                                gfx.setShadowMSAASampleCount(sType, sampleCount);
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            ImGui::EndMenu();
        }
        
        bool& options = Attorney::EditorMenuBar::optionWindowEnabled(_context.editor());
        ImGui::MenuItem("Editor options", "", &options);

        ImGui::Separator();
        if (ImGui::BeginMenu("Export Game"))
        {
            for (auto platform : Editor::g_supportedExportPlatforms) {
                if (ImGui::MenuItem(platform, "", false, true)) {
                    if (hasUnsavedElements) {
                        saveSceneCbk();
                    }
                    Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), Util::StringFormat("Exported game for [%s]!", platform), Time::SecondsToMilliseconds<F32>(3.0f));
                    break;
                }
            }
           
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Close Editor"))
        {
            if (hasUnsavedElements) {
                _closePopup = true;
            } else {
                _context.editor().toggle(false);
            }
        }

        if (ImGui::MenuItem("Quit", "Alt+F4"))
        {
            _quitPopup = true;
        }

        ImGui::EndMenu();
    }

    const char* sceneOpenPath = _sceneOpenDialog.chooseFolderDialog(showSceneOpenDialog, g_scenePath.c_str());
    if (strlen(sceneOpenPath) > 0) {
        Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), Util::StringFormat("Chosen scene load directory: \"%s\"", sceneOpenPath), Time::SecondsToMilliseconds<F32>(3.0f));
        Attorney::EditorGeneralWidget::switchScene(_context.editor(), sceneOpenPath);
    }

    const char* sceneSavePath = _sceneSaveDialog.chooseFolderDialog(showSceneSaveDialog, g_scenePath.c_str());
    if (strlen(sceneSavePath) > 0) {
        Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), Util::StringFormat("Chosen scene save directory: \"%s\"", sceneSavePath), Time::SecondsToMilliseconds<F32>(3.0f));
        saveSceneCbk(sceneSavePath);
    }
}

void MenuBar::drawEditMenu() const {
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "CTRL+Z", false, _context.editor().UndoStackSize() > 0))
        {
            if (!_context.editor().Undo()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "Undo failed!", Time::SecondsToMilliseconds<F32>(3.0f));
            }
        }

        if (ImGui::MenuItem("Redo", "CTRL+Y", false, _context.editor().RedoStackSize() > 0))
        {
            if (!_context.editor().Redo()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "Redo failed!", Time::SecondsToMilliseconds<F32>(3.0f));
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Cut", "CTRL+X", false, false))
        {
        }

        if (ImGui::MenuItem("Copy", "CTRL+C", false, false))
        {
        }

        if (ImGui::MenuItem("Paste", "CTRL+V", false, false))
        {
        }

        ImGui::Separator();
        ImGui::EndMenu();
    }
}

void MenuBar::drawProjectMenu() const {
    if (ImGui::BeginMenu("Project"))
    {
        if(ImGui::MenuItem("Configuration", "", false, false))
        {
        }

        ImGui::EndMenu();
    }
}
void MenuBar::drawObjectMenu() {
    if (ImGui::BeginMenu("Object"))
    {
        if (ImGui::BeginMenu("New Primitive")) 
        {
            if (ImGui::MenuItem("Sphere")) {
                g_nodeDescriptor = {};
                _newPrimitiveType = ObjectType::SPHERE_3D;
            }
            if (ImGui::MenuItem("Box")) {
                g_nodeDescriptor = {};
                _newPrimitiveType = ObjectType::BOX_3D;
            }
            if (ImGui::MenuItem("Plane")) {
                g_nodeDescriptor = {};
                _newPrimitiveType = ObjectType::QUAD_3D;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

}
void MenuBar::drawToolsMenu() {
    if (ImGui::BeginMenu("Tools"))
    {
        const bool memEditorEnabled = Attorney::EditorMenuBar::memoryEditorEnabled(_context.editor());
        if (ImGui::MenuItem("Memory Editor", nullptr, memEditorEnabled)) {
            Attorney::EditorMenuBar::toggleMemoryEditor(_context.editor(), !memEditorEnabled);
            if (!_context.editor().saveToXML()) {
                Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "Save failed!", Time::SecondsToMilliseconds<F32>(3.0f));
            }
        }

        if (ImGui::BeginMenu("Render Targets"))
        {
            const GFXRTPool& pool = _context.gfx().renderTargetPool();
            for (U8 i = 0; i < to_U8(RenderTargetUsage::COUNT); ++i) {
                const RenderTargetUsage usage = static_cast<RenderTargetUsage>(i);
                const auto& rTargets = pool.renderTargets(usage);

                if (rTargets.empty()) {
                    if (ImGui::MenuItem(UsageToString(usage), "", false, false))
                    {
                    }
                } else {
                    if (ImGui::BeginMenu(UsageToString(usage)))
                    {
                        for (const auto& rt : rTargets) {
                            if (rt == nullptr) {
                                continue;
                            }

                            if (ImGui::BeginMenu(rt->name().c_str()))
                            {
                                for (U8 j = 0; j < to_U8(RTAttachmentType::COUNT); ++j) {
                                    const RTAttachmentType type = static_cast<RTAttachmentType>(j);
                                    const U8 count = rt->getAttachmentCount(type);

                                    for (U8 k = 0; k < count; ++k) {
                                        const RTAttachment& attachment = rt->getAttachment(type, k);
                                        const Texture_ptr& tex = attachment.texture();
                                        if (tex == nullptr) {
                                            continue;
                                        }

                                        if (ImGui::MenuItem(tex->resourceName().c_str()))
                                        {
                                            _previewTextures.push_back(tex);
                                        }
                                    }
                                }

                                ImGui::EndMenu();
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawWindowsMenu() const {
    if (ImGui::BeginMenu("Window"))
    {
        bool& sampleWindowEnabled = Attorney::EditorMenuBar::sampleWindowEnabled(_context.editor());
        if (ImGui::MenuItem("Sample Window", nullptr, &sampleWindowEnabled)) {
            
        }
        ImGui::EndMenu();
    }

}

void MenuBar::drawPostFXMenu() const {
    if (ImGui::BeginMenu("PostFX"))
    {
        PostFX& postFX = _context.gfx().getRenderer().postFX();
        for (U16 i = 0u; i < to_base(FilterType::FILTER_COUNT); ++i) {
            const FilterType f = static_cast<FilterType>(i);

            bool filterEnabled = postFX.getFilterState(f);
            if (ImGui::MenuItem(PostFX::FilterName(f), nullptr, &filterEnabled)) {
                if (filterEnabled) {
                    postFX.pushFilter(f, true);
                } else {
                    postFX.popFilter(f, true);
                }
            }
        }
        ImGui::EndMenu();
    }
}

void MenuBar::drawDebugMenu() {
    if (ImGui::BeginMenu("Debug"))
    {
        if (ImGui::BeginMenu("BRDF Settings")) {
            const MaterialDebugFlag debugFlag = _context.gfx().materialDebugFlag();
            bool debug = false;
            for (U8 i = 0u; i < to_U8(MaterialDebugFlag::COUNT); ++i) {
                const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
                debug = debugFlag == flag;
                if (ImGui::MenuItem(TypeUtil::MaterialDebugFlagToString(flag), "", &debug)) {
                    _context.gfx().materialDebugFlag(debug ? flag : MaterialDebugFlag::COUNT);
                }
            }
            ImGui::EndMenu();
        }

        SceneEnvironmentProbePool* envProbPool = Attorney::EditorGeneralWidget::getActiveEnvProbePool(_context.editor());
        LightPool& pool = Attorney::EditorGeneralWidget::getActiveLightPool(_context.editor());
        ECSManager& ecsManager = Attorney::EditorGeneralWidget::getECSManager(_context.editor());

        if (ImGui::BeginMenu("Toggle Light Types")) {
            for (U8 i = 0; i < to_U8(LightType::COUNT); ++i) {
                const LightType type = static_cast<LightType>(i);

                bool state = pool.lightTypeEnabled(type);
                if (ImGui::MenuItem(TypeUtil::LightTypeToString(type), "", &state)) {
                    pool.toggleLightType(type, state);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Select Env Probe")) {
            constexpr U8 MaxProbesPerPage = 32;
            const auto PrintProbeEntry = [&envProbPool](const EnvironmentProbeList& probes, const size_t j) {
                EnvironmentProbeComponent* crtProbe = probes[j];
                bool selected = envProbPool->debugProbe() == crtProbe;
                if (ImGui::MenuItem(crtProbe->getSGN()->name().c_str(), "", &selected)) {
                    envProbPool->debugProbe(crtProbe);
                }
            };
            envProbPool->lockProbeList();
            const EnvironmentProbeList& probes = envProbPool->getLocked();
            const size_t probeCount = probes.size();
            if (probeCount > MaxProbesPerPage) {
                const size_t pageCount = probeCount > MaxProbesPerPage ? probeCount / MaxProbesPerPage : 1;
                const size_t remainder = probeCount > MaxProbesPerPage ? probeCount - pageCount * MaxProbesPerPage : 0;
                for (U8 p = 0; p < pageCount + 1; ++p) {
                    const size_t start = p * MaxProbesPerPage;
                    const size_t end = start + (p < pageCount ? MaxProbesPerPage : remainder);
                    if (ImGui::BeginMenu(Util::StringFormat("%d - %d", start, end).c_str())) {
                        for (size_t j = start; j < end; ++j) {
                            PrintProbeEntry(probes, j);
                        }
                        ImGui::EndMenu();
                    }
                }
            } else {
                for (size_t j = 0; j < probeCount; ++j) {
                    PrintProbeEntry(probes, j);
                }
            }
            envProbPool->unlockProbeList();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Select Debug light"))
        {
            constexpr U8 MaxLightsPerPage = 32;
            const auto PrintLightEntry = [&pool](const LightPool::LightList& lights, const size_t j) {
                Light* crtLight = lights[j];
                bool selected = pool.debugLight() == crtLight;
                if (ImGui::MenuItem(crtLight->getSGN()->name().c_str(), "", &selected)) {
                    pool.debugLight(crtLight);
                }
            };

            for (U8 i = 0; i < to_U8(LightType::COUNT); ++i) {
                const LightType type = static_cast<LightType>(i);
                const LightPool::LightList& lights = pool.getLights(type);
                if (!lights.empty()) {
                    const size_t lightCount = lights.size();

                    if (ImGui::BeginMenu(TypeUtil::LightTypeToString(type))) {
                        if (lightCount > MaxLightsPerPage) {
                            const size_t pageCount = lightCount > MaxLightsPerPage ? lightCount / MaxLightsPerPage : 1;
                            const size_t remainder = lightCount > MaxLightsPerPage ? lightCount - pageCount * MaxLightsPerPage : 0;
                            for (U8 p = 0; p < pageCount + 1; ++p) {
                                const size_t start = p * MaxLightsPerPage;
                                const size_t end = start + (p < pageCount ? MaxLightsPerPage : remainder);
                                if (ImGui::BeginMenu(Util::StringFormat("%d - %d", start, end).c_str())) {
                                    for (size_t j = start; j < end; ++j) {
                                        PrintLightEntry(lights, j);
                                    }
                                    ImGui::EndMenu();
                                }
                            }
                        } else {
                            for (size_t j = 0; j < lightCount; ++j) {
                                PrintLightEntry(lights, j);
                            }
                        }

                        ImGui::EndMenu();
                    }
                } else {
                    ImGui::Text(TypeUtil::LightTypeToString(type));
                }
            }
            if (ImGui::BeginMenu("Shadow enabled")) {
                for (U8 i = 0; i < to_U8(LightType::COUNT); ++i) {
                    const LightType type = static_cast<LightType>(i);
                    const LightPool::LightList& lights = pool.getLights(type);
                    for (Light* crtLight : lights)  {
                        if (crtLight->castsShadows()) {
                            bool selected = pool.debugLight() == crtLight;
                            if (ImGui::MenuItem(crtLight->getSGN()->name().c_str(), "", &selected)) {
                                pool.debugLight(crtLight);
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        bool lightImpostors = pool.lightImpostorsEnabled();
        if (ImGui::MenuItem("Draw Light Impostors", "", &lightImpostors))
        {
            pool.lightImpostorsEnabled(lightImpostors);
        }
        
        bool playAnimations = ecsManager.getSystem<AnimationSystem>()->getAnimationState();

        if (ImGui::MenuItem("Play animations", "", &playAnimations)) {
            ecsManager.getSystem<AnimationSystem>()->toggleAnimationState(playAnimations);
        }

        if (ImGui::BeginMenu("Debug Gizmos")) {
            SceneManager* sceneManager = context().kernel().sceneManager();
            SceneRenderState& renderState = sceneManager->getActiveScene().state()->renderState();

            bool temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_AABB);
            if (ImGui::MenuItem("Show AABBs", "", &temp)) {
                Console::d_printfn(Locale::Get(_ID("TOGGLE_SCENE_BOUNDING_BOXES")), temp ? "On" : "Off");
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_AABB, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_OBB);
            if (ImGui::MenuItem("Show OBBs", "", &temp)) {
                Console::d_printfn(Locale::Get(_ID("TOGGLE_SCENE_ORIENTED_BOUNDING_BOXES")), temp ? "On" : "Off");
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_OBB, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_BSPHERES);
            if (ImGui::MenuItem("Show bounding spheres", "", &temp)) {
                Console::d_printfn(Locale::Get(_ID("TOGGLE_SCENE_BOUNDING_SPHERES")), temp ? "On" : "Off");
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_BSPHERES, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_SKELETONS);
            if (ImGui::MenuItem("Show skeletons", "", &temp)) {
                Console::d_printfn(Locale::Get(_ID("TOGGLE_SCENE_SKELETONS")), temp ? "On" : "Off");
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_SKELETONS, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::SCENE_GIZMO);
            if (ImGui::MenuItem("Show scene axis", "", &temp)) {
                Console::d_printfn(Locale::Get(_ID("TOGGLE_SCENE_AXIS_GIZMO")));
                renderState.toggleOption(SceneRenderState::RenderOptions::SCENE_GIZMO, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::SELECTION_GIZMO);
            if (ImGui::MenuItem("Show selection axis", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::SELECTION_GIZMO, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::ALL_GIZMOS);
            if (ImGui::MenuItem("Show all axis", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::ALL_GIZMOS, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY);
            if (ImGui::MenuItem("Render geometry", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_GEOMETRY, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME);
            if (ImGui::MenuItem("Render wireframe", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_WIREFRAME, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_OCTREE_REGIONS);
            if (ImGui::MenuItem("Render octree regions", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_OCTREE_REGIONS, temp);
            }
            temp = renderState.isEnabledOption(SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES);
            if (ImGui::MenuItem("Render custom gismos", "", &temp)) {
                renderState.toggleOption(SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES, temp);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edge Detection Method")) {

            PreRenderBatch& batch = _context.gfx().getRenderer().postFX().getFilterBatch();
            bool noneSelected = batch.edgeDetectionMethod() == PreRenderBatch::EdgeDetectionMethod::COUNT;
            if (ImGui::MenuItem("None", "", &noneSelected)) {
                batch.edgeDetectionMethod(PreRenderBatch::EdgeDetectionMethod::COUNT);
            }

            for (U8 i = 0; i < to_U8(PreRenderBatch::EdgeDetectionMethod::COUNT) + 1; ++i) {
                PreRenderBatch::EdgeDetectionMethod method = static_cast<PreRenderBatch::EdgeDetectionMethod>(i);

                bool selected = batch.edgeDetectionMethod() == method;
                if (ImGui::MenuItem(EdgeMethodName(method), "", &selected)) {
                    batch.edgeDetectionMethod(method);
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug Views"))
        {
            vector<std::tuple<string, I16, I16, bool>> viewNames = {};
            _context.gfx().getDebugViewNames(viewNames);

            eastl::set<I16> groups = {};
            for (auto[name, groupID, index, enabled] : viewNames) {
                if (groupID != -1) {
                    groups.insert(groupID);
                }

                const string label = groupID == -1 ? name : Util::StringFormat("(%d) %s", groupID, name.c_str());
                if (ImGui::MenuItem(label.c_str(), "", &enabled)) {
                    _context.gfx().toggleDebugView(index, enabled);
                }
            }
            if (!groups.empty()) {
                ImGui::Separator();
                for (const I16 group : groups) {
                    bool groupEnabled = _context.gfx().getDebugGroupState(group);
                    if (ImGui::MenuItem(Util::StringFormat("Enable Group [ %d ]", group).c_str(), "", &groupEnabled)) {
                        _context.gfx().toggleDebugGroup(group, groupEnabled);
                    }
                }
            }
            ImGui::EndMenu();
        }

        bool state = context().gui().showDebugCursor();
        if (ImGui::MenuItem("Show CEGUI Debug Cursor", "", &state)) {
            context().gui().showDebugCursor(state);
        }

        ImGui::EndMenu();
    }
}

void MenuBar::drawHelpMenu() const {
    if (ImGui::BeginMenu("Help"))
    {
        bool& sampleWindowEnabled = Attorney::EditorMenuBar::sampleWindowEnabled(_context.editor());
        if (ImGui::MenuItem("Sample Window", nullptr, &sampleWindowEnabled)) {

        }
        ImGui::Separator();

        ImGui::Text("Copyright(c) 2018 DIVIDE - Studio");
        ImGui::Text("Copyright(c) 2009 Ionut Cava");
        ImGui::EndMenu();
    }
}
} //namespace Divide