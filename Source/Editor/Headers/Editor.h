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
#ifndef DVD_EDITOR_H_
#define DVD_EDITOR_H_

#include "UndoManager.h"

#include "Core/Headers/FrameListener.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Core/TemplateLibraries/Headers/CircularBuffer.h"
#include "Rendering/Camera/Headers/CameraSnapshot.h"
#include "Editor/Widgets/Headers/Gizmo.h"
#include "Editor/Widgets/Headers/DockedWindow.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Managers/Headers/ProjectManager.h"

#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

#include <ImGuiMisc/imguistyleserializer/imguistyleserializer.h>

struct ImDrawData;

#ifndef ImTextureID
namespace ImGui
{
    typedef ImU64 ImTextureID;
}
#endif //ImTextureID

namespace Divide
{

    namespace Attorney
    {
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
        class EditorEditorComponent;
    }

    namespace GFX
    {
        class CommandBuffer;
    }
    class Scene;
    class Camera;
    class Texture;
    class LightPool;
    class RenderPass;
    class ECSManager;
    class UndoManager;
    class IMPrimitive;
    class OutputWindow;
    class PanelManager;
    class PostFXWindow;
    class DisplayWindow;
    class ShaderProgram;
    class PropertyWindow;
    class SceneGraphNode;
    class SceneViewWindow;
    class PlatformContext;
    class ApplicationOutput;
    class NodePreviewWindow;
    class ContentExplorerWindow;
    class SolutionExplorerWindow;
    class SceneEnvironmentProbePool;

    FWD_DECLARE_MANAGED_CLASS( Gizmo );
    FWD_DECLARE_MANAGED_CLASS( Mesh );
    FWD_DECLARE_MANAGED_CLASS( MenuBar );
    FWD_DECLARE_MANAGED_CLASS( StatusBar );
    FWD_DECLARE_MANAGED_CLASS( EditorOptionsWindow );

    struct Selections;
    struct SizeChangeParams;
    struct TransformSettings;
    struct PushConstantsStruct;

    template<typename T>
    struct UndoEntry;
    
    struct IMGUICallbackData
    {
        GFX::CommandBuffer* _cmdBuffer = nullptr;
        Handle<Texture> _texture = INVALID_HANDLE<Texture>;
        int4 _colourData = { 1, 1, 1, 1 };
        float2 _depthRange = { 0.002f, 1.f };
        U32 _arrayLayer = 0u;
        U32 _mip = 0u;
        bool _isDepthTexture = false;
        bool _flip = true;
        bool _srgb = false;
    };

    struct GPUVertexBuffer
    {
        GPUBuffer_uptr _vertexBuffer;
        GPUBuffer_uptr _indexBuffer;

        std::array<GPUBuffer::Handle, 2> _handles;
    };

    PushConstantsStruct IMGUICallbackToPushConstants(const IMGUICallbackData& data, bool isArrayTexture);

    ImGui::ImTextureID to_TexID(Handle<Texture> handle);
    Handle<Texture> from_TexID(ImGui::ImTextureID texID);

