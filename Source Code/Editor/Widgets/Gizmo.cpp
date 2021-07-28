#include "stdafx.h"

#include "Headers/Gizmo.h"
#include "Editor/Headers/Editor.h"
#include "Managers/Headers/SceneManager.h"

#include <imgui_internal.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4505) //Unreferenced local function has been removed
#endif
#include <ImGuizmo/ImGuizmo.cpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif


namespace ImGuizmo {
    struct GizmoBounds
    {
        float  mRadiusSquareCenter = 0.0f;
        ImVec2 mScreenSquareCenter = { 0.0f, 0.0f };
        ImVec2 mScreenSquareMin = { 0.0f, 0.0f };
        ImVec2 mScreenSquareMax = { 0.0f, 0.0f };
    } gBounds = {};
    
    const GizmoBounds& GetBounds() {
        gBounds.mRadiusSquareCenter = gContext.mRadiusSquareCenter;
        gBounds.mScreenSquareCenter = gContext.mScreenSquareCenter;
        gBounds.mScreenSquareMin = gContext.mScreenSquareMin;
        gBounds.mScreenSquareMax = gContext.mScreenSquareMax;
        return gBounds;
    }
};

namespace Divide {
    namespace {
        constexpr U8 g_maxSelectedNodes = 12;
        using TransformCache = std::array<TransformValues, g_maxSelectedNodes>;
        using NodeCache = std::array<SceneGraphNode*, g_maxSelectedNodes>;

        TransformCache g_transformCache;
        NodeCache g_selectedNodesCache;
        UndoEntry<std::pair<TransformCache, NodeCache>> g_undoEntry;
    }

    Gizmo::Gizmo(Editor& parent, ImGuiContext* targetContext)
        : _parent(parent),
          _imguiContext(targetContext)
    {
        g_undoEntry._name = "Gizmo Manipulation";
    }

    Gizmo::~Gizmo()
    {
        _imguiContext= nullptr;
    }

    ImGuiContext& Gizmo::getContext() noexcept {
        assert(_imguiContext != nullptr);
        return *_imguiContext;
    }

    const ImGuiContext& Gizmo::getContext() const noexcept {
        assert(_imguiContext != nullptr);
        return *_imguiContext;
    }

    void Gizmo::enable(const bool state) noexcept {
        _enabled = state;
    }

    bool Gizmo::enabled() const noexcept {
        return _enabled;
    }

    bool Gizmo::active() const noexcept {
        return enabled() && !_selectedNodes.empty();
    }

    void Gizmo::update(const U64 deltaTimeUS) {
        ACKNOWLEDGE_UNUSED(deltaTimeUS);

        if (_shouldRegisterUndo) {
            _parent.registerUndoEntry(g_undoEntry);
            _shouldRegisterUndo = false;
        }
    }

    void Gizmo::render(const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) {
        if (!active() || _selectedNodes.empty()) {
            return;
        }

        const SceneGraphNode* sgn = _selectedNodes.front();
        if (sgn == nullptr) {
            return;
        }

        TransformComponent* const transform = sgn->get<TransformComponent>();
        if (transform == nullptr) {
            return;
        }

        bool hasScale = false;
        vec3<F32> startScale = {};
        vec3<F32> startPos = {};
        mat4<F32> matrix = {};
        BoundingBox nodesBB = {};

        {
            U8 selectionCounter = 0;
            for (auto& node : _selectedNodes) {
                TransformComponent* tComp = node->get<TransformComponent>();
                if (tComp != nullptr) {
                    nodesBB.add(tComp->getPosition());
                    if (!hasScale) {
                        startScale = tComp->getScale();
                        hasScale = true;
                    }
                    if (++selectionCounter == g_maxSelectedNodes) {
                        break;
                    }
                }
            }
        }

        startPos = nodesBB.getCenter();
        matrix.identity();
        matrix.setTranslation(startPos);
        matrix.setScale(startScale);

        ImGui::SetCurrentContext(_imguiContext);
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        DisplayWindow* mainWindow = static_cast<DisplayWindow*>(ImGui::GetMainViewport()->PlatformHandle);
        const vec2<U16> size = mainWindow->getDrawableSize();
        ImGuizmo::SetRect(0.f, 0.f, to_F32(size.width), to_F32(size.height));

        Manipulate(camera->viewMatrix(),
                   camera->projectionMatrix(),
                   _transformSettings.currentGizmoOperation,
                   _transformSettings.currentGizmoMode,
                   matrix,
                   nullptr,
                   _transformSettings.useSnap ? &_transformSettings.snap[0] : nullptr);

        if (ImGuizmo::IsUsing()) {
            if (!_wasUsed) {
                U8 selectionCounter = 0;
                for (auto& node : _selectedNodes) {
                    TransformComponent* tComp = node->get<TransformComponent>();
                    if (tComp != nullptr) {
                        g_transformCache[selectionCounter] = tComp->getValues();
                        g_selectedNodesCache[selectionCounter] = node;
                        if (++selectionCounter == g_maxSelectedNodes) {
                            break;
                        }
                    }
                }
                
                g_undoEntry._oldVal = { g_transformCache, g_selectedNodesCache };
            }

            //ToDo: This seems slow as hell, but it works. Should I bother? -Ionut
            vec3<F32> euler;
            ImGuizmo::DecomposeMatrixToComponents(matrix, _workValues._translation._v, euler._v, _workValues._scale._v);
            matrix.orthoNormalize();
            _workValues._orientation.fromMatrix(mat3<F32>(matrix));

            _workValues._translation = _workValues._translation - startPos;
            for (auto& node : _selectedNodes) {
                TransformComponent* tComp = node->get<TransformComponent>();
                if (tComp != nullptr) {
                   
                    break;
                }
            }

            bool gotScale = false;
            for (auto& node : _selectedNodes) {
                TransformComponent* tComp = node->get<TransformComponent>();
                U8 selectionCounter = 0;
                if (tComp != nullptr) {
                    if (!gotScale) {
                        _workValues._scale /= tComp->getScale();
                        gotScale = true;
                    }
                    tComp->scale(_workValues._scale);
                    tComp->rotate(_workValues._orientation);
                    tComp->translate(_workValues._translation);
                    g_transformCache[selectionCounter] = tComp->getValues();
                    if (++selectionCounter == g_maxSelectedNodes) {
                        break;
                    }
                }
            }
            _wasUsed = true;

            g_undoEntry._newVal = { g_transformCache, g_selectedNodesCache };

            g_undoEntry._dataSetter = [](const std::pair<TransformCache, NodeCache>& data) {
                for (U8 i = 0; i < g_maxSelectedNodes; ++i) {
                    SceneGraphNode* node = data.second[i];
                    if (node != nullptr) {
                        TransformComponent* tComp = node->get<TransformComponent>();
                        if (tComp != nullptr) {
                            tComp->setTransform(data.first[i]);
                        }
                    }
                }
            };
        }

        ImGui::Render();
        Attorney::EditorGizmo::renderDrawList(_parent, ImGui::GetDrawData(), targetViewport, -1, bufferInOut);
    }

