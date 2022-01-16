#include "stdafx.h"

#include "Headers/PropertyWindow.h"
#include "Headers/UndoManager.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

#include "Editor/Headers/Editor.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Geometry/Material/Headers/Material.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"

#include <imgui_internal.h>

#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Headers/Utils.h"
#include <Editor/Widgets/Headers/ImGuiExtensions.h>
#include <IconFontCppHeaders/IconsForkAwesome.h>

namespace Divide {
    namespace {
        hashMap<U64, std::tuple<Frustum, FColour3, bool>> g_debugFrustums;
        std::array<U64, 1024> s_openProperties = {};

        // Separate activate is used for stuff that do continuous value changes, e.g. colour selectors, but you only want to register the old val once
        template<typename T, bool SeparateActivate, typename Pred>
        void RegisterUndo(Editor& editor, GFX::PushConstantType Type, const T& oldVal, const T& newVal, const char* name, Pred&& dataSetter) {
            static hashMap<U64, UndoEntry<T>> _undoEntries;
            UndoEntry<T>& undo = _undoEntries[_ID(name)];
            if (!SeparateActivate || ImGui::IsItemActivated()) {
                undo._oldVal = oldVal;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                undo._type = Type;
                undo._name = name;
                undo._newVal = newVal;
                undo._dataSetter = dataSetter;
                editor.registerUndoEntry(undo);
            }
        }

        bool IsRequiredComponentType(SceneGraphNode* selection, const ComponentType componentType) {
            if (selection != nullptr) {
                return BitCompare(selection->getNode().requiredComponentMask(), to_U32(componentType));
            }

            return false;
        }
        template<typename Pred>
        void ApplyToMaterials(const Material& baseMaterial, Material* instanceRoot, Pred&& predicate) {
            if (instanceRoot != nullptr) {
                predicate(baseMaterial, instanceRoot);

                const auto& instances = instanceRoot->getInstances();
                for (Material* mat : instances) {
                    ApplyToMaterials(baseMaterial, mat, predicate);
                }
            }
        }

        template<typename Pred>
        void ApplyAllButton(I32 &id, const bool readOnly, const Material& material, Pred&& predicate) {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 40);
            ImGui::PushID(4321234 + id++);
            if (readOnly) {
                PushReadOnly();
            }
            if (ImGui::SmallButton("A")) {
                ApplyToMaterials(material, material.baseMaterial(), MOV(predicate));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Apply to all instances");
            }
            if (readOnly) {
                PopReadOnly();
            }
            ImGui::PopID();
        }