    class Editor final : public PlatformContextComponent,
                         public FrameListener,
                         public Input::InputAggregatorInterface,
                         NonMovable
    {

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
        friend class Attorney::EditorEditorComponent;

        struct QueueModelSpawn;

        public:
        static std::array<const char*, 3> g_supportedExportPlatforms;

        enum class WindowType : U8
        {
            PostFX = 0,
            SolutionExplorer,
            Properties,
            ContentExplorer,
            Output,
            NodePreview,
            SceneView,
            COUNT
        };

        enum class ImGuiContextType : U8
        {
            Gizmo = 0,
            Editor = 1,
            COUNT
        };

        struct FocusedWindowState
        {
            bool _focusedScenePreview{ false };
            bool _focusedNodePreview{ false };
            bool _hoveredScenePreview{ false };
            bool _hoveredNodePreview{ false };
            ImVec2 _globalMousePos{ 0.f, 0.f };
            ImVec2 _scaledMousePos{ 0.f, 0.f };
        };

    public:
        explicit Editor( PlatformContext& context, ImGuiStyleEnum theme = ImGuiStyle_DarkCodz01 );
        ~Editor() override;

        [[nodiscard]] bool init( const vec2<U16> renderResolution );
        void close();
        void idle() noexcept;
        void update( U64 deltaTimeUS );
        /// Render any editor specific element that needs to be part of the scene (e.g. Control Gizmo)
        void drawScreenOverlay( const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const;

        void toggle( bool state );
        void onWindowSizeChange( const SizeChangeParams& params );
        void onResolutionChange( const SizeChangeParams& params );
        void selectionChangeCallback( PlayerIndex idx, const vector<SceneGraphNode*>& nodes ) const;
        void onChangeScene( Scene* newScene );
        void onNodeSpatialChange( const SceneGraphNode& node);

        [[nodiscard]] bool Undo() const;
        [[nodiscard]] inline size_t UndoStackSize() const noexcept;

        [[nodiscard]] bool Redo() const;
        [[nodiscard]] inline size_t RedoStackSize() const noexcept;

        [[nodiscard]] Rect<I32> scenePreviewRect( bool globalCoords ) const noexcept;
        [[nodiscard]] bool hasFocus() const;
        [[nodiscard]] bool isHovered() const;
        template<typename T>
        void registerUndoEntry( const UndoEntry<T>& entry );

        [[nodiscard]] inline bool inEditMode() const noexcept;
        [[nodiscard]] inline bool simulationPaused() const noexcept;
        [[nodiscard]] inline U32  stepQueue() const noexcept;
        [[nodiscard]] inline const TransformSettings& getTransformSettings() const noexcept;
        inline void setTransformSettings( const TransformSettings& settings ) const noexcept;

        void infiniteGridAxisWidth( const F32 value ) noexcept;
        void infiniteGridScale( const F32 value ) noexcept;

        void showStatusMessage( const string& message, F32 durationMS, bool error ) const;

        [[nodiscard]] inline const RenderTargetHandle& getNodePreviewTarget() const noexcept;

        void remapAbsolutePosition(Input::MouseEvent& eventInOut) const noexcept;

    protected: //frame listener
        [[nodiscard]] bool framePostRender( const FrameEvent& evt ) override;
        [[nodiscard]] bool frameEnded( const FrameEvent& evt ) noexcept override;

    public: // input
        [[nodiscard]] bool onKeyInternal(Input::KeyEvent& argInOut) override;
        [[nodiscard]] bool onMouseMoved(Input::MouseMoveEvent& argInOut) override;
        [[nodiscard]] bool onMouseMovedInternal(Input::MouseMoveEvent& argInOut) override;
        [[nodiscard]] bool onMouseButton(Input::MouseButtonEvent& argInOut) override;
        [[nodiscard]] bool onMouseButtonInternal(Input::MouseButtonEvent& argInOut) override;
        [[nodiscard]] bool onJoystickButtonInternal(Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool onJoystickAxisMovedInternal(Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool onJoystickPovMovedInternal(Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool onJoystickBallMovedInternal(Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool onJoystickRemapInternal(Input::JoystickEvent& argInOut) override;
        [[nodiscard]] bool onTextInputInternal(Input::TextInputEvent& argInOut) override;
        [[nodiscard]] bool onTextEditInternal(Input::TextEditEvent& argInOut) override;
        [[nodiscard]] bool onDeviceAddOrRemoveInternal(Input::InputEvent& argInOut) override;

        [[nodiscard]] bool wantsMouse() const;
        [[nodiscard]] bool wantsKeyboard() const noexcept;
        [[nodiscard]] bool wantsJoystick() const noexcept;

    public:
        [[nodiscard]] bool saveToXML() const;
        [[nodiscard]] bool loadFromXML();

    protected:
        [[nodiscard]] inline bool isInit() const noexcept;
        [[nodiscard]] bool render( );

        void toggleInternal(bool state);

        BoundingSphere teleportToNode( Camera* camera, const SceneGraphNode* sgn ) const;
        void saveNode( const SceneGraphNode* sgn ) const;
        void loadNode( SceneGraphNode* sgn ) const;
        void queueRemoveNode( I64 nodeGUID );
        void updateEditorFocus();
        void updateFocusState( ImVec2 mousePos );
        /// Destroys the old font, if any, before loading the new one
        void createFontTexture( ImGuiIO& io, F32 DPIScaleFactor );
        [[nodiscard]] static ImGuiViewport* FindViewportByPlatformHandle( ImGuiContext* context, const DisplayWindow* window );

        [[nodiscard]] U32 saveItemCount() const noexcept;

        [[nodiscard]] bool isDefaultScene() const noexcept;

        void postRender( RenderStage stage, const CameraSnapshot& cameraSnapshot, RenderTargetID target, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        void getCommandBuffer( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

        void renderModelSpawnModal();

        PROPERTY_R_IW( I64, previewNodeGUID, -1 );
        PROPERTY_R_IW( bool, running, false );
        PROPERTY_R_IW( bool, unsavedSceneChanges, false );
        PROPERTY_R_IW( FocusedWindowState, windowFocusState );
        POINTER_R_IW( Camera, selectedCamera, nullptr );
        POINTER_R_IW( Camera, editorCamera, nullptr );
        POINTER_R_IW( Camera, nodePreviewCamera, nullptr );
        PROPERTY_R_IW( bool, nodePreviewWindowVisible, false);
        PROPERTY_RW( bool, infiniteGridEnabledScene, true );
        PROPERTY_RW( bool, infiniteGridEnabledNode, true );
        PROPERTY_R( F32, infiniteGridAxisWidth, 2.f );
        PROPERTY_R( F32, infiniteGridScale, 1.f );
        PROPERTY_R_IW( FColour3, nodePreviewBGColour );
        PROPERTY_INTERNAL( bool, lockSolutionExplorer, false );
        PROPERTY_INTERNAL( bool, sceneGizmoEnabled, false );

    protected:
        void renderDrawList( ImDrawData* pDrawData, I64 bufferGUID, const Rect<I32>& targetViewport, bool editorPass, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );

        /// Saves all new changes to the current scene and uses the provided callbacks to return progress messages. msgCallback gets called per save-step/process, finishCallback gets called once at the end
        [[nodiscard]] bool saveSceneChanges( const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback ) const;
        [[nodiscard]] bool switchScene( const SceneEntry& scene, bool createIfNotExists = false );
        [[nodiscard]] bool openProject( const ProjectID& projectID );
        /// Returns true if the window was closed
        [[nodiscard]] bool modalTextureView( std::string_view modalName, Handle<Texture> tex, float2 dimensions, bool preserveAspect, bool useModal ) const;
        /// Returns true if the model was queued
        [[nodiscard]] bool modalModelSpawn( Handle<Mesh> mesh, bool showSpawnModalFirst, const float3& scale, const float3& position);
        /// Return true if the model was spawned as a scene node
        [[nodiscard]] bool spawnGeometry( SceneGraphNode& root, const QueueModelSpawn& model ) const;
        /// Return true if the specified node passed frustum culling during the main render pass
        [[nodiscard]] bool isNodeInView( const SceneGraphNode& node ) const noexcept;

        void onRemoveComponent( const EditorComponent& comp ) const;

        [[nodiscard]] const ProjectIDs& getProjectList() const noexcept;
        [[nodiscard]] const SceneEntries& getSceneList() const noexcept;
        [[nodiscard]] LightPool& getActiveLightPool() const;
        [[nodiscard]] SceneEnvironmentProbePool* getActiveEnvProbePool() const noexcept;

        inline void toggleMemoryEditor( bool state ) noexcept;

        void copyPlayerCamToEditorCam() noexcept;
        void setEditorCamLookAt( const float3& eye, const float3& fwd, const float3& up );
        void setEditorCameraSpeed( const float3& speed ) noexcept;

        [[nodiscard]] bool addComponent( SceneGraphNode* selection, ComponentType newComponentType ) const;
        [[nodiscard]] bool addComponent( const Selections& selections, ComponentType newComponentType ) const;
        [[nodiscard]] bool removeComponent( SceneGraphNode* selection, ComponentType newComponentType ) const;
        [[nodiscard]] bool removeComponent( const Selections& selections, ComponentType newComponentType ) const;

        GPUVertexBuffer* getOrCreateIMGUIBuffer( I64 bufferGUID, U32 maxVertices, U32 maxIndices, GFX::MemoryBarrierCommand& memCmdInOut );
        void initBasicImGUIState(ImGuiIO& io, ImGuiPlatformIO& platform_io, bool enableViewportSupport, string& clipboardStringBuffer) noexcept;

    protected:
        SceneGraphNode* _previewNode{ nullptr };

    private:
        Time::ProfileTimer& _editorUpdateTimer;
        Time::ProfileTimer& _editorRenderTimer;

        MenuBar_uptr             _menuBar;
        StatusBar_uptr           _statusBar;
        EditorOptionsWindow_uptr _optionsWindow;
        UndoManager_uptr         _undoManager;
        Gizmo_uptr               _gizmo ;

        DisplayWindow* _mainWindow = nullptr;
        Handle<Texture>       _fontTexture = INVALID_HANDLE<Texture>;
        Handle<ShaderProgram> _imguiProgram = INVALID_HANDLE<ShaderProgram>;
        Handle<ShaderProgram> _infiniteGridProgram = INVALID_HANDLE<ShaderProgram>;

        IMPrimitive* _infiniteGridPrimitive = nullptr;
        PipelineDescriptor  _infiniteGridPipelineDesc;
        PipelineDescriptor _axisGizmoPipelineDesc;
        IMPrimitive* _axisGizmo = nullptr;
        Pipeline* _editorPipeline = nullptr;

        eastl::fixed_vector<std::pair<I64, GPUVertexBuffer>, 5, true> _imguiBuffers;

        std::pair<bufferPtr, size_t> _memoryEditorData = { nullptr, 0 };
        std::array<ImGuiContext*, to_base( ImGuiContextType::COUNT )> _imguiContexts = {};
        std::array<string, to_base( ImGuiContextType::COUNT )> _imguiStringBuffers = {};
        std::array<std::unique_ptr<DockedWindow>, to_base( WindowType::COUNT )> _dockedWindows = {};

        ResourcePath _externalTextEditorPath;

        SamplerDescriptor _editorSampler{};

        string         _lastOpenSceneName{ "" };
        U32            _stepQueue = 1u;
        F32            _queuedDPIValue = -1.f;
        bool           _simulationPaused = true;
        ImGuiStyleEnum _currentTheme = ImGuiStyle_Count;
        bool           _showSampleWindow = false;
        bool           _showOptionsWindow = false;
        bool           _showMemoryEditor = false;
        bool           _isScenePaused = false;
        bool           _gridSettingsDirty = true;
        bool           _mouseCaptured = false;
        std::optional<bool> _queueToggle;
        CircularBuffer<SceneEntry, 10> _recentSceneList;
        CameraSnapshot _render2DSnapshot;
        RenderTargetHandle _nodePreviewRTHandle{};

        struct QueueModelSpawn
        {
            Handle<Mesh> _mesh{ INVALID_HANDLE<Mesh> };
            TransformValues transform{};
            bool _showModal{true};
        };

        eastl::queue<QueueModelSpawn> _queuedModelSpawns;

    }; //Editor

    namespace Attorney
    {
        class EditorGizmo
        {
            static void renderDrawList( Editor& editor, ImDrawData* pDrawData, const I64 windowGUID, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                editor.renderDrawList( pDrawData, windowGUID, targetViewport, false, bufferInOut, memCmdInOut );
            }

            friend class Divide::Gizmo;
        };

        class EditorSceneViewWindow
        {
            [[nodiscard]] static bool editorEnabledGizmo( const Editor& editor ) noexcept
            {
                return editor._gizmo->isEnabled();
            }

            static void editorEnableGizmo( const Editor& editor, const bool state ) noexcept
            {
                editor._gizmo->enable( state );
            }

            static void copyPlayerCamToEditorCam( Editor& editor ) noexcept
            {
                editor.copyPlayerCamToEditorCam();
            }

            static void setEditorCamLookAt( Editor& editor, const float3& eye, const float3& fwd, const float3& up ) noexcept
            {
                editor.setEditorCamLookAt( eye, fwd, up );
            }

            static void setEditorCameraSpeed( Editor& editor, const float3& speed ) noexcept
            {
                editor.setEditorCameraSpeed( speed );
            }

            static void editorStepQueue( Editor& editor, const U32 steps ) noexcept
            {
                editor._stepQueue = steps;
            }

            static void simulationPaused( Editor& editor, const bool state ) noexcept
            {
                editor._simulationPaused = state;
            }

            static FColour3& nodePreviewBGColour( Editor& editor ) noexcept
            {
                return editor._nodePreviewBGColour;
            }

            friend class Divide::SceneViewWindow;
            friend class Divide::NodePreviewWindow;
        };

        class EditorSolutionExplorerWindow
        {
            static void setSelectedCamera( Editor& editor, Camera* camera )  noexcept
            {
                editor.selectedCamera( camera );
            }

            [[nodiscard]] static const Camera* getSelectedCamera( const Editor& editor )  noexcept
            {
                return editor.selectedCamera();
            }

            static void editorEnableGizmo( const Editor& editor, const bool state ) noexcept
            {
                editor._gizmo->enable( state );
            }

            static BoundingSphere teleportToNode( const Editor& editor, Camera* camera, const SceneGraphNode* targetNode )
            {
                return editor.teleportToNode( camera, targetNode );
            }

            static void saveNode( const Editor& editor, const SceneGraphNode* targetNode )
            {
                editor.saveNode( targetNode );
            }

            static void loadNode( const Editor& editor, SceneGraphNode* targetNode )
            {
                editor.loadNode( targetNode );
            }

            static void queueRemoveNode( Editor& editor, const I64 nodeGUID )
            {
                editor.queueRemoveNode( nodeGUID );
            }

            [[nodiscard]] static bool lockSolutionExplorer( const Editor& editor )
            {
                return editor.lockSolutionExplorer();
            }

            [[nodiscard]] static bool isNodeInView( const Editor& editor, const SceneGraphNode& node )
            {
                return editor.isNodeInView( node );
            }
            friend class Divide::SolutionExplorerWindow;
        };

        class EditorPropertyWindow
        {
            static void setSelectedCamera( Editor& editor, Camera* camera )  noexcept
            {
                editor.selectedCamera( camera );
            }

            [[nodiscard]] static Camera* getSelectedCamera( const Editor& editor )  noexcept
            {
                return editor.selectedCamera();
            }

            static void lockSolutionExplorer( Editor& editor, const bool state ) noexcept
            {
                editor.lockSolutionExplorer( state );
            }

            static void saveNode( const Editor& editor, const SceneGraphNode* targetNode )
            {
                editor.saveNode( targetNode );
            }

            friend class Divide::PropertyWindow;
        };


        class EditorOptionsWindow
        {
            [[nodiscard]] static ImGuiStyleEnum getTheme( const Editor& editor ) noexcept
            {
                return editor._currentTheme;
            }

            static void setTheme( Editor& editor, const ImGuiStyleEnum newTheme ) noexcept
            {
                editor._currentTheme = newTheme;
            }

            [[nodiscard]] static const ResourcePath& externalTextEditorPath( const Editor& editor ) noexcept
            {
                return editor._externalTextEditorPath;
            }

            static void externalTextEditorPath( Editor& editor, const ResourcePath& path )
            {
                editor._externalTextEditorPath = path;
            }

            friend class Divide::EditorOptionsWindow;
        };

        class EditorMenuBar
        {
            static void toggleMemoryEditor( Editor& editor, const bool state )  noexcept
            {
                editor.toggleMemoryEditor( state );
            }

            [[nodiscard]] static bool memoryEditorEnabled( const Editor& editor ) noexcept
            {
                return editor._showMemoryEditor;
            }

            [[nodiscard]] static bool& sampleWindowEnabled( Editor& editor ) noexcept
            {
                return editor._showSampleWindow;
            }

            [[nodiscard]] static bool& optionWindowEnabled( Editor& editor ) noexcept
            {
                return editor._showOptionsWindow;
            }

            [[nodiscard]] static const CircularBuffer<SceneEntry, 10>& getRecentSceneList( const Editor& editor ) noexcept
            {
                return editor._recentSceneList;
            }    
            
            [[nodiscard]] static const SceneEntries& getSceneList( const Editor& editor ) noexcept
            {
                return editor.getSceneList();
            }

            [[nodiscard]] static const ProjectIDs& getProjectList( Editor& editor ) noexcept
            {
                return editor.getProjectList();
            }

            [[nodiscard]] static bool openProject( Editor& editor, const ProjectID& projectID ) noexcept
            {
                return editor.openProject( projectID );
            }

            friend class Divide::MenuBar;
        };

        class EditorGeneralWidget
        {
            static void setTransformSettings( const Editor& editor, const TransformSettings& settings ) noexcept
            {
                editor.setTransformSettings( settings );
            }

            [[nodiscard]] static const TransformSettings& getTransformSettings( const Editor& editor ) noexcept
            {
                return editor.getTransformSettings();
            }

            [[nodiscard]] static LightPool& getActiveLightPool( const Editor& editor )
            {
                return editor.getActiveLightPool();
            }

            [[nodiscard]] static SceneEnvironmentProbePool* getActiveEnvProbePool( const Editor& editor )
            {
                return editor.getActiveEnvProbePool();
            }

            static void enableGizmo( const Editor& editor, const bool state ) noexcept
            {
                return editor._gizmo->enable( state );
            }

            static void setSceneGizmoEnabled( Editor& editor, const bool state ) noexcept
            {
                return editor.sceneGizmoEnabled( state );
            }

            [[nodiscard]] static bool getSceneGizmoEnabled( const Editor& editor ) noexcept
            {
                return editor.sceneGizmoEnabled();
            }

            [[nodiscard]] static U32 saveItemCount( const Editor& editor ) noexcept
            {
                return editor.saveItemCount();
            }

            [[nodiscard]] static bool hasUnsavedSceneChanges( const Editor& editor ) noexcept
            {
                return editor.unsavedSceneChanges();
            }

            [[nodiscard]] static bool isDefaultScene( const Editor& editor ) noexcept
            {
                return editor.isDefaultScene();
            }

            static void registerUnsavedSceneChanges( Editor& editor ) noexcept
            {
                editor.unsavedSceneChanges( true );
            }

            [[nodiscard]] static bool saveSceneChanges( const Editor& editor, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback )
            {
                return editor.saveSceneChanges( msgCallback, finishCallback );
            }

            static bool switchScene( Editor& editor, const SceneEntry& scene, bool createIfNotExists )
            {
                return editor.switchScene( scene, createIfNotExists );
            }

            static void inspectMemory( Editor& editor, const std::pair<bufferPtr, size_t> data ) noexcept
            {
                editor._memoryEditorData = data;
            }

            [[nodiscard]] static bool modalTextureView( const Editor& editor, const std::string_view modalName, Handle<Texture> tex, const float2 dimensions, const bool preserveAspect, const bool useModal )
            {
                return editor.modalTextureView( modalName, tex, dimensions, preserveAspect, useModal );
            }

            [[nodiscard]] static bool modalModelSpawn( Editor& editor, Handle<Mesh> mesh, bool showSpawnModalFirst, const float3& scale, const float3& position )
            {
                return editor.modalModelSpawn( mesh, showSpawnModalFirst, scale, position );
            }

            [[nodiscard]] static ImGuiContext& getImGuiContext( Editor& editor, const Editor::ImGuiContextType type ) noexcept
            {
                return *editor._imguiContexts[to_base( type )];
            }

            [[nodiscard]] static ImGuiContext& imguizmoContext( Editor& editor, const Editor::ImGuiContextType type ) noexcept
            {
                return *editor._imguiContexts[to_base( type )];
            }

            [[nodiscard]] static bool addComponent( const Editor& editor, const Selections& selections, const ComponentType newComponentType )
            {
                return editor.addComponent( selections, newComponentType );
            }

            [[nodiscard]] static bool removeComponent( const Editor& editor, SceneGraphNode* selection, const ComponentType newComponentType )
            {
                return editor.removeComponent( selection, newComponentType );
            }

            [[nodiscard]] static bool removeComponent( const Editor& editor, const Selections& selections, const ComponentType newComponentType )
            {
                return editor.removeComponent( selections, newComponentType );
            }

            static void showStatusMessage( const Editor& editor, const string& message, const F32 durationMS, const bool error )
            {
                editor.showStatusMessage( message, durationMS, error );
            }

            [[nodiscard]] static const ResourcePath& externalTextEditorPath( const Editor& editor ) noexcept
            {
                return editor._externalTextEditorPath;
            }

            static void setPreviewNode( Editor& editor, SceneGraphNode* previewNode ) noexcept
            {
                editor._previewNode = previewNode;
            }

            friend class Divide::Gizmo;
            friend class Divide::MenuBar;
            friend class Divide::PostFXWindow;
            friend class Divide::PropertyWindow;
            friend class Divide::NodePreviewWindow;
            friend class Divide::EditorOptionsWindow;
            friend class Divide::ContentExplorerWindow;
            friend class Divide::SolutionExplorerWindow;
        };

        class EditorRenderPassExecutor
        {
            static void getCommandBuffer( Editor& editor, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                editor.getCommandBuffer( bufferInOut, memCmdInOut );
            }

             static void postRender( Editor& editor, const RenderStage stage, const CameraSnapshot& cameraSnapshot, const RenderTargetID target, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                editor.postRender( stage, cameraSnapshot, target, bufferInOut, memCmdInOut );
            }

            friend class Divide::RenderPass;
            friend class Divide::RenderPassExecutor;
            friend class Divide::RenderPassManager;
        };

        class EditorEditorComponent
        {

            static void onRemoveComponent( const Editor& editor, const EditorComponent& comp )
            {
                editor.onRemoveComponent( comp );
            }

            friend class Divide::EditorComponent;
        };
    } //namespace Attorney

    void PushReadOnly( bool fade = true, F32 fadedAlpha = 0.4f );
    void PopReadOnly();

    struct ScopedReadOnly final : NonCopyable
    {
        ScopedReadOnly( const bool fade = false, const F32 fadedAlpha = 0.4f)
        {
            PushReadOnly( fade, fadedAlpha);
        }
        ~ScopedReadOnly()
        {
            PopReadOnly();
        }
    };

    void PushImGuiContext(ImGuiContext* ctx);
    void PopImGuiContext();

    struct ScopedImGuiContext final : NonCopyable
    {
        ScopedImGuiContext(ImGuiContext* ctx)
        {
            PushImGuiContext(ctx);
        }
        ~ScopedImGuiContext()
        {
            PopImGuiContext();
        }
    };
} //namespace Divide

#endif //DVD_EDITOR_H_

#include "Editor.inl"