    void Gizmo::updateSelections(const vectorEASTL<SceneGraphNode*>& nodes) {
         _selectedNodes.resize(0);
         for (SceneGraphNode* node : nodes) {
            if (node->get<TransformComponent>() != nullptr) {
                _selectedNodes.push_back(node);
            }
        }
    }

    void Gizmo::setTransformSettings(const TransformSettings& settings) noexcept {
        _transformSettings = settings;
    }

    const TransformSettings& Gizmo::getTransformSettings() const noexcept {
        return _transformSettings;
    }

    void Gizmo::onSceneFocus(const bool state) noexcept {
        ACKNOWLEDGE_UNUSED(state);

        ImGuiIO & io = _imguiContext->IO;
        _wasUsed = false;
        io.KeyCtrl = io.KeyShift = io.KeyAlt = io.KeySuper = false;
    }

    bool Gizmo::onKey(const bool pressed, const Input::KeyEvent& key) {
        if (pressed) {
            _wasUsed = false;
        } else if (_wasUsed) {
            _shouldRegisterUndo = true;
            _wasUsed = false;
        }

        ImGuiIO & io = _imguiContext->IO;

        io.KeysDown[to_I32(key._key)] = pressed;

        if (key._key == Input::KeyCode::KC_LCONTROL || key._key == Input::KeyCode::KC_RCONTROL) {
            io.KeyCtrl = pressed;
        }

        if (key._key == Input::KeyCode::KC_LSHIFT || key._key == Input::KeyCode::KC_RSHIFT) {
            io.KeyShift = pressed;
        }

        if (key._key == Input::KeyCode::KC_LMENU || key._key == Input::KeyCode::KC_RMENU) {
            io.KeyAlt = pressed;
        }

        if (key._key == Input::KeyCode::KC_LWIN || key._key == Input::KeyCode::KC_RWIN) {
            io.KeySuper = pressed;
        }

        bool ret = false;
        if (active() && io.KeyCtrl) {
            TransformSettings settings = _parent.getTransformSettings();
            if (key._key == Input::KeyCode::KC_T) {
                settings.currentGizmoOperation = ImGuizmo::TRANSLATE;
                ret = true;
            } else if (key._key == Input::KeyCode::KC_R) {
                settings.currentGizmoOperation = ImGuizmo::ROTATE;
                ret = true;
            } else if (key._key == Input::KeyCode::KC_S) {
                settings.currentGizmoOperation = ImGuizmo::SCALE;
                ret = true;
            }
            if (ret && !pressed) {
                _parent.setTransformSettings(settings);
            }
        }

        return ret;
    }

    void Gizmo::onMouseButton(const bool pressed) {
        if (pressed) {
            _wasUsed = false;
        } else if (_wasUsed) {
            _shouldRegisterUndo = true;
            _wasUsed = false;
        }
    }

    bool Gizmo::needsMouse() const {
        if (active()) {
            ImGuiContext* crtContext = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(_imguiContext);
            const bool imguizmoState = ImGuizmo::IsOver();
            ImGui::SetCurrentContext(crtContext);
            return imguizmoState;
        }

        return false;
    }

    bool Gizmo::hovered() const {
        if (active()) {
            const ImGuizmo::GizmoBounds& bounds = ImGuizmo::GetBounds();
            const ImGuiIO& io = _imguiContext->IO;
            const vec2<F32> deltaScreen = {
                io.MousePos.x - bounds.mScreenSquareCenter.x,
                io.MousePos.y - bounds.mScreenSquareCenter.y
            };
            const F32 dist = deltaScreen.length();
            return dist < bounds.mRadiusSquareCenter + 5.0f;
        }

        return false;
    }
} //namespace Divide