        bool PreviewTextureButton(I32 &id, Texture* tex, const bool readOnly) {
            bool ret = false;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 15);
            ImGui::PushID(4321234 + id++);
            if (readOnly) {
                PushReadOnly();
            }
            if (ImGui::SmallButton("T")) {
                ret = true;
            }
            if (tex != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(Util::StringFormat("Preview texture : %s", tex->assetName().c_str()).c_str());
            }
            if (readOnly) {
                PopReadOnly();
            }
            ImGui::PopID();
            return ret;
        }
    }

    PropertyWindow::PropertyWindow(Editor& parent, PlatformContext& context, const Descriptor& descriptor)
        : DockedWindow(parent, descriptor),
          PlatformContextComponent(context)
    {
    }

    void PropertyWindow::onRemoveComponent(const EditorComponent& comp) {
        if (_lockedComponent._editorComp == nullptr) {
            return;
        }

        if (comp.getGUID() == _lockedComponent._editorComp->getGUID()) {
            _lockedComponent = { nullptr, nullptr };
        }
    }

    bool PropertyWindow::drawCamera(Camera* cam) {
        bool sceneChanged = false;
        if (cam == nullptr) {
            return false;
        }

        const char* camName = cam->resourceName().c_str();
        const U64 camID = _ID(camName);
        ImGui::PushID(to_I32(camID) * 54321);

        if (ImGui::CollapsingHeader(camName)) {
            Util::PushNarrowLabelWidth();
            {
                vec3<F32> eye = cam->getEye();
                EditorComponentField camField = {};
                camField._name = "Eye";
                camField._basicType = GFX::PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = eye._v;
                camField._dataSetter = [cam](const void* val) noexcept {
                    cam->setEye(*static_cast<const vec3<F32>*>(val));
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                constexpr char* CamRotateLabels[] = {
                    "P", "Y", "R"
                };

                vec3<F32> euler = cam->getEuler();
                EditorComponentField camField = {};
                camField._name = "Euler";
                camField._labels = CamRotateLabels;
                camField._tooltip = "Change camera orientation using euler angles( degrees).\nP = Pitch, Y = Yaw, R = Roll";
                camField._basicType = GFX::PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = euler._v;
                camField._dataSetter = [cam](const void* e) noexcept {
                    cam->setEuler(*static_cast<const vec3<F32>*>(e));
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            if (cam->type() != Camera::CameraType::STATIC &&
                cam->type() != Camera::CameraType::SCRIPTED)
            {
                constexpr char* CamSpeedLabels[] = {
                    "T", "M", "Z"
                };

                FreeFlyCamera* flyCam = static_cast<FreeFlyCamera*>(cam);
                vec3<F32> speed;
                speed.move = flyCam->getMoveSpeedFactor();
                speed.turn = flyCam->getTurnSpeedFactor();
                speed.zoom = flyCam->getZoomSpeedFactor();
                
                EditorComponentField camField = {};
                camField._name = "Speed";
                camField._range = { 0.001f, 100.f };
                camField._labels = CamSpeedLabels;
                camField._basicType = GFX::PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::SLIDER_TYPE;
                camField._readOnly = false;
                camField._tooltip = "Change camera speed factor.\nT = Turn speed, M = Move speed, Z = Zoom speed";
                camField._data = speed._v;
                camField._dataSetter = [flyCam](const void* e) noexcept {
                    const vec3<F32> speed = *static_cast<const vec3<F32>*>(e);
                    flyCam->setMoveSpeedFactor(speed.move);
                    flyCam->setTurnSpeedFactor(speed.turn);
                    flyCam->setZoomSpeedFactor(speed.zoom);
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                vec3<F32> fwd = cam->getForwardDir();
                EditorComponentField camField = {};
                camField._name = "Forward";
                camField._basicType = GFX::PushConstantType::VEC3;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = true;
                camField._data = fwd._v;
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                F32 aspect = cam->getAspectRatio();
                EditorComponentField camField = {};
                camField._name = "Aspect";
                camField._basicType = GFX::PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &aspect;
                camField._dataSetter = [cam](const void* a) noexcept {
                    cam->setAspectRatio(*static_cast<const F32*>(a));
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                F32 horizontalFoV = cam->getHorizontalFoV();
                EditorComponentField camField = {};
                camField._name = "FoV (horizontal)";
                camField._basicType = GFX::PushConstantType::FLOAT;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = &horizontalFoV;
                camField._dataSetter = [cam](const void* fov) noexcept {
                    cam->setHorizontalFoV(*static_cast<const F32*>(fov));
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                vec2<F32> zPlanes = cam->getZPlanes();
                EditorComponentField camField = {};
                camField._name = "zPlanes";
                camField._basicType = GFX::PushConstantType::VEC2;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = zPlanes._v;
                camField._dataSetter = [cam](const void* planes) {
                    if (cam->isOrthoProjected()) {
                        cam->setProjection(cam->orthoRect(), *static_cast<const vec2<F32>*>(planes));
                    } else {
                        cam->setProjection(cam->getAspectRatio(), cam->getVerticalFoV(), *static_cast<const vec2<F32>*>(planes));
                    }
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            if (cam->isOrthoProjected()) {
                vec4<F32> orthoRect = cam->orthoRect();
                EditorComponentField camField = {};
                camField._name = "Ortho";
                camField._basicType = GFX::PushConstantType::VEC4;
                camField._type = EditorComponentFieldType::PUSH_TYPE;
                camField._readOnly = false;
                camField._data = orthoRect._v;
                camField._dataSetter = [cam](const void* rect) {
                    cam->setProjection(*static_cast<const vec4<F32>*>(rect), cam->getZPlanes());
                };
                sceneChanged = processField(camField) || sceneChanged;
            }
            {
                mat4<F32> viewMatrix = cam->viewMatrix();
                EditorComponentField worldMatrixField = {};
                worldMatrixField._name = "View Matrix";
                worldMatrixField._basicType = GFX::PushConstantType::MAT4;
                worldMatrixField._type = EditorComponentFieldType::PUSH_TYPE;
                worldMatrixField._readOnly = true;
                worldMatrixField._data = &viewMatrix;
                if (processBasicField(worldMatrixField)) {
                    // Value changed
                }
            }
            {
                mat4<F32> projMatrix = cam->projectionMatrix();
                EditorComponentField projMatrixField;
                projMatrixField._basicType = GFX::PushConstantType::MAT4;
                projMatrixField._type = EditorComponentFieldType::PUSH_TYPE;
                projMatrixField._readOnly = true;
                projMatrixField._name = "Projection Matrix";
                projMatrixField._data = &projMatrix;
                if (processBasicField(projMatrixField)) {
                    // Value changed
                }
            }
            {
                ImGui::Separator();
                bool drawFustrum = g_debugFrustums.find(camID) != eastl::cend(g_debugFrustums);
                ImGui::PushID(to_I32(camID) * 123456);
                if (ImGui::Checkbox("Draw debug frustum", &drawFustrum)) {
                    if (drawFustrum) {
                        g_debugFrustums[camID] = { cam->getFrustum(), DefaultColours::GREEN, true };
                    } else {
                        g_debugFrustums.erase(camID);
                    }
                }
                if (drawFustrum) {
                    auto& [frust, colour, realtime] = g_debugFrustums[camID];
                    ImGui::Checkbox("Update realtime", &realtime);
                    const bool update = realtime || ImGui::Button("Update frustum");
                    if (update) {
                        frust = cam->getFrustum();
                    }
                    ImGui::PushID(cam->resourceName().c_str());
                    ImGui::ColorEdit3("Frust Colour", colour._v, ImGuiColorEditFlags__OptionsDefault);
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            Util::PopNarrowLabelWidth();
        }
        ImGui::PopID();

        return sceneChanged;
    }

    void PropertyWindow::backgroundUpdateInternal() {
        I64 guid = 12344231;
        for (const auto& it : g_debugFrustums) {
            const auto& [frustum, colour, realtime] = it.second;
            IMPrimitive::FrustumDescriptor descriptor;
            descriptor.frustum = frustum;
            descriptor.colour = Util::ToByteColour(colour);
            _context.gfx().debugDrawFrustum(guid++, descriptor);
        }
    }

    bool PropertyWindow::printComponent(SceneGraphNode* sgnNode, EditorComponent* comp, const F32 xOffset, const F32 smallButtonWidth) {
        bool sceneChanged = false;

        bool isLockedField = false;
        bool fieldWasOpen = false;

        if (_lockedComponent._editorComp != nullptr && _lockedComponent._editorComp->getGUID() == comp->getGUID()) {
            fieldWasOpen = true;
            isLockedField = true;
        }

        const string fieldNameStr = fieldWasOpen ? Util::StringFormat("%s (%s)", comp->name().c_str(), _lockedComponent._parentSGN->name().c_str()) : comp->name().c_str();
        const char* fieldName = fieldNameStr.c_str();
        const U64 fieldHash = _ID(fieldName);
        if (!isLockedField) {
            for (const U64 p : s_openProperties) {
                if (p == fieldHash) {
                    fieldWasOpen = true;
                    break;
                }
            }
        }
        if (comp->fields().empty()) {
            PushReadOnly();
            ImGui::CollapsingHeader(fieldName, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet);
            PopReadOnly();
        } else {
            if (ImGui::CollapsingHeader(fieldName, ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | (fieldWasOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0u))) {
                if (!fieldWasOpen) {
                    for (U64& p : s_openProperties) {
                        if (p == 0u) {
                            p = fieldHash;
                            break;
                        }
                    }
                }

                ImGui::NewLine();
                ImGui::SameLine(xOffset);
                if (ImGui::Button(ICON_FK_SEARCH" INSPECT", ImVec2(smallButtonWidth, 20))) {
                    Attorney::EditorGeneralWidget::inspectMemory(_context.editor(), std::make_pair(comp, sizeof(EditorComponent)));
                }
                if (!isLockedField && comp->parentComponentType() != ComponentType::COUNT && !IsRequiredComponentType(sgnNode, comp->parentComponentType())) {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FK_MINUS" REMOVE", ImVec2(smallButtonWidth, 20))) {
                        Attorney::EditorGeneralWidget::inspectMemory(_context.editor(), std::make_pair(nullptr, 0));

                        if (Attorney::EditorGeneralWidget::removeComponent(_context.editor(), sgnNode, comp->parentComponentType())) {
                            sceneChanged = true;
                            return true;
                        }
                    }
                }
                ImGui::SameLine(ImGui::GetWindowSize().x - 80.f);
                bool fieldLocked = _lockedComponent._editorComp != nullptr && _lockedComponent._editorComp->getGUID() == comp->getGUID();
                ImGui::PushID(to_I32(fieldHash));
                if (ImGui::Checkbox(ICON_FK_LOCK"  ", &fieldLocked)) {
                    if (!fieldLocked) {
                        _lockedComponent = { nullptr, nullptr };
                    } else {
                        _lockedComponent = { comp, sgnNode };
                    }
                }
                ImGui::PopID();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Always keep this component visible in the editor regardless of the currently selected scene node");
                    skipAutoTooltip(true);
                }
                ImGui::Separator();
                vector<EditorComponentField>& fields = Attorney::EditorComponentEditor::fields(*comp);
                for (EditorComponentField& field : fields) {
                    if (processField(field) && !field._readOnly) {
                        Attorney::EditorComponentEditor::onChanged(*comp, field);
                        sceneChanged = true;
                    }
                    ImGui::Spacing();
                }
                const U32 componentMask = sgnNode->componentMask();
                if (BitCompare(componentMask, ComponentType::ENVIRONMENT_PROBE)) {
                    const EnvironmentProbeComponent* probe = sgnNode->get<EnvironmentProbeComponent>();
                    if (probe != nullptr) {
                        const auto& cameras = probe->probeCameras();

                        for (U8 face = 0u; face < 6u; ++face) {
                            Camera* probeCameras = cameras[face];
                            if (drawCamera(probeCameras)) {
                                sceneChanged = true;
                            }
                        }
                    }
                }
                Light* light = nullptr;
                if (BitCompare(componentMask, ComponentType::SPOT_LIGHT)) {
                    light = sgnNode->get<SpotLightComponent>();
                } else if (BitCompare(componentMask, ComponentType::POINT_LIGHT)) {
                    light = sgnNode->get<PointLightComponent>();
                } else if (BitCompare(componentMask, ComponentType::DIRECTIONAL_LIGHT)) {
                    light = sgnNode->get<DirectionalLightComponent>();
                }
                if (light != nullptr) {
                    if (light->castsShadows()) {
                        if (ImGui::CollapsingHeader("Light Shadow Settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                            ImGui::Text("Shadow Offset: %d", to_U32(light->getShadowOffset()));

                            switch (light->getLightType()) {
                                case LightType::POINT: {
                                    for (U8 face = 0u; face < 6u; ++face) {
                                        Camera* shadowCamera = ShadowMap::shadowCameras(ShadowType::CUBEMAP)[face];
                                        if (drawCamera(shadowCamera)) {
                                            sceneChanged = true;
                                        }
                                    }
                                                
                                } break;

                                case LightType::SPOT: {
                                    Camera* shadowCamera = ShadowMap::shadowCameras(ShadowType::SINGLE).front();
                                    if (drawCamera(shadowCamera)) {
                                        sceneChanged = true;
                                    }
                                } break;

                                case LightType::DIRECTIONAL: {
                                    DirectionalLightComponent* dirLight = static_cast<DirectionalLightComponent*>(light);
                                    for (U8 split = 0u; split < dirLight->csmSplitCount(); ++split) {
                                        Camera* shadowCamera = ShadowMap::shadowCameras(ShadowType::LAYERED)[split];
                                        if (drawCamera(shadowCamera)) {
                                            sceneChanged = true;
                                        }
                                    }
                                } break;

                                case LightType::COUNT: {
                                    DIVIDE_UNEXPECTED_CALL();
                                } break;
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader("Scene Shadow Settings", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                        SceneManager* sceneManager = context().kernel().sceneManager();
                        auto& activeSceneState = sceneManager->getActiveScene().state();

                        {
                            F32 bleedBias = activeSceneState->lightBleedBias();
                            EditorComponentField tempField = {};
                            tempField._name = "Light bleed bias";
                            tempField._basicType = GFX::PushConstantType::FLOAT;
                            tempField._type = EditorComponentFieldType::PUSH_TYPE;
                            tempField._readOnly = false;
                            tempField._data = &bleedBias;
                            tempField._format = "%.6f";
                            tempField._range = { 0.0f, 1.0f };
                            tempField._dataSetter = [&activeSceneState](const void* bias) noexcept {
                                activeSceneState->lightBleedBias(*static_cast<const F32*>(bias));
                            };
                            sceneChanged = processField(tempField) || sceneChanged;
                        }
                        {
                            F32 shadowVariance = activeSceneState->minShadowVariance();
                            EditorComponentField tempField = {};
                            tempField._name = "Minimum variance";
                            tempField._basicType = GFX::PushConstantType::FLOAT;
                            tempField._type = EditorComponentFieldType::PUSH_TYPE;
                            tempField._readOnly = false;
                            tempField._data = &shadowVariance;
                            tempField._range = { 0.00001f, 0.99999f };
                            tempField._format = "%.6f";
                            tempField._dataSetter = [&activeSceneState](const void* variance) noexcept {
                                activeSceneState->minShadowVariance(*static_cast<const F32*>(variance));
                            };
                            sceneChanged = processField(tempField) || sceneChanged;
                        }
                    }
                }
            } else {
                for (U64& p : s_openProperties) {
                    if (p == fieldHash) {
                        p = 0u;
                        break;
                    }
                }
            }
        }
        return sceneChanged;
    }

    void PropertyWindow::drawInternal() {
        constexpr F32 buttonWidth = 90.0f;
        constexpr F32 smallButtonWidth = 70.0f;

        skipAutoTooltip(false);

        bool sceneChanged = false;
        F32 xOffset = ImGui::GetWindowSize().x * 0.5f - (smallButtonWidth * 2);
        if (_lockedComponent._editorComp) {
            sceneChanged = printComponent(_lockedComponent._parentSGN, _lockedComponent._editorComp, xOffset, smallButtonWidth);
        }

        const Selections crtSelections = selections();
        const bool hasSelections = crtSelections._selectionCount > 0u;
        
        bool lockSolutionExplorer = false;
        Camera* selectedCamera = Attorney::EditorPropertyWindow::getSelectedCamera(_parent);
        if (selectedCamera != nullptr) {
            sceneChanged = drawCamera(selectedCamera);
        } else if (hasSelections) {
            for (U8 i = 0u; i < crtSelections._selectionCount; ++i) {
                SceneGraphNode* sgnNode = node(crtSelections._selections[i]);
                if (sgnNode != nullptr) {
                    ImGui::PushID(sgnNode->name().c_str());

                    bool enabled = sgnNode->hasFlag(SceneGraphNode::Flags::ACTIVE);
                    if (ImGui::Checkbox(Util::StringFormat("%s %s", getIconForNode(sgnNode), sgnNode->name().c_str()).c_str(), &enabled)) {
                        if (enabled) {
                            sgnNode->setFlag(SceneGraphNode::Flags::ACTIVE);
                        } else {
                            sgnNode->clearFlag(SceneGraphNode::Flags::ACTIVE);
                        }
                        sceneChanged = true;
                    }

                    ImGui::SameLine(ImGui::GetWindowSize().x - 80.f);
                    bool selectionLocked = sgnNode->hasFlag(SceneGraphNode::Flags::SELECTION_LOCKED);
                    if (ImGui::Checkbox(ICON_FK_LOCK"  ", &selectionLocked)) {
                        if (selectionLocked) {
                            sgnNode->setFlag(SceneGraphNode::Flags::SELECTION_LOCKED);
                        } else {
                            sgnNode->clearFlag(SceneGraphNode::Flags::SELECTION_LOCKED);
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When ticked, prevents selection of a different node");
                        skipAutoTooltip(true);
                    }
                    if (selectionLocked) {
                        lockSolutionExplorer = true;
                    }
                    ImGui::Separator();

                    // Root
                    if (sgnNode->parent() == nullptr) {
                        ImGui::Separator();
                    }

                    vector_fast<EditorComponent*>& editorComp = Attorney::SceneGraphNodeEditor::editorComponents(sgnNode);
                    for (EditorComponent* comp : editorComp) {
                        if (_lockedComponent._editorComp != nullptr && _lockedComponent._editorComp->getGUID() == comp->getGUID()) {
                            continue;
                        }
                        sceneChanged = printComponent(sgnNode, comp, xOffset, smallButtonWidth) || sceneChanged;
                    }
                    ImGui::Separator();
                    ImGui::NewLine();
                    ImGui::NewLine();
                    ImGui::SameLine(ImGui::GetWindowSize().x * 0.5f - (buttonWidth * 0.5f));
                    if (ImGui::Button(ICON_FK_FLOPPY_O" Save Node", ImVec2(buttonWidth, 15))) {
                        Attorney::EditorPropertyWindow::saveNode(_parent, sgnNode);
                    }
                }
                ImGui::PopID();
            }

            //ToDo: Speed this up. Also, how do we handle adding stuff like RenderingComponents and creating materials and the like?
            const auto validComponentToAdd = [this, &crtSelections](const ComponentType type) -> bool {
                if (type == ComponentType::COUNT) {
                    return false;
                }

                if (type == ComponentType::SCRIPT) {
                    return true;
                }

                bool missing = false;
                for (U8 i = 0u; i < crtSelections._selectionCount; ++i) {
                    const SceneGraphNode* sgn = node(crtSelections._selections[i]);
                    if (sgn != nullptr && !BitCompare(sgn->componentMask(), to_U32(type))) {
                        missing = true;
                        break;
                    }
                }

                return missing;
            };

            xOffset = ImGui::GetWindowSize().x - buttonWidth - 20.0f;
            ImGui::NewLine();
            ImGui::Separator();
            ImGui::NewLine();
            ImGui::SameLine(xOffset);
            if (ImGui::Button(ICON_FK_PLUS" ADD NEW", ImVec2(buttonWidth, 15))) {
                ImGui::OpenPopup("COMP_SELECTION_GROUP");
            }
            static ComponentType selectedType = ComponentType::COUNT;

            if (ImGui::BeginPopup("COMP_SELECTION_GROUP")) {
                for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i) {
                    const U32 componentBit = 1 << i;
                    const ComponentType type = static_cast<ComponentType>(componentBit);
                    if (type == ComponentType::COUNT || !validComponentToAdd(type)) {
                        continue;
                    }

                    if (ImGui::Selectable(TypeUtil::ComponentTypeToString(type))) {
                        selectedType = type;
                    }
                }
                ImGui::EndPopup();
            }
            if (selectedType != ComponentType::COUNT) {
                ImGui::OpenPopup("Add new component");
            }

            if (ImGui::BeginPopupModal("Add new component", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Add new %s component?", TypeUtil::ComponentTypeToString(selectedType));
                ImGui::Separator();

                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    if (Attorney::EditorGeneralWidget::addComponent(_context.editor(), crtSelections, selectedType)) {
                        sceneChanged = true;
                    }
                    selectedType = ComponentType::COUNT;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    selectedType = ComponentType::COUNT;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::Separator();
            ImGui::Text("Please select a scene node \n to inspect its properties");
            ImGui::Separator();
        }

        if (_previewTexture != nullptr) {
            if (Attorney::EditorGeneralWidget::modalTextureView(_context.editor(), Util::StringFormat("Image Preview: %s", _previewTexture->resourceName().c_str()).c_str(), _previewTexture, vec2<F32>(512, 512), true, false)) {
                _previewTexture = nullptr;
            }
        }

        Attorney::EditorPropertyWindow::lockSolutionExplorer(_parent, lockSolutionExplorer);

        if (sceneChanged) {
            Attorney::EditorGeneralWidget::registerUnsavedSceneChanges(_context.editor());
        }
    }
    
    const Selections& PropertyWindow::selections() const {
        const Scene& activeScene = context().kernel().sceneManager()->getActiveScene();
        return activeScene.getCurrentSelection();
    }
    
    SceneGraphNode* PropertyWindow::node(const I64 guid) const {
        const Scene& activeScene = context().kernel().sceneManager()->getActiveScene();

        return activeScene.sceneGraph()->findNode(guid);
    }

    bool PropertyWindow::processField(EditorComponentField& field) {
        if (field._labels == nullptr) {
            field._labels = Util::FieldLabels;
        }

        const auto printFieldName = [&field]() { ImGui::Text("[%s]", field._name.c_str()); };
        bool ret = false;
        switch (field._type) {
            case EditorComponentFieldType::SEPARATOR: {
                printFieldName();
                ImGui::Separator();
            }break;
            case EditorComponentFieldType::BUTTON: {
                if (field._readOnly) {
                    PushReadOnly();
                }
                if (field._range.y - field._range.x > 1.0f) {
                    ret = ImGui::Button(field._name.c_str(), ImVec2(field._range.x, field._range.y));
                } else {
                    ret = ImGui::Button(field._name.c_str());
                }
                if (field._readOnly) {
                    PopReadOnly();
                }
            }break;
            case EditorComponentFieldType::SLIDER_TYPE:
            case EditorComponentFieldType::PUSH_TYPE:
            case EditorComponentFieldType::SWITCH_TYPE: {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                {
                    ret = processBasicField(field);
                }ImGui::PopItemWidth();
            } break;
            case EditorComponentFieldType::DROPDOWN_TYPE: {
                if (field._readOnly) {
                    PushReadOnly();
                }

                printFieldName();
                const U8 entryStart = to_U8(field._range.min);
                const U8 entryCount = to_U8(field._range.max);
                static UndoEntry<I32> typeUndo = {};
                if (entryCount > 0 && entryStart <= entryCount) {
                    ImGui::PushID(field._name.c_str());

                    const U8 crtMode = field.get<U8>();
                    ret = ImGui::BeginCombo("", field.getDisplayName(crtMode));
                    if (ret) {
                        for (U8 n = entryStart; n < entryCount; ++n) {
                            const bool isSelected = crtMode == n;
                            if (ImGui::Selectable(field.getDisplayName(n), isSelected)) {
                                typeUndo._type = GFX::PushConstantType::UINT;
                                typeUndo._name = "Drop Down Selection";
                                typeUndo._oldVal = to_U32(crtMode);
                                typeUndo._newVal = to_U32(n);
                                typeUndo._dataSetter = [&field](const U32& data) {
                                    field.set(to_U8(data));
                                };

                                field.set(n);
                                _context.editor().registerUndoEntry(typeUndo);
                                break;
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                if (field._readOnly) {
                    PopReadOnly();
                }
            }break;
            case EditorComponentFieldType::BOUNDING_BOX: {
                printFieldName();
                BoundingBox bb = {};
                field.get<BoundingBox>(bb);

                F32* bbMin = Attorney::BoundingBoxEditor::min(bb);
                F32* bbMax = Attorney::BoundingBoxEditor::max(bb);
                vec3<F32> halfExtent = bb.getHalfExtent();
                vec3<F32> bbCenter = bb.getCenter();
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Min ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._data = bbMin;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._dataSetter = [&field](const void* val) {
                        BoundingBox aabb = {};
                        field.get<BoundingBox>(aabb);
                        aabb.setMin(*static_cast<const vec3<F32>*>(val));
                        field.set<BoundingBox>(aabb);
                    };
                    ret = processField(bbField) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Max ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._data = bbMax;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._dataSetter = [&field](const void* val) {
                        BoundingBox aabb = {};
                        field.get<BoundingBox>(aabb);
                        aabb.setMax(*static_cast<const vec3<F32>*>(val));
                        field.set<BoundingBox>(aabb);
                    };
                    ret = processField(bbField) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Half Extents ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._data = halfExtent;
                    bbField._hexadecimal = field._hexadecimal;
                    ret = processField(bbField) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._data = bbCenter;
                    bbField._hexadecimal = field._hexadecimal;
                    ret = processField(bbField) || ret;
                }
            }break;
            case EditorComponentFieldType::ORIENTED_BOUNDING_BOX: {
                printFieldName();
                OBB obb = {};
                field.get<OBB>(obb);
                vec3<F32> position = obb.position();
                vec3<F32> hExtents = obb.halfExtents();
                OBB::OBBAxis axis = obb.axis();
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = position._v;
                    ret = processField(bbField) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Half Extents ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = hExtents._v;
                    ret = processField(bbField) || ret;
                }
                for (U8 i = 0; i < 3; ++i) {
                    EditorComponentField bbField = {};
                    bbField._name = Util::StringFormat( "Axis [ %d ]", i).c_str();
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = true;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = axis[i]._v;
                    ret = processField(bbField) || ret;
                }
            }break;
            case EditorComponentFieldType::BOUNDING_SPHERE: {
                printFieldName();

                BoundingSphere bs = {};
                field.get<BoundingSphere>(bs);
                F32* center = Attorney::BoundingSphereEditor::center(bs);
                F32& radius = Attorney::BoundingSphereEditor::radius(bs);
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Center ";
                    bbField._basicType = GFX::PushConstantType::VEC3;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = center;
                    bbField._dataSetter = [&field](const void* c) {
                        BoundingSphere bSphere = {};
                        field.get<BoundingSphere>(bSphere);
                        bSphere.setCenter(*static_cast<const vec3<F32>*>(c));
                        field.set<BoundingSphere>(bSphere);
                    };
                    ret = processField(bbField) || ret;
                }
                {
                    EditorComponentField bbField = {};
                    bbField._name = "Radius ";
                    bbField._basicType = GFX::PushConstantType::FLOAT;
                    bbField._type = EditorComponentFieldType::PUSH_TYPE;
                    bbField._readOnly = field._readOnly;
                    bbField._hexadecimal = field._hexadecimal;
                    bbField._data = &radius;
                    bbField._dataSetter = [&field](const void* r) {
                        BoundingSphere bSphere = {};
                        field.get<BoundingSphere>(bSphere);
                        bSphere.setRadius(*static_cast<const F32*>(r));
                        field.set<BoundingSphere>(bSphere);
                    };
                    ret = processField(bbField) || ret;
                }
            }break;
            case EditorComponentFieldType::TRANSFORM: {
                assert(!field._dataSetter && "Need direct access to memory");
                ret = processTransform(field.getPtr<TransformComponent>(), field._readOnly, field._hexadecimal);
            }break;

            case EditorComponentFieldType::MATERIAL: {
                assert(!field._dataSetter && "Need direct access to memory");
                ret = processMaterial(field.getPtr<Material>(), field._readOnly, field._hexadecimal);
            }break;
            case EditorComponentFieldType::COUNT: break;
        }

        if (!skipAutoTooltip() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(field._tooltip.empty() ? field._name.c_str() : field._tooltip.c_str());
        }

        return ret;
    }

    bool PropertyWindow::processTransform(TransformComponent* transform, bool readOnly, bool hex) {
        if (transform == nullptr) {
            return false;
        }
        Util::PushNarrowLabelWidth();
        bool ret = false;
        const bool transformReadOnly = readOnly || transform->editorLockPosition();
        const bool rotationReadOnly = readOnly || transform->editorLockRotation();
        const bool scaleReadOnly = readOnly || transform->editorLockScale();

        const TransformValues transformValues = transform->getLocalValues();
        vec3<F32> pos = transformValues._translation;
        vec3<F32> rot = Angle::to_DEGREES(transformValues._orientation.getEuler());
        vec3<F32> scale = transformValues._scale;
        
        const vec3<F32> oldRot = rot;
        if (Util::DrawVec<F32, 3, true>(ImGuiDataType_Float, "Position", pos._v, transformReadOnly).wasChanged) {
            ret = true;
            RegisterUndo<vec3<F32>, false>(_parent,
                                           GFX::PushConstantType::VEC3,
                                           transformValues._translation,
                                           pos,
                                           "Transform position",
                                           [transform](const vec3<F32>& val) {
                                                transform->setPosition(val);
                                           });
            transform->setPosition(pos);
        }
        if (Util::DrawVec<F32, 3, true>(ImGuiDataType_Float, "Rotation", rot._v, rotationReadOnly).wasChanged) {
            ret = true;
            RegisterUndo<vec3<F32>, false>(_parent,
                                           GFX::PushConstantType::VEC3,
                                           oldRot,
                                           rot,
                                           "Transform rotation",
                                           [transform](const vec3<F32>& val) {
                                               transform->setRotationEuler(val);
                                           });
            transform->setRotationEuler(rot);
        }
        TransformComponent::ScalingMode scalingMode = transform->scalingMode();
        bool nonUniformScalingEnabled = scalingMode != TransformComponent::ScalingMode::UNIFORM;

        bool scaleChanged = false;
        if (nonUniformScalingEnabled) {
            scaleChanged = Util::DrawVec<F32, 3, true>(ImGuiDataType_Float, "Scale", scale._v, scaleReadOnly, 1.f).wasChanged;
        } else {
            if (Util::DrawVec<F32, 1, true>(ImGuiDataType_Float, "Scale", scale._v, scaleReadOnly, 1.f).wasChanged) {
                scaleChanged = true;
                scale.z = scale.y = scale.x;
            }
        }

        if (scaleChanged) {
            ret = true;
            // Scale is tricky as it may invalidate everything if it's set wrong!
            for (U8 i = 0; i < 3; ++i) {
                scale[i] = std::max(std::numeric_limits<F32>::epsilon(), scale[i]);
            }
            RegisterUndo<vec3<F32>, false>(_parent,
                                          GFX::PushConstantType::VEC3,
                                          transformValues._scale,
                                          scale,
                                          "Transform scale",
                                          [transform](const vec3<F32>& val) {
                                              transform->setScale(val);
                                          });
            transform->setScale(scale);
        }

        if (ImGui::Checkbox("Non-uniform scaling", &nonUniformScalingEnabled)) {
            const TransformComponent::ScalingMode newMode = nonUniformScalingEnabled ? TransformComponent::ScalingMode::NON_UNIFORM : TransformComponent::ScalingMode::UNIFORM;
            RegisterUndo<bool, false>(_parent, GFX::PushConstantType::UINT, to_U32(scalingMode), to_U32(newMode), "Non-uniform scaling", [transform](const bool& oldVal) noexcept {
                transform->scalingMode(static_cast<TransformComponent::ScalingMode>(oldVal));
            });

            transform->scalingMode(newMode);
            ret = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle per-axis independent scale values.\nAllow shear/tear/squash/etc.\nBreaks the scene hierarchy in many ways but should be fine for leaf nodes");
            skipAutoTooltip(true);
        }
        Util::PopNarrowLabelWidth();
        return ret;
    }

    bool PropertyWindow::processMaterial(Material* material, bool readOnly, bool hex) {
        if (material == nullptr) {
            return false;
        }

        if (readOnly) {
            PushReadOnly();
        }

        bool ret = false;
        static RenderStagePass currentStagePass{RenderStage::DISPLAY, RenderPassType::MAIN_PASS};
        {
            const char* crtStageName = TypeUtil::RenderStageToString(currentStagePass._stage);
            const char* crtPassName = TypeUtil::RenderPassTypeToString(currentStagePass._passType);
            if (ImGui::BeginCombo("Stage", crtStageName, ImGuiComboFlags_PopupAlignLeft)) {
                for (U8 n = 0; n < to_U8(RenderStage::COUNT); ++n) {
                    const RenderStage mode = static_cast<RenderStage>(n);
                    const bool isSelected = currentStagePass._stage == mode;

                    if (ImGui::Selectable(TypeUtil::RenderStageToString(mode), isSelected)) {
                        currentStagePass._stage = mode;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("PassType", crtPassName, ImGuiComboFlags_PopupAlignLeft)) {
                for (U8 n = 0; n < to_U8(RenderPassType::COUNT); ++n) {
                    const RenderPassType pass = static_cast<RenderPassType>(n);
                    const bool isSelected = currentStagePass._passType == pass;

                    if (ImGui::Selectable(TypeUtil::RenderPassTypeToString(pass), isSelected)) {
                        currentStagePass._passType = pass;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            {
                constexpr U8 min = 0u, max = to_U8(RenderStagePass::VariantType::COUNT);
                ImGui::SliderScalar("Variant", ImGuiDataType_U8, &currentStagePass._variant, &min, &max);
            }
            {
                constexpr U8 min = 0u, max = to_U8(RenderStagePass::PassIndex::COUNT);
                ImGui::SliderScalar("Pass", ImGuiDataType_U16, &currentStagePass._pass, &min, &max);
            }

            ImGui::InputScalar("Index", ImGuiDataType_U16, &currentStagePass._index, nullptr, nullptr, (hex ? "%08X" : nullptr), (hex ? ImGuiInputTextFlags_CharsHexadecimal : 0u));
        }

        size_t stateHash = 0;
        string shaderName = "None";
        ShaderProgram* program = nullptr;
        if (currentStagePass._stage != RenderStage::COUNT && currentStagePass._passType != RenderPassType::COUNT) {
            const I64 shaderGUID = material->computeAndGetProgramGUID(currentStagePass);
            program = ShaderProgram::FindShaderProgram(shaderGUID);
            if (program != nullptr) {
                shaderName = program->resourceName().c_str();
            }
            stateHash = material->getOrCreateRenderStateBlock(currentStagePass);
        }

        if (ImGui::CollapsingHeader(("Program: " + shaderName).c_str())) {
            if (program != nullptr) {
                const ShaderProgramDescriptor& descriptor = program->descriptor();
                for (const ShaderModuleDescriptor& module : descriptor._modules) {
                    const char* stages[] = { "PS", "VS", "GS", "HS", "DS","CS" };
                    if (ImGui::CollapsingHeader(Util::StringFormat("%s: File [ %s ] Variant [ %s ]",
                                                                stages[to_base(module._moduleType)],
                                                                module._sourceFile.data(),
                                                                module._variant.empty() ? "-" : module._variant.c_str()).c_str())) 
                    {
                        ImGui::Text("Defines: ");
                        ImGui::Separator();
                        for (const auto& [text, appendPrefix] : module._defines) {
                            ImGui::Text(text.c_str());
                        }
                        if (ImGui::Button("Open Source File")) {
                            const string& textEditor = Attorney::EditorGeneralWidget::externalTextEditorPath(_context.editor());
                            if (textEditor.empty()) {
                                Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "ERROR: No text editor specified!", Time::SecondsToMilliseconds<F32>(3), true);
                            } else {
                                if (openFile(textEditor.c_str(), (program->assetLocation() + Paths::Shaders::GLSL::g_parentShaderLoc).c_str(), module._sourceFile.data()) != FileError::NONE) {
                                    Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "ERROR: Couldn't open specified source file!", Time::SecondsToMilliseconds<F32>(3), true);
                                }
                            }
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Rebuild from source") && !readOnly) {
                    Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "Rebuilding shader from source ...", Time::SecondsToMilliseconds<F32>(3), false);
                    bool skipped = false;
                    if (!program->recompile(skipped)) {
                        Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), "ERROR: Failed to rebuild shader from source!", Time::SecondsToMilliseconds<F32>(3), true);
                    } else {
                        Attorney::EditorGeneralWidget::showStatusMessage(_context.editor(), skipped ? "Rebuilt shader not needed!" : "Rebuilt shader from source!", Time::SecondsToMilliseconds<F32>(3), false);
                        ret = true;
                    }
                }
                ImGui::Separator();
            }
        }

        static bool renderStateWasOpen = false;
        if (!ImGui::CollapsingHeader(Util::StringFormat("Render State: %zu", stateHash).c_str(), (renderStateWasOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0u) | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            renderStateWasOpen = false;
        } else if (stateHash > 0) {
            renderStateWasOpen = true;

            RenderStateBlock block = RenderStateBlock::get(stateHash);
            bool changed = false;
            {
                P32 colourWrite = block.colourWrite();
                constexpr const char* const names[] = { "R", "G", "B", "A" };

                for (U8 i = 0; i < 4; ++i) {
                    if (i > 0) {
                        ImGui::SameLine();
                    }

                    bool val = colourWrite.b[i] == 1;
                    if (ImGui::Checkbox(names[i], &val)) {
                        RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !val, val, "Colour Mask", [stateHash, material, i](const bool& oldVal) {
                            RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                            const P32 cw = stateBlock.colourWrite();
                            stateBlock.setColourWrites(
                                i == 0 ? oldVal : cw.b[0],
                                i == 1 ? oldVal : cw.b[1],
                                i == 2 ? oldVal : cw.b[2],
                                i == 3 ? oldVal : cw.b[3]
                            );
                            material->setRenderStateBlock(stateBlock.getHash(), currentStagePass._stage, currentStagePass._passType, currentStagePass._variant);
                        });
                        colourWrite.b[i] = val ? 1 : 0;
                        block.setColourWrites(colourWrite.b[0] == 1, colourWrite.b[1] == 1, colourWrite.b[2] == 1, colourWrite.b[3] == 1);
                        changed = true;
                    }
                }
            }

            F32 zBias = block.zBias();
            F32 zUnits = block.zUnits();
            {
                EditorComponentField tempField = {};
                tempField._name = "ZBias";
                tempField._basicType = GFX::PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::PUSH_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &zBias;
                tempField._range = { 0.0f, 1000.0f };
                const RenderStagePass tempPass = currentStagePass;
                tempField._dataSetter = [material, stateHash, tempPass, zUnits](const void* data) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setZBias(*static_cast<const F32*>(data), zUnits);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                };
                changed = processField(tempField) || changed;
            }
            {
                EditorComponentField tempField = {};
                tempField._name = "ZUnits";
                tempField._basicType = GFX::PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::PUSH_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &zUnits;
                tempField._range = { 0.0f, 65536.0f };
                const RenderStagePass tempPass = currentStagePass;
                tempField._dataSetter = [material, stateHash, tempPass, zBias](const void* data) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setZBias(zBias, *static_cast<const F32*>(data));
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                };
                changed = processField(tempField) || changed;
            }

            ImGui::Text("Tessellation control points: %d", block.tessControlPoints());
            
            {
                CullMode cMode = block.cullMode();

                static UndoEntry<I32> cullUndo = {};
                const char* crtMode = TypeUtil::CullModeToString(cMode);
                if (ImGui::BeginCombo("Cull Mode", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(CullMode::COUNT); ++n) {
                        const CullMode mode = static_cast<CullMode>(n);
                        const bool isSelected = cMode == mode;

                        if (ImGui::Selectable(TypeUtil::CullModeToString(mode), isSelected)) {
                            cullUndo._type = GFX::PushConstantType::INT;
                            cullUndo._name = "Cull Mode";
                            cullUndo._oldVal = to_I32(cMode);
                            cullUndo._newVal = to_I32(mode);
                            const RenderStagePass tempPass = currentStagePass;
                            cullUndo._dataSetter = [material, stateHash, tempPass](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setCullMode(static_cast<CullMode>(data));
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(cullUndo);

                            cMode = mode;
                            block.setCullMode(mode);
                            changed = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> fillUndo = {};
                FillMode fMode = block.fillMode();
                const char* crtMode = TypeUtil::FillModeToString(fMode);
                if (ImGui::BeginCombo("Fill Mode", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(FillMode::COUNT); ++n) {
                        const FillMode mode = static_cast<FillMode>(n);
                        const bool isSelected = fMode == mode;

                        if (ImGui::Selectable(TypeUtil::FillModeToString(mode), isSelected)) {
                            fillUndo._type = GFX::PushConstantType::INT;
                            fillUndo._name = "Fill Mode";
                            fillUndo._oldVal = to_I32(fMode);
                            fillUndo._newVal = to_I32(mode);
                            const RenderStagePass tempPass = currentStagePass;
                            fillUndo._dataSetter = [material, stateHash, tempPass](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setFillMode(static_cast<FillMode>(data));
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(fillUndo);

                            fMode = mode;
                            block.setFillMode(mode);
                            changed = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            U32 stencilReadMask = block.stencilMask();
            U32 stencilWriteMask = block.stencilWriteMask();
            if (ImGui::InputScalar("Stencil mask", ImGuiDataType_U32, &stencilReadMask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>(_parent, GFX::PushConstantType::UINT, block.stencilMask(), stencilReadMask, "Stencil mask", [material, stateHash, stencilWriteMask, tempPass](const U32& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setStencilReadWriteMask(oldVal, stencilWriteMask);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                block.setStencilReadWriteMask(stencilReadMask, stencilWriteMask);
                changed = true;
            }

            if (ImGui::InputScalar("Stencil write mask", ImGuiDataType_U32, &stencilWriteMask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>(_parent, GFX::PushConstantType::UINT, block.stencilWriteMask(), stencilWriteMask, "Stencil write mask", [material, stateHash, stencilReadMask, tempPass](const U32& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setStencilReadWriteMask(stencilReadMask, oldVal);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                block.setStencilReadWriteMask(stencilReadMask, stencilWriteMask);
                changed = true;
            }

            bool stencilDirty = false;
            U32 stencilRef = block.stencilRef();
            bool stencilEnabled = block.stencilEnable();
            StencilOperation sFailOp = block.stencilFailOp();
            StencilOperation sZFailOp = block.stencilZFailOp();
            StencilOperation sPassOp = block.stencilPassOp();
            ComparisonFunction sFunc = block.stencilFunc();

            if (ImGui::InputScalar("Stencil reference mask", ImGuiDataType_U32, &stencilRef, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<U32, false>(_parent, GFX::PushConstantType::UINT, block.stencilRef(), stencilRef, "Stencil reference mask", [material, stateHash, tempPass, stencilEnabled, sFailOp, sZFailOp, sPassOp, sFunc](const U32& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setStencil(stencilEnabled, oldVal, sFailOp, sPassOp, sZFailOp, sFunc);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });
                stencilDirty = true;
            }

            {
                static UndoEntry<I32> stencilUndo = {};
                ComparisonFunction zFunc = block.zFunc();
                const char* crtMode = TypeUtil::ComparisonFunctionToString(zFunc);
                if (ImGui::BeginCombo("Depth function", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(ComparisonFunction::COUNT); ++n) {
                        const ComparisonFunction func = static_cast<ComparisonFunction>(n);
                        const bool isSelected = zFunc == func;

                        if (ImGui::Selectable(TypeUtil::ComparisonFunctionToString(func), isSelected)) {
                            stencilUndo._type = GFX::PushConstantType::INT;
                            stencilUndo._name = "Depth function";
                            stencilUndo._oldVal = to_I32(zFunc);
                            stencilUndo._newVal = to_I32(func);
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, stateHash, tempPass](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setZFunc(static_cast<ComparisonFunction>(data));
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(stencilUndo);

                            zFunc = func;
                            block.setZFunc(func);
                            changed = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString(sFailOp);
                if (ImGui::BeginCombo("Stencil fail op", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(StencilOperation::COUNT); ++n) {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sFailOp == op;

                        if (ImGui::Selectable(TypeUtil::StencilOperationToString(op), isSelected)) {
                            stencilUndo._type = GFX::PushConstantType::INT;
                            stencilUndo._name = "Stencil fail op";
                            stencilUndo._oldVal = to_I32(sFailOp);
                            stencilUndo._newVal = to_I32(op);
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, stateHash, tempPass, stencilEnabled, stencilRef, sZFailOp, sPassOp, sFunc](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setStencil(stencilEnabled, stencilRef, static_cast<StencilOperation>(data), sPassOp, sZFailOp, sFunc);
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(stencilUndo);

                            sFailOp = op;
                            stencilDirty = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString(sZFailOp);
                if (ImGui::BeginCombo("Stencil depth fail op", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(StencilOperation::COUNT); ++n) {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sZFailOp == op;

                        if (ImGui::Selectable(TypeUtil::StencilOperationToString(op), isSelected)) {
                            stencilUndo._type = GFX::PushConstantType::INT;
                            stencilUndo._name = "Stencil depth fail op";
                            stencilUndo._oldVal = to_I32(sZFailOp);
                            stencilUndo._newVal = to_I32(op);
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, stateHash, tempPass, stencilEnabled, stencilRef, sFailOp, sPassOp, sFunc](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setStencil(stencilEnabled, stencilRef, sFailOp, sPassOp, static_cast<StencilOperation>(data), sFunc);
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(stencilUndo);

                            sZFailOp = op;
                            stencilDirty = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::StencilOperationToString(sPassOp);
                if (ImGui::BeginCombo("Stencil pass op", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(StencilOperation::COUNT); ++n) {
                        const StencilOperation op = static_cast<StencilOperation>(n);
                        const bool isSelected = sPassOp == op;

                        if (ImGui::Selectable(TypeUtil::StencilOperationToString(op), isSelected)) {

                            stencilUndo._type = GFX::PushConstantType::INT;
                            stencilUndo._name = "Stencil pass op";
                            stencilUndo._oldVal = to_I32(sPassOp);
                            stencilUndo._newVal = to_I32(op);
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, stateHash, tempPass, stencilEnabled, stencilRef, sFailOp, sZFailOp, sFunc](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setStencil(stencilEnabled, stencilRef, sFailOp, static_cast<StencilOperation>(data), sZFailOp, sFunc);
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(stencilUndo);

                            sPassOp = op;
                            stencilDirty = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            {
                static UndoEntry<I32> stencilUndo = {};
                const char* crtMode = TypeUtil::ComparisonFunctionToString(sFunc);
                if (ImGui::BeginCombo("Stencil function", crtMode, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(ComparisonFunction::COUNT); ++n) {
                        const ComparisonFunction mode = static_cast<ComparisonFunction>(n);
                        const bool isSelected = sFunc == mode;

                        if (ImGui::Selectable(TypeUtil::ComparisonFunctionToString(mode), isSelected)) {

                            stencilUndo._type = GFX::PushConstantType::INT;
                            stencilUndo._name = "Stencil function";
                            stencilUndo._oldVal = to_I32(sFunc);
                            stencilUndo._newVal = to_I32(mode);
                            const RenderStagePass tempPass = currentStagePass;
                            stencilUndo._dataSetter = [material, stateHash, tempPass, stencilEnabled, stencilRef, sFailOp, sZFailOp, sPassOp](const I32& data) {
                                RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                                stateBlock.setStencil(stencilEnabled, stencilRef, sFailOp, sPassOp, sZFailOp,  static_cast<ComparisonFunction>(data));
                                material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                            };
                            _context.editor().registerUndoEntry(stencilUndo);

                            sFunc = mode;
                            stencilDirty = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            bool frontFaceCCW = block.frontFaceCCW();
            if (ImGui::Checkbox("CCW front face", &frontFaceCCW)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !frontFaceCCW, frontFaceCCW, "CCW front face", [material, stateHash, tempPass](const bool& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setFrontFaceCCW(oldVal);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                block.setFrontFaceCCW(frontFaceCCW);
                changed = true;
            }

            bool scissorEnabled = block.scissorTestEnabled();
            if (ImGui::Checkbox("Scissor test", &scissorEnabled)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !scissorEnabled, scissorEnabled, "Scissor test", [material, stateHash, tempPass](const bool& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setScissorTest(oldVal);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                block.setScissorTest(scissorEnabled);
                changed = true;
            }

            bool depthTestEnabled = block.depthTestEnabled();
            if (ImGui::Checkbox("Depth test", &depthTestEnabled)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !depthTestEnabled, depthTestEnabled, "Depth test", [material, stateHash, tempPass](const bool& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.depthTestEnabled(oldVal);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                block.depthTestEnabled(depthTestEnabled);
                changed = true;
            }

            if (ImGui::Checkbox("Stencil test", &stencilEnabled)) {
                const RenderStagePass tempPass = currentStagePass;
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !stencilEnabled, stencilEnabled, "Stencil test", [material, stateHash, tempPass, stencilRef, sFailOp, sZFailOp, sPassOp, sFunc](const bool& oldVal) {
                    RenderStateBlock stateBlock = RenderStateBlock::get(stateHash);
                    stateBlock.setStencil(oldVal, stencilRef, sFailOp, sPassOp, sZFailOp,  sFunc);
                    material->setRenderStateBlock(stateBlock.getHash(), tempPass._stage, tempPass._passType, tempPass._variant);
                });

                stencilDirty = true;
            }

            if (stencilDirty) {
                block.setStencil(stencilEnabled, stencilRef, sFailOp, sPassOp, sZFailOp, sFunc);
                changed = true;
            }

            if (changed && !readOnly) {
                material->setRenderStateBlock(block.getHash(), currentStagePass._stage, currentStagePass._passType, currentStagePass._variant);
                ret = true;
            }
        }

        static bool shadingModeWasOpen = false;
        const ShadingMode crtMode = material->properties().shadingMode();
        const char* crtModeName = TypeUtil::ShadingModeToString(crtMode);
        if (!ImGui::CollapsingHeader(Util::StringFormat("Shading Mode [ %s ]", crtModeName).c_str(), (shadingModeWasOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0u) | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            shadingModeWasOpen = false;
        } else {
            skipAutoTooltip(true);

            shadingModeWasOpen = true;
            {
                static UndoEntry<I32> modeUndo = {};
                ImGui::PushItemWidth(250);
                if (ImGui::BeginCombo("[Shading Mode]", crtModeName, ImGuiComboFlags_PopupAlignLeft)) {
                    for (U8 n = 0; n < to_U8(ShadingMode::COUNT); ++n) {
                        const ShadingMode mode = static_cast<ShadingMode>(n);
                        const bool isSelected = crtMode == mode;

                        if (ImGui::Selectable(TypeUtil::ShadingModeToString(mode), isSelected)) {

                            modeUndo._type = GFX::PushConstantType::INT;
                            modeUndo._name = "Shading Mode";
                            modeUndo._oldVal = to_I32(crtMode);
                            modeUndo._newVal = to_I32(mode);
                            modeUndo._dataSetter = [material, mode]([[maybe_unused]] const I32& data) {
                                material->properties().shadingMode(mode);
                            };
                            _context.editor().registerUndoEntry(modeUndo);

                            material->properties().shadingMode(mode);
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
            }
            I32 id = 0;

            ApplyAllButton(id, false, *material, [](const Material& baseMaterial, Material* matInstance) {
                matInstance->properties().shadingMode(baseMaterial.properties().shadingMode());
            });

            bool fromTexture = false;
            Texture* texture = nullptr;
            { //Base colour
                ImGui::Separator();
                ImGui::PushItemWidth(250);
                FColour4 diffuse = material->getBaseColour(fromTexture, texture);
                if (Util::colourInput4(_parent, "[Albedo]", diffuse, readOnly, [material](const FColour4& col) { material->properties().baseColour(col); return true; })) {
                    ret = true;
                }
                ImGui::PopItemWidth();
                if (fromTexture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Albedo is sampled from a texture. Base colour possibly unused!");
                    skipAutoTooltip(true);
                }
                ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                    matInstance->properties().baseColour(baseMaterial.properties().baseColour());
                });
                if (PreviewTextureButton(id, texture, !fromTexture)) {
                    _previewTexture = texture;
                }
                ImGui::Separator();
            }
            { //Second texture
                Texture_ptr detailTex = material->getTexture(TextureUsage::UNIT1).lock();
                const bool ro = detailTex == nullptr;
                ImGui::PushID(4321234 + id++);
                if (ro || readOnly) {
                    PushReadOnly();
                }

                bool showTexture = false;
                if (ImGui::Button("Detail Texture")) {
                    ret = true;
                    showTexture = true;
                }
                if (ro || readOnly) {
                    PopReadOnly();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (ro) {
                        ImGui::SetTooltip("No detail texture specified!");
                    } else {
                        ImGui::SetTooltip(Util::StringFormat("Preview texture : %s", detailTex->assetName().c_str()).c_str());
                    }
                    skipAutoTooltip(true);
                }
                ImGui::PopID();
                if (showTexture && !ro) {
                    _previewTexture = detailTex.get();
                }
            }
            { //Normal
                Texture_ptr normalTex = material->getTexture(TextureUsage::NORMALMAP).lock();
                const bool ro = normalTex == nullptr;
                ImGui::PushID(4321234 + id++);
                if (ro || readOnly) {
                    PushReadOnly();
                }
                bool showTexture = false;
                if (ImGui::Button("Normal Map")) {
                    ret = true;
                    showTexture = true;
                }
                if (ro || readOnly) {
                    PopReadOnly();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (ro) {
                        ImGui::SetTooltip("No normal map specified!");
                    } else {
                        ImGui::SetTooltip(Util::StringFormat("Preview texture : %s", normalTex->assetName().c_str()).c_str());
                    }
                    skipAutoTooltip(true);
                }
                ImGui::PopID();
                if (showTexture && !ro) {
                    _previewTexture = normalTex.get();
                }
            }
            { //Emissive
                ImGui::Separator();

                ImGui::PushItemWidth(250);
                FColour3 emissive = material->getEmissive(fromTexture, texture);
                if (Util::colourInput3(_parent, "[Emissive]", emissive, fromTexture || readOnly, [&material](const FColour3& col) { material->properties().emissive(col); return true; })) {
                    ret = true;
                }
                ImGui::PopItemWidth();
                if (fromTexture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Control managed by application (e.g. is overriden by a texture)");
                    skipAutoTooltip(true);
                }
                ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                    matInstance->properties().emissive(baseMaterial.properties().emissive());
                });
                if (PreviewTextureButton(id, texture, !fromTexture)) {
                    _previewTexture = texture;
                }
                ImGui::Separator();
            }
            { //Ambient
                ImGui::Separator();
                ImGui::PushItemWidth(250);
                FColour3 ambient = material->getAmbient(fromTexture, texture);
                if (Util::colourInput3(_parent, "[Ambient]", ambient, fromTexture || readOnly, [material](const FColour3& colour) { material->properties().ambient(colour); return true; })) {
                    ret = true;
                }
                ImGui::PopItemWidth();
                if (fromTexture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Control managed by application (e.g. is overriden by a texture)");
                    skipAutoTooltip(true);
                }
                ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                    matInstance->properties().ambient(baseMaterial.properties().ambient());
                });
                if (PreviewTextureButton(id, texture, !fromTexture)) {
                    _previewTexture = texture;
                }
                ImGui::Separator();
            }
            if (material->properties().shadingMode() != ShadingMode::PBR_MR &&
                material->properties().shadingMode() != ShadingMode::PBR_SG)
            {
                FColour3 specular = material->getSpecular(fromTexture, texture);
                F32 shininess = material->properties().shininess();

                { //Specular power
                    EditorComponentField tempField = {};
                    tempField._name = "Shininess";
                    tempField._basicType = GFX::PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = readOnly;
                    tempField._data = &shininess;
                    tempField._range = { 0.0f, Material::MAX_SHININESS };
                    tempField._dataSetter = [&material](const void* s) {
                        material->properties().shininess(*static_cast<const F32*>(s));
                    };

                    ImGui::PushItemWidth(175);
                    ret = processBasicField(tempField) || ret;
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ApplyAllButton(id, fromTexture || readOnly, *material, [&specular](const Material& baseMaterial, Material* matInstance) {
                        matInstance->properties().shininess(baseMaterial.properties().shininess());
                    });
                }
                { //Specular colour
                    ImGui::Separator();
                    ImGui::PushItemWidth(250);
                    if (Util::colourInput3(_parent, "[Specular]", specular, fromTexture || readOnly, [material](const FColour3& col) { material->properties().specular(col); return true; })) {
                        ret = true;
                    }
                    ImGui::PopItemWidth();
                    if (fromTexture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("Control managed by application (e.g. is overriden by a texture)");
                    }
                    ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                        matInstance->properties().specular(baseMaterial.properties().specular());
                    });
                    if (PreviewTextureButton(id, texture, !fromTexture)) {
                        _previewTexture = texture;
                    }
                    skipAutoTooltip(true);
                    ImGui::Separator();
                }
            } else {
                { // Metallic
                    ImGui::Separator();
                    F32 metallic = material->getMetallic(fromTexture, texture);
                    EditorComponentField tempField = {};
                    tempField._name = "Metallic";
                    tempField._basicType = GFX::PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if (fromTexture) {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &metallic;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material](const void* m) {
                        material->properties().metallic(*static_cast<const F32*>(m));
                    };

                    ImGui::PushItemWidth(175);
                    ret = processBasicField(tempField) || ret;
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                        matInstance->properties().metallic(baseMaterial.properties().metallic());
                    });
                    if (PreviewTextureButton(id, texture, !fromTexture)) {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }
                { // Roughness
                    ImGui::Separator();
                    F32 roughness = material->getRoughness(fromTexture, texture);
                    EditorComponentField tempField = {};
                    tempField._name = "Roughness";
                    tempField._basicType = GFX::PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if (fromTexture) {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &roughness;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material](const void* r) {
                        material->properties().roughness(*static_cast<const F32*>(r));
                    };
                    ImGui::PushItemWidth(175);
                    ret = processBasicField(tempField) || ret;
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                        matInstance->properties().roughness(baseMaterial.properties().roughness());
                    });
                    if (PreviewTextureButton(id, texture, !fromTexture)) {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }

                { // Occlusion
                    ImGui::Separator();
                    F32 occlusion = material->getOcclusion(fromTexture, texture);
                    EditorComponentField tempField = {};
                    tempField._name = "Occlusion";
                    tempField._basicType = GFX::PushConstantType::FLOAT;
                    tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                    tempField._readOnly = fromTexture || readOnly;
                    if (fromTexture) {
                        tempField._tooltip = "Control managed by application (e.g. is overriden by a texture)";
                    }
                    tempField._data = &occlusion;
                    tempField._range = { 0.0f, 1.0f };
                    tempField._dataSetter = [material](const void* m) {
                        material->properties().occlusion(*static_cast<const F32*>(m));
                    };

                    ImGui::PushItemWidth(175);
                    ret = processBasicField(tempField) || ret;
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                        matInstance->properties().occlusion(baseMaterial.properties().occlusion());
                    });
                    if (PreviewTextureButton(id, texture, !fromTexture)) {
                        _previewTexture = texture;
                    }
                    ImGui::Separator();
                }
            }
            { // Parallax
                ImGui::Separator();
                F32 parallax = material->properties().parallaxFactor();
                EditorComponentField tempField = {};
                tempField._name = "Parallax";
                tempField._basicType = GFX::PushConstantType::FLOAT;
                tempField._type = EditorComponentFieldType::SLIDER_TYPE;
                tempField._readOnly = readOnly;
                tempField._data = &parallax;
                tempField._range = { 0.0f, 1.0f };
                tempField._dataSetter = [material](const void* p) {
                    material->properties().parallaxFactor(*static_cast<const F32*>(p));
                };
                ImGui::PushItemWidth(175);
                ret = processBasicField(tempField) || ret;
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                    matInstance->properties().parallaxFactor(baseMaterial.properties().parallaxFactor());
                });
                ImGui::Separator();
            }
            { // Texture operations
                constexpr char* const names[] = {
                    "Tex operation [Albedo - Tex0]",
                    "Tex operation [(Albedo*Tex0) - Tex1]",
                    "Tex operation [SpecColour - SpecMap]"
                };

                static UndoEntry<I32> opUndo = {};
                for (U8 i = 0; i < 3; ++i) {
                    const TextureUsage targetTex = i == 0 ? TextureUsage::UNIT0 
                                                          : i == 1 ? TextureUsage::UNIT1
                                                                   : TextureUsage::SPECULAR;

                    const bool hasTexture = material->getTexture(targetTex).lock() != nullptr;

                    ImGui::PushID(4321234 + id++);
                    if (!hasTexture) {
                        PushReadOnly();
                    }
                    const TextureOperation crtOp = material->getTextureInfo(targetTex)._operation;
                    ImGui::Text(names[i]);
                    if (ImGui::BeginCombo("", TypeUtil::TextureOperationToString(crtOp), ImGuiComboFlags_PopupAlignLeft)) {
                        for (U8 n = 0; n < to_U8(TextureOperation::COUNT); ++n) {
                            const TextureOperation op = static_cast<TextureOperation>(n);
                            const bool isSelected = op == crtOp;

                            if (ImGui::Selectable(TypeUtil::TextureOperationToString(op), isSelected)) {

                                opUndo._type = GFX::PushConstantType::INT;
                                opUndo._name = "Tex Operation " + Util::to_string(i);
                                opUndo._oldVal = to_I32(crtOp);
                                opUndo._newVal = to_I32(op);
                                opUndo._dataSetter = [material, targetTex](const I32& data) {
                                    material->setTextureOperation(targetTex, static_cast<TextureOperation>(data));
                                };
                                _context.editor().registerUndoEntry(opUndo);
                                material->setTextureOperation(targetTex, op);
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ApplyAllButton(id, fromTexture || readOnly, *material, [targetTex](const Material& baseMaterial, Material* matInstance) {
                        matInstance->setTextureOperation(targetTex, baseMaterial.getTextureInfo(targetTex)._operation);
                    });
                    if (!hasTexture) {
                        PopReadOnly();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Insuficient input textures for this operation!");
                            skipAutoTooltip(true);
                        }
                    }
                    ImGui::PopID();
                }

                ImGui::Separator();
            }
            bool ignoreTexAlpha = material->properties().overrides().ignoreTexDiffuseAlpha();
            bool doubleSided = material->properties().doubleSided();
            bool refractive = material->properties().isRefractive();
            const bool reflective = material->reflective();

            ImGui::Text("[Double Sided]"); ImGui::SameLine();
            if (ImGui::ToggleButton("[Double Sided]", &doubleSided) && !readOnly) {
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !doubleSided, doubleSided, "DoubleSided", [material](const bool& oldVal) {
                    material->properties().doubleSided(oldVal);
                });
                material->properties().doubleSided(doubleSided);
                ret = true;
            }
            ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                matInstance->properties().doubleSided(baseMaterial.properties().doubleSided());
            });
            ImGui::Text("[Ignore texture Alpha]"); ImGui::SameLine();
            if (ImGui::ToggleButton("[Ignore texture Alpha]", &ignoreTexAlpha) && !readOnly) {
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !ignoreTexAlpha, ignoreTexAlpha, "IgnoretextureAlpha", [material](const bool& oldVal) {
                    material->properties().ignoreTexDiffuseAlpha(oldVal);
                });
                material->properties().ignoreTexDiffuseAlpha(ignoreTexAlpha);
                ret = true;
            }
            ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                matInstance->properties().ignoreTexDiffuseAlpha(baseMaterial.properties().overrides().ignoreTexDiffuseAlpha());
            });
            ImGui::Text("[Refractive]"); ImGui::SameLine();
            if (ImGui::ToggleButton("[Refractive]", &refractive) && !readOnly) {
                RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !refractive, refractive, "Refractive", [material](const bool& oldVal) {
                    material->properties().isRefractive(oldVal);
                });
                material->properties().isRefractive(refractive);
                ret = true;
            }
            ApplyAllButton(id, fromTexture || readOnly, *material, [](const Material& baseMaterial, Material* matInstance) {
                matInstance->properties().isRefractive(baseMaterial.properties().isRefractive());
            });
        }
        ImGui::Separator();

        if (readOnly) {
            PopReadOnly();
            ret = false;
        }
        return ret;
    }

    bool PropertyWindow::processBasicField(EditorComponentField& field) {
          const bool isSlider = field._type == EditorComponentFieldType::SLIDER_TYPE &&
                                field._basicType != GFX::PushConstantType::BOOL &&
                                !field.isMatrix();

          const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_CharsNoBlank |
                                            (field._hexadecimal ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal) |
                                            (field._readOnly ? ImGuiInputTextFlags_ReadOnly : 0u);

          const char* name = field._name.c_str();
          ImGui::PushID(name);
          Util::PushTooltip(field._tooltip.c_str());
          const F32 step = field._step;
          bool ret = false;
          switch (field._basicType) {
              case GFX::PushConstantType::BOOL: {
                  if (field._readOnly) {
                      PushReadOnly();
                  }

                  ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[1];

                  static UndoEntry<bool> undo = {};
                  bool val = field.get<bool>();
                  if (field._type == EditorComponentFieldType::SWITCH_TYPE) {
                      ret = ImGui::ToggleButton("", &val);
                  } else {
                      ret = ImGui::Checkbox("", &val);
                  }
                  ImGui::SameLine();
                  ImGui::PushFont(boldFont);
                  ImGui::Text(name);
                  ImGui::PopFont();
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                      if (Util::IsPushedTooltip()) {
                          ImGui::SetTooltip(Util::PushedToolTip());
                      } else {
                          ImGui::SetTooltip(name);
                      }
                      skipAutoTooltip(true);
                  }

                  if (ret && !field._readOnly) {
                      RegisterUndo<bool, false>(_parent, GFX::PushConstantType::BOOL, !val, val, name, [&field](const bool& oldVal) {
                          field.set(oldVal);
                      });
                      field.set<bool>(val);
                  }
                  if (field._readOnly) {
                      PopReadOnly();
                  }
              }break;
              case GFX::PushConstantType::INT: {
              switch (field._basicTypeSize) {
                  case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<I64, I64, 1>(_parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                  case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<I32, I32, 1>(_parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                  case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<I16, I16, 1>(_parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                  case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<I8,  I8,  1>(_parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                  case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UINT: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<U64, U64, 1>(_parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<U32, U32, 1>(_parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<U16, U16, 1>(_parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<U8,  U8,  1>(_parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::DOUBLE: {
                  ret = Util::inputOrSlider<D64, D64, 1>(_parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::FLOAT: {
                  ret = Util::inputOrSlider<F32, F32, 1>(_parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::IVEC2: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec2<I64>, I64, 2>(_parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec2<I32>, I32, 2>(_parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec2<I16>, I16, 2>(_parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec2<I8>,  I8,  2>(_parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::IVEC3: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec3<I64>, I64, 3>(_parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec3<I32>, I32, 3>(_parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec3<I16>, I16, 3>(_parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec3<I8>,  I8,  3>(_parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::IVEC4: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec4<I64>, I64, 4>(_parent, isSlider, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec4<I32>, I32, 4>(_parent, isSlider, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec4<I16>, I16, 4>(_parent, isSlider, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec4<I8>,  I8,  4>(_parent, isSlider, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UVEC2: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec2<U64>, U64, 2>(_parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec2<U32>, U32, 2>(_parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec2<U16>, U16, 2>(_parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec2<U8>,  U8,  2>(_parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UVEC3: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec3<U64>, U64, 3>(_parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec3<U32>, U32, 3>(_parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec3<U16>, U16, 3>(_parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec3<U8>,  U8,  3>(_parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UVEC4: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputOrSlider<vec4<U64>, U64, 4>(_parent, isSlider, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputOrSlider<vec4<U32>, U32, 4>(_parent, isSlider, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputOrSlider<vec4<U16>, U16, 4>(_parent, isSlider, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputOrSlider<vec4<U8>,  U8,  4>(_parent, isSlider, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::VEC2: {
                  ret = Util::inputOrSlider<vec2<F32>, F32, 2>(_parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::VEC3: {
                  ret = Util::inputOrSlider<vec3<F32>, F32, 3>(_parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::VEC4: {
                  ret = Util::inputOrSlider<vec4<F32>, F32, 4>(_parent, isSlider, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DVEC2: {
                  ret = Util::inputOrSlider<vec2<D64>, D64, 2>(_parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DVEC3: {
                  ret = Util::inputOrSlider<vec3<D64>, D64, 3>(_parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DVEC4: {
                  ret = Util::inputOrSlider<vec4<D64>, D64, 4>(_parent, isSlider, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::IMAT2: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat2<I64>, 2>(_parent, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat2<I32>, 2>(_parent, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat2<I16>, 2>(_parent, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat2<I8>,  2>(_parent, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::IMAT3: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat3<I64>, 3>(_parent, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat3<I32>, 3>(_parent, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat3<I16>, 3>(_parent, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat3<I8>,  3>(_parent, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
                  ImGui::Separator();
              }break;
              case GFX::PushConstantType::IMAT4: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat4<I64>, 4>(_parent, name, step, ImGuiDataType_S64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat4<I32>, 4>(_parent, name, step, ImGuiDataType_S32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat4<I16>, 4>(_parent, name, step, ImGuiDataType_S16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat4<I8>,  4>(_parent, name, step, ImGuiDataType_S8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
                  ImGui::Separator();
              }break;
              case GFX::PushConstantType::UMAT2: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat2<U64>, 2>(_parent, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat2<U32>, 2>(_parent, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat2<U16>, 2>(_parent, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat2<U8>,  2>(_parent, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UMAT3: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat3<U64>, 3>(_parent, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat3<U32>, 3>(_parent, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat3<U16>, 3>(_parent, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat3<U8>,  3>(_parent, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::UMAT4: {
                  switch (field._basicTypeSize) {
                      case GFX::PushConstantSize::QWORD: ret = Util::inputMatrix<mat4<U64>, 4>(_parent, name, step, ImGuiDataType_U64, field, flags, field._format); break;
                      case GFX::PushConstantSize::DWORD: ret = Util::inputMatrix<mat4<U32>, 4>(_parent, name, step, ImGuiDataType_U32, field, flags, field._format); break;
                      case GFX::PushConstantSize::WORD:  ret = Util::inputMatrix<mat4<U16>, 4>(_parent, name, step, ImGuiDataType_U16, field, flags, field._format); break;
                      case GFX::PushConstantSize::BYTE:  ret = Util::inputMatrix<mat4<U8>,  4>(_parent, name, step, ImGuiDataType_U8,  field, flags, field._format); break;
                      case GFX::PushConstantSize::COUNT: DIVIDE_UNEXPECTED_CALL(); break;
                  }
              }break;
              case GFX::PushConstantType::MAT2: {
                  ret = Util::inputMatrix<mat2<F32>, 2>(_parent, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::MAT3: {
                  ret = Util::inputMatrix<mat3<F32>, 3>(_parent, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::MAT4: {
                  ret = Util::inputMatrix<mat4<F32>, 4>(_parent, name, step, ImGuiDataType_Float, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DMAT2: {
                  ret = Util::inputMatrix<mat2<D64>, 2>(_parent, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DMAT3: {
                  ret = Util::inputMatrix<mat3<D64>, 3>(_parent, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::DMAT4: {
                  ret = Util::inputMatrix<mat4<D64>, 4>(_parent, name, step, ImGuiDataType_Double, field, flags, field._format);
              }break;
              case GFX::PushConstantType::FCOLOUR3: {
                  ret = Util::colourInput3(_parent, field);
              }break;
              case GFX::PushConstantType::FCOLOUR4: {
                  ret = Util::colourInput4(_parent, field);
              }break;
              case GFX::PushConstantType::COUNT: {
                  ImGui::Text(name);
              }break;
          }
          Util::PopTooltip();
          ImGui::PopID();

          return ret;
      }

      const char* PropertyWindow::name() const {
          const Selections& nodes = selections();
          if (nodes._selectionCount == 0) {
              return DockedWindow::name();
          }
          if (nodes._selectionCount == 1) {
              return node(nodes._selections[0])->name().c_str();
          }

          return Util::StringFormat("%s, %s, ...", node(nodes._selections[0])->name().c_str(), node(nodes._selections[1])->name().c_str()).c_str();
      }
} //namespace Divide
