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
#ifndef _DIVIDE_EDITOR_H_
#define _DIVIDE_EDITOR_H_

#include "UndoManager.h"

#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Headers/PlatformContextComponent.h"

#include "Rendering/Headers/FrameListener.h"
#include "Rendering/Camera/Headers/CameraSnapshot.h"

#include "Editor/Widgets/Headers/Gizmo.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

#include <ImGuiMisc/imguistyleserializer/imguistyleserializer.h>

struct ImDrawData;

namespace Divide {

namespace Attorney {
    class EditorGizmo;
    class EditorMenuBar;
    class EditorOutputWindow;
    class EditorOptionsWindow;
    class EditorGeneralWidget;
    class EditorWindowManager;
    class EditorPropertyWindow;
    class EditorSceneViewWindow;
    class EditorSolutionExplorerWindow;
    class EditorRenderPassExecutor;
}

class Gizmo;
class Camera;
class MenuBar;
class StatusBar;
class LightPool;
class ECSManager;
class UndoManager;
class IMPrimitive;
class DockedWindow;
class OutputWindow;
class PanelManager;
class PostFXWindow;
class DisplayWindow;
class FreeFlyCamera;
class PropertyWindow;
class SceneGraphNode;
class SceneViewWindow;
class PlatformContext;
class ApplicationOutput;
class EditorOptionsWindow;
class ContentExplorerWindow;
class SolutionExplorerWindow;
class SceneEnvironmentProbePool;

FWD_DECLARE_MANAGED_CLASS(Mesh);
FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

struct Selections;
struct SizeChangeParams;
struct TransformSettings;

template<typename T>
struct UndoEntry;

void InitBasicImGUIState(ImGuiIO& io) noexcept;

class Editor final : public PlatformContextComponent,
                     public FrameListener,
                     public Input::InputAggregatorInterface,
                     NonMovable {

    friend class Attorney::EditorGizmo;
    friend class Attorney::EditorMenuBar;
    friend class Attorney::EditorOutputWindow;
    friend class Attorney::EditorGeneralWidget;
    friend class Attorney::EditorOptionsWindow;
    friend class Attorney::EditorWindowManager;
    friend class Attorney::EditorPropertyWindow;
    friend class Attorney::EditorSceneViewWindow;
    friend class Attorney::EditorSolutionExplorerWindow;
    friend class Attorney::EditorRenderPassExecutor;

  public:
    static std::array<Input::MouseButton, 5> g_oisButtons;
    static std::array<const char*, 3> g_supportedExportPlatforms;

    enum class WindowType : U8 {
        PostFX = 0,
        SolutionExplorer,
        Properties,
        ContentExplorer,
        Output,
        SceneView,
        COUNT
    };

      enum class ImGuiContextType : U8 {
          Gizmo = 0,
          Editor = 1,
          COUNT
    };
  public:
    explicit Editor(PlatformContext& context, ImGuiStyleEnum theme = ImGuiStyle_DarkCodz01);
    ~Editor();

    [[nodiscard]] bool init(const vec2<U16>& renderResolution);
    void close();
    void idle() noexcept;
    void update(U64 deltaTimeUS);
    /// Render any editor specific element that needs to be part of the scene (e.g. Control Gizmo)
    void drawScreenOverlay(const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) const;

    void toggle(bool state);
    void onSizeChange(const SizeChangeParams& params);
    void selectionChangeCallback(PlayerIndex idx, const vector<SceneGraphNode*>& nodes) const;
    void onChangeScene(Scene* newScene);

    [[nodiscard]] bool Undo() const;
    [[nodiscard]] inline size_t UndoStackSize() const noexcept;

    [[nodiscard]] bool Redo() const;
    [[nodiscard]] inline size_t RedoStackSize() const noexcept;

    [[nodiscard]] Rect<I32> scenePreviewRect(bool globalCoords) const noexcept;
    [[nodiscard]] bool wantsMouse() const;
    [[nodiscard]] bool wantsKeyboard() const noexcept;
    [[nodiscard]] bool wantsJoystick() const noexcept;
    [[nodiscard]] bool usingGizmo() const;

    template<typename T>
    void registerUndoEntry(const UndoEntry<T>& entry);

    [[nodiscard]] inline bool inEditMode() const noexcept;
    [[nodiscard]] inline bool simulationPaused() const noexcept;
    [[nodiscard]] inline U32  stepQueue() const noexcept;
    [[nodiscard]] inline const TransformSettings& getTransformSettings() const noexcept;
    inline void setTransformSettings(const TransformSettings& settings) const noexcept;

    void infiniteGridAxisWidth(const F32 value) noexcept;
    void infiniteGridScale(const F32 value) noexcept;

    void showStatusMessage(const string& message, F32 durationMS, bool error) const;

  protected: //frame listener
    [[nodiscard]] bool framePostRender(const FrameEvent& evt) override;
    [[nodiscard]] bool frameEnded(const FrameEvent& evt) noexcept override;

  public: // input
    /// Key pressed: return true if input was consumed
    [[nodiscard]] bool onKeyDown(const Input::KeyEvent& key) override;
    /// Key released: return true if input was consumed
    [[nodiscard]] bool onKeyUp(const Input::KeyEvent& key) override;
    /// Mouse moved: return true if input was consumed
    [[nodiscard]] bool mouseMoved(const Input::MouseMoveEvent& arg) override;
    /// Mouse button pressed: return true if input was consumed
    [[nodiscard]] bool mouseButtonPressed(const Input::MouseButtonEvent& arg) override;
    /// Mouse button released: return true if input was consumed
    [[nodiscard]] bool mouseButtonReleased(const Input::MouseButtonEvent& arg) override;

    [[nodiscard]] bool joystickButtonPressed(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickButtonReleased(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickAxisMoved(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickPovMoved(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickBallMoved(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickAddRemove(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool joystickRemap(const Input::JoystickEvent &arg) noexcept override;
    [[nodiscard]] bool onUTF8(const Input::UTF8Event& arg) override;
        
    [[nodiscard]] bool saveToXML() const;
    [[nodiscard]] bool loadFromXML();

  protected:
    [[nodiscard]] inline bool isInit() const noexcept;
    [[nodiscard]] bool render(U64 deltaTime);

    void teleportToNode(const SceneGraphNode* sgn) const;
    void saveNode(const SceneGraphNode* sgn) const;
    void loadNode(SceneGraphNode* sgn) const;
    void queueRemoveNode(I64 nodeGUID);
    void onPreviewFocus(bool state) const;
    /// Destroys the old font, if any, before loading the new one
    void createFontTexture(F32 DPIScaleFactor);
    [[nodiscard]] static ImGuiViewport* FindViewportByPlatformHandle(ImGuiContext* context, const DisplayWindow* window);

    [[nodiscard]] U32 saveItemCount() const noexcept;

    [[nodiscard]] bool isDefaultScene() const noexcept;

    void postRender(const Camera& camera, RenderTargetID target, GFX::CommandBuffer& bufferInOut);

    PROPERTY_R_IW(bool, running, false);
    PROPERTY_R_IW(bool, unsavedSceneChanges, false);
    PROPERTY_R_IW(bool, scenePreviewFocused, false);
    PROPERTY_R_IW(bool, scenePreviewHovered, false);
    POINTER_R_IW(Camera, selectedCamera, nullptr);
    POINTER_R_IW(FreeFlyCamera, editorCamera, nullptr);
    PROPERTY_R(Rect<I32>, targetViewport, Rect<I32>(0, 0, 1, 1));
    PROPERTY_RW(bool, infiniteGridEnabled, true);
    PROPERTY_R(F32, infiniteGridAxisWidth, 2.f);
    PROPERTY_R(F32, infiniteGridScale, 1.f);
    PROPERTY_INTERNAL(bool, lockSolutionExplorer, false);

  protected: // attorney
    void renderDrawList(ImDrawData* pDrawData, const Rect<I32>& targetViewport, I64 windowGUID, GFX::CommandBuffer& bufferInOut) const;

    /// Saves all new changes to the current scene and uses the provided callbacks to return progress messages. msgCallback gets called per save-step/process, finishCallback gets called once at the end
    /// sceneNameOverride should be left empty to save the scene in its own folder. Any string passed will create a new scene with the name specified and save everything to that folder instead, leaving the original scene untouched
    /// This is usefull for creating a new scene from the editor's default one.
    [[nodiscard]] bool saveSceneChanges(const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "") const;
    [[nodiscard]] bool switchScene(const char* scenePath);

    // Returns true if the window was closed
    [[nodiscard]] bool modalTextureView(const char* modalName, const Texture* tex, const vec2<F32>& dimensions, bool preserveAspect, bool useModal) const;
    // Returns true if the modal window was closed
    [[nodiscard]] bool modalModelSpawn(const char* modalName, const Mesh_ptr& mesh) const;
    // Return true if the model was spawned as a scene node
    [[nodiscard]] bool spawnGeometry(const Mesh_ptr& mesh, const vec3<F32>& scale, const vec3<F32>& position, const vec3<Angle::DEGREES<F32>>& rotation, const string& name) const;

    [[nodiscard]] ECSManager& getECSManager() const;
    [[nodiscard]] LightPool& getActiveLightPool() const;
    [[nodiscard]] SceneEnvironmentProbePool* getActiveEnvProbePool() const noexcept;

    inline void toggleMemoryEditor(bool state) noexcept;

    void copyPlayerCamToEditorCam() noexcept;

    [[nodiscard]] bool addComponent(SceneGraphNode* selection, ComponentType newComponentType) const;
    [[nodiscard]] bool addComponent(const Selections& selections, ComponentType newComponentType) const;
    [[nodiscard]] bool removeComponent(SceneGraphNode* selection, ComponentType newComponentType) const;
    [[nodiscard]] bool removeComponent(const Selections& selections, ComponentType newComponentType) const;
    [[nodiscard]] SceneNode_ptr createNode(SceneNodeType type, const ResourceDescriptor& descriptor);

  private:
    Time::ProfileTimer& _editorUpdateTimer;
    Time::ProfileTimer& _editorRenderTimer;

    eastl::unique_ptr<MenuBar>             _menuBar = nullptr;
    eastl::unique_ptr<StatusBar>           _statusBar = nullptr;
    eastl::unique_ptr<EditorOptionsWindow> _optionsWindow = nullptr;
    eastl::unique_ptr<UndoManager>         _undoManager = nullptr;
    eastl::unique_ptr<Gizmo>               _gizmo = nullptr;

    DisplayWindow*    _mainWindow = nullptr;
    Texture_ptr       _fontTexture = nullptr;
    ShaderProgram_ptr _imguiProgram = nullptr;

    IMPrimitive*  _infiniteGridPrimitive = nullptr;
    ShaderProgram_ptr _infiniteGridProgram;
    Pipeline*     _infiniteGridPipeline = nullptr;

    std::pair<bufferPtr, size_t> _memoryEditorData = { nullptr, 0 };
    std::array<ImGuiContext*, to_base(ImGuiContextType::COUNT)> _imguiContexts = {};
    std::array<DockedWindow*, to_base(WindowType::COUNT)> _dockedWindows = {};

    string                       _externalTextEditorPath = "";

    I64            _lastOpenSceneGUID = -1;
    U32            _stepQueue = 1u;
    bool           _simulationPaused = true;
    ImGuiStyleEnum _currentTheme = ImGuiStyle_Count;
    bool           _showSampleWindow = false;
    bool           _showOptionsWindow = false;
    bool           _showMemoryEditor = false;
    bool           _isScenePaused = false;
    bool           _gridSettingsDirty = true;
    CircularBuffer<Str256> _recentSceneList;
}; //Editor

namespace Attorney {
    class EditorGizmo {
        static void renderDrawList(const Editor& editor, ImDrawData* pDrawData, const Rect<I32>& targetViewport, const I64 windowGUID, GFX::CommandBuffer& bufferInOut) {
            editor.renderDrawList(pDrawData, targetViewport, windowGUID, bufferInOut);
        }

        [[nodiscard]] static ImGuiViewport* findViewportByPlatformHandle(Editor& editor, ImGuiContext* context, DisplayWindow* window) {
            return editor.FindViewportByPlatformHandle(context, window);
        }

        friend class Divide::Gizmo;
    };

    class EditorSceneViewWindow {
        [[nodiscard]] static bool editorEnabledGizmo(const Editor& editor) noexcept {
            return editor._gizmo->enabled();
        }

        static void editorEnableGizmo(const Editor& editor, const bool state) noexcept {
            editor._gizmo->enable(state);
        }

        static void copyPlayerCamToEditorCam(Editor& editor) noexcept {
            editor.copyPlayerCamToEditorCam();
        }

        static void editorStepQueue(Editor& editor, const U32 steps) noexcept {
            editor._stepQueue = steps;
        }

        static void simulationPaused(Editor& editor, const bool state) noexcept {
            editor._simulationPaused = state;
        }
        
        friend class Divide::SceneViewWindow;
    };

    class EditorSolutionExplorerWindow {
        static void setSelectedCamera(Editor& editor, Camera* camera)  noexcept {
            editor.selectedCamera(camera);
        }

        [[nodiscard]] static Camera* getSelectedCamera(const Editor& editor)  noexcept {
            return editor.selectedCamera();
        }

        [[nodiscard]] static bool editorEnableGizmo(const Editor& editor) noexcept {
            return editor._gizmo->enabled();
        }

        static void editorEnableGizmo(const Editor& editor, const bool state) noexcept {
            editor._gizmo->enable(state);
        }

        static void teleportToNode(const Editor& editor, const SceneGraphNode* targetNode) {
            editor.teleportToNode(targetNode);
        }

        static void saveNode(const Editor& editor, const SceneGraphNode* targetNode) {
            editor.saveNode(targetNode);
        }

        static void loadNode(const Editor& editor, SceneGraphNode* targetNode) {
            editor.loadNode(targetNode);
        }

        static void queueRemoveNode(Editor& editor, const I64 nodeGUID) {
            editor.queueRemoveNode(nodeGUID);
        }

        [[nodiscard]] static SceneNode_ptr createNode(Editor& editor, const SceneNodeType type, const ResourceDescriptor& descriptor) {
            return editor.createNode(type, descriptor);
        }

        [[nodiscard]] static bool lockSolutionExplorer(const Editor& editor) {
            return editor.lockSolutionExplorer();
        }

        friend class Divide::SolutionExplorerWindow;
    };

    class EditorPropertyWindow {
        static void setSelectedCamera(Editor& editor, Camera* camera)  noexcept {
            editor.selectedCamera(camera);
        }

        [[nodiscard]] static Camera* getSelectedCamera(const Editor& editor)  noexcept {
            return editor.selectedCamera();
        }

        static void lockSolutionExplorer(Editor& editor, const bool state) noexcept {
            editor.lockSolutionExplorer(state);
        }

        friend class Divide::PropertyWindow;
    };


    class EditorOptionsWindow {
        [[nodiscard]] static ImGuiStyleEnum getTheme(const Editor& editor) noexcept {
            return editor._currentTheme;
        }

        static void setTheme(Editor& editor, const ImGuiStyleEnum newTheme) noexcept {
            editor._currentTheme = newTheme;
        }

        [[nodiscard]] static const string& externalTextEditorPath(const Editor& editor) noexcept {
            return editor._externalTextEditorPath;
        }

        static void externalTextEditorPath(Editor& editor, const string& path) {
            editor._externalTextEditorPath = path;
        }

        friend class Divide::EditorOptionsWindow;
    };

    class EditorMenuBar {
        static void toggleMemoryEditor(Editor& editor, const bool state)  noexcept {
            editor.toggleMemoryEditor(state);
        }

        [[nodiscard]] static bool memoryEditorEnabled(const Editor& editor) noexcept {
            return editor._showMemoryEditor;
        }

        [[nodiscard]] static bool& sampleWindowEnabled(Editor& editor) noexcept {
            return editor._showSampleWindow;
        }

        [[nodiscard]] static bool& optionWindowEnabled(Editor& editor) noexcept {
            return editor._showOptionsWindow;
        }
        
        [[nodiscard]] static const CircularBuffer<Str256>& getRecentSceneList(const Editor& editor) noexcept {
             return editor._recentSceneList;
        }

        friend class Divide::MenuBar;
    };

    class EditorGeneralWidget {
        static void setTransformSettings(const Editor& editor, const TransformSettings& settings) noexcept {
            editor.setTransformSettings(settings);
        }

        [[nodiscard]] static const TransformSettings& getTransformSettings(const Editor& editor) noexcept {
            return editor.getTransformSettings();
        }

        [[nodiscard]] static LightPool& getActiveLightPool(const Editor& editor) {
            return editor.getActiveLightPool();
        }

        [[nodiscard]] static ECSManager& getECSManager(const Editor& editor) {
            return editor.getECSManager();
        }

        [[nodiscard]] static SceneEnvironmentProbePool* getActiveEnvProbePool(const Editor& editor) {
            return editor.getActiveEnvProbePool();
        }

        static void enableGizmo(const Editor& editor, const bool state) noexcept {
            return editor._gizmo->enable(state);
        }
      
        [[nodiscard]] static bool enableGizmo(const Editor& editor) noexcept {
            return editor._gizmo->enabled();
        }

        [[nodiscard]] static U32 saveItemCount(const Editor& editor) noexcept {
            return editor.saveItemCount();
        }

        [[nodiscard]] static bool hasUnsavedSceneChanges(const Editor& editor) noexcept {
            return editor.unsavedSceneChanges();
        }
        
        [[nodiscard]] static bool isDefaultScene(const Editor& editor) noexcept {
            return editor.isDefaultScene();
        }

        static void registerUnsavedSceneChanges(Editor& editor) noexcept {
            editor.unsavedSceneChanges(true);
        }

        [[nodiscard]] static bool saveSceneChanges(const Editor& editor, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "") {
            return editor.saveSceneChanges(msgCallback, finishCallback, sceneNameOverride);
        }
        
        static bool switchScene(Editor& editor, const char* scenePath) {
            return editor.switchScene(scenePath);
        }

        static void inspectMemory(Editor& editor, const std::pair<bufferPtr, size_t> data) noexcept {
            editor._memoryEditorData = data;
        }

        [[nodiscard]] static bool modalTextureView(const Editor& editor, const char* modalName, const Texture* tex, const vec2<F32>& dimensions, const bool preserveAspect, const bool useModal) {
            return editor.modalTextureView(modalName, tex, dimensions, preserveAspect, useModal);
        }

        [[nodiscard]] static bool modalModelSpawn(const Editor& editor, const char* modalName, const Mesh_ptr& mesh) {
            return editor.modalModelSpawn(modalName, mesh);
        }

        [[nodiscard]] static ImGuiContext& getImGuiContext(Editor& editor, const Editor::ImGuiContextType type) noexcept {
            return *editor._imguiContexts[to_base(type)];
        }

        [[nodiscard]] static ImGuiContext& imguizmoContext(Editor& editor, const Editor::ImGuiContextType type) noexcept {
            return *editor._imguiContexts[to_base(type)];
        }

        [[nodiscard]] static bool addComponent(const Editor& editor, const Selections& selections, const ComponentType newComponentType) {
            return editor.addComponent(selections, newComponentType);
        }

        [[nodiscard]] static bool removeComponent(const Editor& editor, SceneGraphNode* selection, const ComponentType newComponentType) {
            return editor.removeComponent(selection, newComponentType);
        }

        [[nodiscard]] static bool removeComponent(const Editor& editor, const Selections& selections, const ComponentType newComponentType) {
            return editor.removeComponent(selections, newComponentType);
        }

        static void showStatusMessage(const Editor& editor, const string& message, const F32 durationMS, const bool error) {
            editor.showStatusMessage(message, durationMS, error);
        }

        [[nodiscard]] static const string& externalTextEditorPath(const Editor& editor) noexcept {
            return editor._externalTextEditorPath;
        }

        friend class Divide::Gizmo;
        friend class Divide::MenuBar;
        friend class Divide::PropertyWindow;
        friend class Divide::PostFXWindow;
        friend class Divide::EditorOptionsWindow;
        friend class Divide::ContentExplorerWindow;
        friend class Divide::SolutionExplorerWindow;
    };

    class EditorRenderPassExecutor {
        static void postRender(Editor& editor, const Camera& camera, const RenderTargetID target, GFX::CommandBuffer& bufferInOut) {
            editor.postRender(camera, target, bufferInOut);
        }

        friend class RenderPassExecutor;
    };
} //namespace Attorney

void PushReadOnly();
void PopReadOnly();

struct ScopedReadOnly final : NonCopyable
{
    ScopedReadOnly() { PushReadOnly(); }
    ~ScopedReadOnly() { PopReadOnly(); }
};
} //namespace Divide

#endif //_DIVIDE_EDITOR_H_

#include "Editor.inl"