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
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Core/Headers/FrameListener.h"
#include "Rendering/Camera/Headers/CameraSnapshot.h"
#include "Editor/Widgets/Headers/Gizmo.h"

#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

#include <ImGuiMisc/imguistyleserializer/imguistyleserializer.h>

struct ImDrawData;

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
    class LightPool;
    class ECSManager;
    class UndoManager;
    class IMPrimitive;
    class DockedWindow;
    class OutputWindow;
    class PanelManager;
    class PostFXWindow;
    class DisplayWindow;
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
    FWD_DECLARE_MANAGED_CLASS( Texture );
    FWD_DECLARE_MANAGED_CLASS( MenuBar );
    FWD_DECLARE_MANAGED_CLASS( StatusBar );
    FWD_DECLARE_MANAGED_CLASS( ShaderProgram );
    FWD_DECLARE_MANAGED_CLASS( GenericVertexData );
    FWD_DECLARE_MANAGED_CLASS( EditorOptionsWindow );

    struct Selections;
    struct SizeChangeParams;
    struct TransformSettings;
    struct PushConstantsStruct;

    template<typename T>
    struct UndoEntry;
    
    void InitBasicImGUIState( ImGuiIO& io ) noexcept;

    struct TextureCallbackData
    {
        GFXDevice* _gfxDevice = nullptr;
        Texture* _texture = nullptr;
        vec4<I32> _colourData = { 1, 1, 1, 1 };
        vec2<F32> _depthRange = { 0.002f, 1.f };
        U32 _arrayLayer = 0u;
        U32 _mip = 0u;
        bool _isDepthTexture = false;
        bool _flip = true;
        bool _srgb = false;
    };

    PushConstantsStruct TexCallbackToPushConstants(const TextureCallbackData& data, bool isArrayTexture);

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

        public:
        static std::array<Input::MouseButton, 5> g_oisButtons;
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
        ~Editor();

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

        [[nodiscard]] bool Undo() const;
        [[nodiscard]] inline size_t UndoStackSize() const noexcept;

        [[nodiscard]] bool Redo() const;
        [[nodiscard]] inline size_t RedoStackSize() const noexcept;

        [[nodiscard]] Rect<I32> scenePreviewRect( bool globalCoords ) const noexcept;
        [[nodiscard]] bool wantsMouse() const;
        [[nodiscard]] bool wantsKeyboard() const noexcept;
        [[nodiscard]] bool wantsJoystick() const noexcept;
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

    protected: //frame listener
        [[nodiscard]] bool framePostRender( const FrameEvent& evt ) override;
        [[nodiscard]] bool frameEnded( const FrameEvent& evt ) noexcept override;

        public: // input
            /// Key pressed: return true if input was consumed
        [[nodiscard]] bool onKeyDown( const Input::KeyEvent& key ) override;
        /// Key released: return true if input was consumed
        [[nodiscard]] bool onKeyUp( const Input::KeyEvent& key ) override;
        /// Mouse moved: return true if input was consumed
        [[nodiscard]] bool mouseMoved( const Input::MouseMoveEvent& arg ) override;
        /// Mouse button pressed: return true if input was consumed
        [[nodiscard]] bool mouseButtonPressed( const Input::MouseButtonEvent& arg ) override;
        /// Mouse button released: return true if input was consumed
        [[nodiscard]] bool mouseButtonReleased( const Input::MouseButtonEvent& arg ) override;

        [[nodiscard]] bool joystickButtonPressed( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickButtonReleased( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickAxisMoved( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickPovMoved( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickBallMoved( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickAddRemove( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool joystickRemap( const Input::JoystickEvent& arg ) noexcept override;
        [[nodiscard]] bool onTextEvent( const Input::TextEvent& arg ) override;

        [[nodiscard]] bool saveToXML() const;
        [[nodiscard]] bool loadFromXML();

    protected:
        [[nodiscard]] inline bool isInit() const noexcept;
        [[nodiscard]] bool render( );

        BoundingSphere teleportToNode( Camera* camera, const SceneGraphNode* sgn ) const;
        void saveNode( const SceneGraphNode* sgn ) const;
        void loadNode( SceneGraphNode* sgn ) const;
        void queueRemoveNode( I64 nodeGUID );
        void updateEditorFocus();
        void updateFocusState( ImVec2 mousePos );
        /// Destroys the old font, if any, before loading the new one
        void createFontTexture( F32 DPIScaleFactor );
        [[nodiscard]] static ImGuiViewport* FindViewportByPlatformHandle( ImGuiContext* context, const DisplayWindow* window );

        [[nodiscard]] U32 saveItemCount() const noexcept;

        [[nodiscard]] bool isDefaultScene() const noexcept;

        void postRender( RenderStage stage, const CameraSnapshot& cameraSnapshot, RenderTargetID target, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
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
        /// sceneNameOverride should be left empty to save the scene in its own folder. Any string passed will create a new scene with the name specified and save everything to that folder instead, leaving the original scene untouched
        /// This is usefull for creating a new scene from the editor's default one.
        [[nodiscard]] bool saveSceneChanges( const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "" ) const;
        [[nodiscard]] bool switchScene( const char* scenePath );

        /// Returns true if the window was closed
        [[nodiscard]] bool modalTextureView( const char* modalName, Texture* tex, vec2<F32> dimensions, bool preserveAspect, bool useModal ) const;
        /// Returns true if the model was queued
        [[nodiscard]] bool modalModelSpawn( const Mesh_ptr& mesh, bool quick, const vec3<F32>& scale = VECTOR3_UNIT, const vec3<F32>& position = VECTOR3_ZERO );
        /// Return true if the model was spawned as a scene node
        [[nodiscard]] bool spawnGeometry( const Mesh_ptr& mesh, const vec3<F32>& scale, const vec3<F32>& position, const vec3<Angle::DEGREES<F32>>& rotation, const string& name ) const;
        /// Return true if the specified node passed frustum culling during the main render pass
        [[nodiscard]] bool isNodeInView( const SceneGraphNode& node ) const noexcept;

        void onRemoveComponent( const EditorComponent& comp ) const;

        [[nodiscard]] LightPool& getActiveLightPool() const;
        [[nodiscard]] SceneEnvironmentProbePool* getActiveEnvProbePool() const noexcept;

        inline void toggleMemoryEditor( bool state ) noexcept;

        void copyPlayerCamToEditorCam() noexcept;
        void setEditorCamLookAt( const vec3<F32>& eye, const vec3<F32>& fwd, const vec3<F32>& up );
        void setEditorCameraSpeed( const vec3<F32>& speed ) noexcept;

        [[nodiscard]] bool addComponent( SceneGraphNode* selection, ComponentType newComponentType ) const;
        [[nodiscard]] bool addComponent( const Selections& selections, ComponentType newComponentType ) const;
        [[nodiscard]] bool removeComponent( SceneGraphNode* selection, ComponentType newComponentType ) const;
        [[nodiscard]] bool removeComponent( const Selections& selections, ComponentType newComponentType ) const;
        [[nodiscard]] SceneNode_ptr createNode( SceneNodeType type, const ResourceDescriptor& descriptor );

        GenericVertexData* getOrCreateIMGUIBuffer( I64 bufferGUID, I32 maxCommandCount, U32 maxVertices, GFX::MemoryBarrierCommand& memCmdInOut );

        protected:
        SceneGraphNode* _previewNode{ nullptr };

        private:
        Time::ProfileTimer& _editorUpdateTimer;
        Time::ProfileTimer& _editorRenderTimer;

        MenuBar_uptr             _menuBar = nullptr;
        StatusBar_uptr           _statusBar = nullptr;
        EditorOptionsWindow_uptr _optionsWindow = nullptr;
        UndoManager_uptr         _undoManager = nullptr;
        Gizmo_uptr               _gizmo = nullptr;

        DisplayWindow* _mainWindow = nullptr;
        Texture_ptr       _fontTexture = nullptr;
        ShaderProgram_ptr _imguiProgram = nullptr;

        IMPrimitive* _infiniteGridPrimitive = nullptr;
        ShaderProgram_ptr _infiniteGridProgram;
        PipelineDescriptor  _infiniteGridPipelineDesc;
        PipelineDescriptor _axisGizmoPipelineDesc;
        IMPrimitive* _axisGizmo = nullptr;
        Pipeline* _editorPipeline = nullptr;

        hashMap<I64, GenericVertexData_ptr> _IMGUIBuffers;

        std::pair<bufferPtr, size_t> _memoryEditorData = { nullptr, 0 };
        std::array<ImGuiContext*, to_base( ImGuiContextType::COUNT )> _imguiContexts = {};
        std::array<DockedWindow*, to_base( WindowType::COUNT )> _dockedWindows = {};

        string                       _externalTextEditorPath = "";

        string          _lastOpenSceneName{ "" };
        size_t         _editorSamplerHash = 0u;
        U32            _stepQueue = 1u;
        F32            _queuedDPIValue = -1.f;
        bool           _simulationPaused = true;
        ImGuiStyleEnum _currentTheme = ImGuiStyle_Count;
        bool           _showSampleWindow = false;
        bool           _showOptionsWindow = false;
        bool           _showMemoryEditor = false;
        bool           _isScenePaused = false;
        bool           _gridSettingsDirty = true;
        CircularBuffer<Str256> _recentSceneList;
        CameraSnapshot _render2DSnapshot{};
        RenderTargetHandle _nodePreviewRTHandle{};
        struct QueueModelSpawn
        {
            Mesh_ptr _mesh{ nullptr };
            vec3<F32> _scale{ VECTOR3_UNIT };
            vec3<F32> _position{ VECTOR3_ZERO };
        } _queuedModelSpawn;
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
                return editor._gizmo->enabled();
            }

            static void editorEnableGizmo( const Editor& editor, const bool state ) noexcept
            {
                editor._gizmo->enable( state );
            }

            static void copyPlayerCamToEditorCam( Editor& editor ) noexcept
            {
                editor.copyPlayerCamToEditorCam();
            }

            static void setEditorCamLookAt( Editor& editor, const vec3<F32>& eye, const vec3<F32>& fwd, const vec3<F32>& up ) noexcept
            {
                editor.setEditorCamLookAt( eye, fwd, up );
            }

            static void setEditorCameraSpeed( Editor& editor, const vec3<F32>& speed ) noexcept
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

            [[nodiscard]] static Camera* getSelectedCamera( const Editor& editor )  noexcept
            {
                return editor.selectedCamera();
            }

            [[nodiscard]] static bool editorEnableGizmo( const Editor& editor ) noexcept
            {
                return editor._gizmo->enabled();
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

            [[nodiscard]] static SceneNode_ptr createNode( Editor& editor, const SceneNodeType type, const ResourceDescriptor& descriptor )
            {
                return editor.createNode( type, descriptor );
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

            [[nodiscard]] static const string& externalTextEditorPath( const Editor& editor ) noexcept
            {
                return editor._externalTextEditorPath;
            }

            static void externalTextEditorPath( Editor& editor, const string& path )
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

            [[nodiscard]] static const CircularBuffer<Str256>& getRecentSceneList( const Editor& editor ) noexcept
            {
                return editor._recentSceneList;
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

            [[nodiscard]] static bool enableGizmo( const Editor& editor ) noexcept
            {
                return editor._gizmo->enabled();
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

            [[nodiscard]] static bool saveSceneChanges( const Editor& editor, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "" )
            {
                return editor.saveSceneChanges( msgCallback, finishCallback, sceneNameOverride );
            }

            static bool switchScene( Editor& editor, const char* scenePath )
            {
                return editor.switchScene( scenePath );
            }

            static void inspectMemory( Editor& editor, const std::pair<bufferPtr, size_t> data ) noexcept
            {
                editor._memoryEditorData = data;
            }

            [[nodiscard]] static bool modalTextureView( const Editor& editor, const char* modalName, Texture* tex, const vec2<F32> dimensions, const bool preserveAspect, const bool useModal )
            {
                return editor.modalTextureView( modalName, tex, dimensions, preserveAspect, useModal );
            }

            [[nodiscard]] static bool modalModelSpawn( Editor& editor, const Mesh_ptr& mesh, bool quick, const vec3<F32>& scale = VECTOR3_UNIT, const vec3<F32>& position = VECTOR3_ZERO )
            {
                return editor.modalModelSpawn( mesh, quick, scale, position );
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

            [[nodiscard]] static const string& externalTextEditorPath( const Editor& editor ) noexcept
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
            static void postRender( Editor& editor, const RenderStage stage, const CameraSnapshot& cameraSnapshot, const RenderTargetID target, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                editor.postRender( stage, cameraSnapshot, target, bufferInOut, memCmdInOut );
            }

            friend class RenderPassExecutor;
        };

        class EditorEditorComponent
        {

            static void onRemoveComponent( const Editor& editor, const EditorComponent& comp )
            {
                editor.onRemoveComponent( comp );
            }

            friend class EditorComponent;
        };
    } //namespace Attorney

    void PushReadOnly( bool fade = true );
    void PopReadOnly();

    struct ScopedReadOnly final : NonCopyable
    {
        ScopedReadOnly( bool fade = false )
        {
            PushReadOnly( fade );
        }
        ~ScopedReadOnly()
        {
            PopReadOnly();
        }
    };
} //namespace Divide

#endif //_DIVIDE_EDITOR_H_

#include "Editor.inl"