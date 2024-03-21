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
#ifndef DVD_SCENE_MANAGER_H
#define DVD_SCENE_MANAGER_H

#include "Scenes/Headers/Scene.h"

#include "Core/Headers/FrameListener.h"
#include "Core/Headers/KernelComponent.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Rendering/RenderPass/Headers/RenderPassCuller.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide
{

    class LoadSave
    {
        public:
        static bool loadScene( Scene& activeScene );
        static bool saveScene( const Scene& activeScene, bool toCache, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "" );

        static bool saveNodeToXML( const Scene& activeScene, const SceneGraphNode* node );
        static bool loadNodeFromXML( const Scene& activeScene, SceneGraphNode* node );
    };

    enum class RenderStage : U8;
    namespace Attorney
    {
        class SceneManagerScene;
        class SceneManagerEditor;
        class SceneManagerKernel;
        class SceneManagerScenePool;
        class SceneManagerRenderPass;
        class SceneManagerSSRAccessor;
        class SceneManagerCameraAccessor;
    };

    namespace AI
    {
        namespace Navigation
        {
            FWD_DECLARE_MANAGED_CLASS( DivideRecast );
        };
    };

    namespace Time
    {
        class ProfileTimer;
    };

    namespace GFX
    {
        struct MemoryBarrierCommand;
    };

    class Editor;
    class ScenePool;
    class ShadowMap;
    class UnitComponent;
    class RenderPassExecutor;
    class ShaderComputeQueue;
    class SSRPreRenderOperator;
    class DirectionalLightSystem;
    class ContentExplorerWindow;
    class SolutionExplorerWindow;
    class GUIConsoleCommandParser;

    struct CameraSnapshot;
    struct SizeChangeParams;

    FWD_DECLARE_MANAGED_CLASS( Player );
    FWD_DECLARE_MANAGED_CLASS( Texture );

    class SceneManager final : public FrameListener,
        public Input::InputAggregatorInterface,
        public KernelComponent
    {

        friend class Attorney::SceneManagerScene;
        friend class Attorney::SceneManagerEditor;
        friend class Attorney::SceneManagerKernel;
        friend class Attorney::SceneManagerScenePool;
        friend class Attorney::SceneManagerRenderPass;
        friend class Attorney::SceneManagerSSRAccessor;
        friend class Attorney::SceneManagerCameraAccessor;

        public:
        using PlayerList = std::array<UnitComponent*, Config::MAX_LOCAL_PLAYER_COUNT>;

        public:
        static bool OnStartup( PlatformContext& context );
        static bool OnShutdown( PlatformContext& context );

        explicit SceneManager( Kernel& parentKernel );
        ~SceneManager() override;

        void idle();

        [[nodiscard]] vector<Str<256>> sceneNameList( bool sorted = true ) const;

        Scene& getActiveScene() noexcept;
        [[nodiscard]] const Scene& getActiveScene() const noexcept;

        void setActiveScene( Scene* scene );

        bool init( PlatformContext& platformContext, ResourceCache* cache );
        void destroy();

        [[nodiscard]] U8 getActivePlayerCount() const noexcept
        {
            return _activePlayerCount;
        }

        // returns selection callback id
        size_t addSelectionCallback( const DELEGATE<void, U8, const vector_fast<SceneGraphNode*>&>& selectionCallback )
        {
            static std::atomic_size_t index = 0u;

            const size_t idx = index.fetch_add( 1u );
            _selectionChangeCallbacks.push_back( std::make_pair( idx, selectionCallback ) );
            return idx;
        }

        bool removeSelectionCallback( const size_t idx )
        {
            return dvd_erase_if( _selectionChangeCallbacks, [idx]( const auto& entry ) noexcept
            {
                return entry.first == idx;
            } );
        }

        [[nodiscard]] bool resetSelection( PlayerIndex idx, const bool resetIfLocked );
        void setSelected( PlayerIndex idx, const vector_fast<SceneGraphNode*>& SGNs, bool recursive );
        void onNodeDestroy( SceneGraphNode* node );
        /// cull the SceneGraph against the current view frustum. 
        void cullSceneGraph( const NodeCullParams& cullParams, const U16 cullFlags, VisibleNodeList<>& nodesOut );
        /// Searches the scenegraph for the specified nodeGUID and, if found, adds it to nodesOut
        void findNode( const vec3<F32>& cameraEye, const I64 nodeGUID, VisibleNodeList<>& nodesOut );
        /// init default culling values like max cull distance and other scene related states
        void initDefaultCullValues( RenderStage stage, NodeCullParams& cullParamsInOut ) noexcept;
        /// get the full list of reflective nodes
        void getSortedReflectiveNodes( const Camera* camera, RenderStage stage, bool inView, VisibleNodeList<>& nodesOut ) const;
        /// get the full list of refractive nodes
        void getSortedRefractiveNodes( const Camera* camera, RenderStage stage, bool inView, VisibleNodeList<>& nodesOut ) const;

        const VisibleNodeList<>& getRenderedNodeList() const noexcept;

        void onChangeFocus( bool hasFocus );

        /// Check if the scene was loaded properly
        [[nodiscard]] bool loadComplete() const noexcept
        {
            return Attorney::SceneManager::loadComplete( getActiveScene() );
        }
        /// Update animations, network data, sounds, triggers etc.
        void updateSceneState( U64 deltaGameTimeUS, U64 deltaAppTimeUS );

        void onResolutionChange( const SizeChangeParams& params );

        [[nodiscard]] U8 playerPass() const noexcept
        {
            return _currentPlayerPass;
        }

        [[nodiscard]] bool saveActiveScene( bool toCache, bool deferred, const DELEGATE<void, std::string_view>& msgCallback = {}, const DELEGATE<void, bool>& finishCallback = {}, const char* sceneNameOverride = "" );

        [[nodiscard]] AI::Navigation::DivideRecast* recast() const noexcept
        {
            return _recast.get();
        }

        [[nodiscard]] SceneEnvironmentProbePool* getEnvProbes() const noexcept;

        public:  /// Input
        /// Key pressed: return true if input was consumed
        [[nodiscard]] bool onKeyDown( const Input::KeyEvent& key ) override;
        /// Key released: return true if input was consumed
        [[nodiscard]] bool onKeyUp( const Input::KeyEvent& key ) override;
        /// Joystick axis change: return true if input was consumed
        [[nodiscard]] bool joystickAxisMoved( const Input::JoystickEvent& arg ) override;
        /// Joystick direction change: return true if input was consumed
        [[nodiscard]] bool joystickPovMoved( const Input::JoystickEvent& arg ) override;
        /// Joystick button pressed: return true if input was consumed
        [[nodiscard]] bool joystickButtonPressed( const Input::JoystickEvent& arg ) override;
        /// Joystick button released: return true if input was consumed
        [[nodiscard]] bool joystickButtonReleased( const Input::JoystickEvent& arg ) override;
        [[nodiscard]] bool joystickBallMoved( const Input::JoystickEvent& arg ) override;
        // return true if input was consumed
        [[nodiscard]] bool joystickAddRemove( const Input::JoystickEvent& arg ) override;
        [[nodiscard]] bool joystickRemap( const Input::JoystickEvent& arg ) override;
        /// Mouse moved: return true if input was consumed
        [[nodiscard]] bool mouseMoved( const Input::MouseMoveEvent& arg ) override;
        /// Mouse button pressed: return true if input was consumed
        [[nodiscard]] bool mouseButtonPressed( const Input::MouseButtonEvent& arg ) override;
        /// Mouse button released: return true if input was consumed
        [[nodiscard]] bool mouseButtonReleased( const Input::MouseButtonEvent& arg ) override;

        [[nodiscard]] bool onTextEvent( const Input::TextEvent& arg ) override;

        [[nodiscard]] bool switchScene( const Str<256>& name, bool unloadPrevious, bool deferToIdle = true, bool threaded = true );

        /// Called if a mouse move event was captured by a different system (editor, gui, etc).
        /// Used to cancel scene specific mouse move tracking
        void mouseMovedExternally( const Input::MouseMoveEvent& arg );

        PROPERTY_RW( bool, wantsMouse, false );

    protected:
        bool networkUpdate( U64 frameCount );

    protected:
        void initPostLoadState() noexcept;
        Scene* load( const Str<256>& sceneName );
        bool   unloadScene( Scene* scene );

        // Add a new player to the simulation
        void addPlayerInternal( Scene& parentScene, SceneGraphNode* playerNode );
        // Removes the specified player from the active simulation
        // Returns true if the player was previously registered
        // On success, player pointer will be reset
        void removePlayerInternal( Scene& parentScene, SceneGraphNode* playerNode );

        // Add a new player to the simulation
        void addPlayer( Scene& parentScene, SceneGraphNode* playerNode, bool queue );
        // Removes the specified player from the active simulation
        // Returns true if the player was previously registered
        // On success, player pointer will be reset
        void removePlayer( Scene& parentScene, SceneGraphNode* playerNode, bool queue );
        void getNodesInScreenRect( const Rect<I32>& screenRect, const Camera& camera, vector_fast<SceneGraphNode*>& nodesOut ) const;

        [[nodiscard]] bool switchSceneInternal();

    protected:
        [[nodiscard]] bool frameStarted( const FrameEvent& evt ) override;
        [[nodiscard]] bool frameEnded( const FrameEvent& evt ) override;

        void drawCustomUI( const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        void postRender( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        void debugDraw( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        void prepareLightData( RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut );
        [[nodiscard]] Camera* playerCamera( bool skipOverride = false ) const noexcept;
        [[nodiscard]] Camera* playerCamera( PlayerIndex idx, bool skipOverride = false ) const noexcept;
        void editorPreviewNode( const I64 editorPreviewNode ) noexcept;
        void currentPlayerPass( PlayerIndex idx );
        BoundingSphere moveCameraToNode( Camera* camera, const SceneGraphNode* targetNode ) const;
        bool saveNode( const SceneGraphNode* targetNode ) const;
        bool loadNode( SceneGraphNode* targetNode ) const;
        SceneNode_ptr createNode( SceneNodeType type, const ResourceDescriptor& descriptor );
        std::pair<Texture_ptr, SamplerDescriptor> getSkyTexture() const;

    private:
        bool _init = false;
        bool _processInput = false;
        /// Pointer to the hardware objects
        PlatformContext* _platformContext = nullptr;
        /// Pointer to the general purpose resource cache
        ResourceCache* _resourceCache = nullptr;

        Task* _saveTask = nullptr;
        PlayerIndex _currentPlayerPass = 0u;
        ScenePool* _scenePool = nullptr;
        U64 _elapsedAppTime = 0ULL;
        U32 _elapsedAppTimeMS = 0u;
        U64 _elapsedGameTime = 0ULL;
        U32 _elapsedGameTimeMS = 0u;
        U64 _saveTimer = 0ULL;

        std::array<Time::ProfileTimer*, to_base( RenderStage::COUNT )> _sceneGraphCullTimers;
        PlayerList _players = {};
        U8 _activePlayerCount = 0u;

        bool _playerQueueDirty = false;
        eastl::queue<std::pair<Scene*, SceneGraphNode*>>  _playerAddQueue;
        eastl::queue<std::pair<Scene*, SceneGraphNode*>>  _playerRemoveQueue;
        AI::Navigation::DivideRecast_uptr _recast = nullptr;

        vector<std::pair<size_t, DELEGATE<void, U8 /*player index*/, const vector_fast<SceneGraphNode*>& /*nodes*/>> > _selectionChangeCallbacks;
        VisibleNodeList<> _recentlyRenderedNodes;
        struct SwitchSceneTarget
        {
            Str<256> _targetSceneName = "";
            bool _unloadPreviousScene = true;
            bool _loadInSeparateThread = true;
            bool _isSet = false;
        } _sceneSwitchTarget;

    };

    namespace Attorney
    {
        class SceneManagerScene
        {
            static void addPlayer( Divide::SceneManager& manager, Scene& parentScene, SceneGraphNode* playerNode, const bool queue )
            {
                manager.addPlayer( parentScene, playerNode, queue );
            }

            static void removePlayer( Divide::SceneManager& manager, Scene& parentScene, SceneGraphNode* playerNode, const bool queue )
            {
                manager.removePlayer( parentScene, playerNode, queue );
            }

            static void getNodesInScreenRect( const Divide::SceneManager& manager, const Rect<I32>& screenRect, const Camera& camera, vector_fast<SceneGraphNode*>& nodesOut )
            {
                manager.getNodesInScreenRect( screenRect, camera, nodesOut );
            }

            friend class Divide::Scene;
        };

        class SceneManagerKernel
        {
            static void initPostLoadState( Divide::SceneManager* manager ) noexcept
            {
                manager->initPostLoadState();
            }

            static void currentPlayerPass( Divide::SceneManager* manager, const PlayerIndex idx )
            {
                manager->currentPlayerPass( idx );
            }

            static bool networkUpdate( Divide::SceneManager* manager, const U64 frameCount )
            {
                return manager->networkUpdate( frameCount );
            }

            friend class Divide::Kernel;
        };

        class SceneManagerEditor
        {
            static SceneNode_ptr createNode( Divide::SceneManager* manager, const SceneNodeType type, const ResourceDescriptor& descriptor )
            {
                return manager->createNode( type, descriptor );
            }

            static SceneEnvironmentProbePool* getEnvProbes( const Divide::SceneManager* manager ) noexcept
            {
                return manager->getEnvProbes();
            }

            static bool saveNode( const Divide::SceneManager* mgr, const SceneGraphNode* targetNode )
            {
                return mgr->saveNode( targetNode );
            }

            static bool loadNode( const Divide::SceneManager* mgr, SceneGraphNode* targetNode )
            {
                return mgr->loadNode( targetNode );
            }

            static Camera* playerCamera( const Divide::SceneManager* mgr, bool skipOverride = false ) noexcept
            {
                return mgr->playerCamera( skipOverride );
            }

            static Camera* playerCamera( const Divide::SceneManager* mgr, PlayerIndex idx, bool skipOverride = false ) noexcept
            {
                return mgr->playerCamera( idx, skipOverride );
            }

            static void editorPreviewNode( Divide::SceneManager* mgr, const I64 editorPreviewNode )
            {
                return mgr->editorPreviewNode( editorPreviewNode );
            }
            friend class Divide::Editor;
        };

        class SceneManagerScenePool
        {
            static bool unloadScene( Divide::SceneManager& mgr, Scene* scene )
            {
                return mgr.unloadScene( scene );
            }

            friend class Divide::ScenePool;
        };

        class SceneManagerSSRAccessor
        {

            static std::pair<Texture_ptr, SamplerDescriptor> getSkyTexture( const Divide::SceneManager* mgr )
            {
                return mgr->getSkyTexture();
            }

            friend class Divide::SSRPreRenderOperator;
        };

        class SceneManagerCameraAccessor
        {
            static Camera* playerCamera( const Divide::SceneManager* mgr, const bool skipOverride = false ) noexcept
            {
                return mgr->playerCamera( skipOverride );
            }

            static Camera* playerCamera( const Divide::SceneManager& mgr, const bool skipOverride = false ) noexcept
            {
                return mgr.playerCamera( skipOverride );
            }

            static Camera* playerCamera( const Divide::SceneManager* mgr, const PlayerIndex idx, const bool skipOverride = false ) noexcept
            {
                return mgr->playerCamera( idx, skipOverride );
            }

            static Camera* playerCamera( const Divide::SceneManager& mgr, const PlayerIndex idx, const bool skipOverride = false ) noexcept
            {
                return mgr.playerCamera( idx, skipOverride );
            }

            static BoundingSphere moveCameraToNode( const Divide::SceneManager* mgr, Camera* camera, const SceneGraphNode* targetNode )
            {
                return mgr->moveCameraToNode( camera, targetNode );
            }

            friend class Divide::Scene;
            friend class Divide::Editor;
            friend class Divide::ShadowMap;
            friend class Divide::RenderPass;
            friend class Divide::DirectionalLightSystem;
            friend class Divide::ContentExplorerWindow;
            friend class Divide::SolutionExplorerWindow;
            friend class Divide::GUIConsoleCommandParser;
        };

        class SceneManagerRenderPass
        {
            static void cullScene( Divide::SceneManager* mgr, const NodeCullParams& cullParams, const U16 cullFlags, VisibleNodeList<>& nodesOut )
            {
                mgr->cullSceneGraph( cullParams, cullFlags, nodesOut );
            }

            static void findNode( Divide::SceneManager* mgr, const vec3<F32>& cameraEye, const I64 nodeGUID, VisibleNodeList<>& nodesOut )
            {
                mgr->findNode( cameraEye, nodeGUID, nodesOut );
            }

            static void initDefaultCullValues( Divide::SceneManager* mgr, const RenderStage stage, NodeCullParams& cullParamsInOut ) noexcept
            {
                mgr->initDefaultCullValues( stage, cullParamsInOut );
            }

            static void prepareLightData( Divide::SceneManager* mgr, const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                mgr->prepareLightData( stage, cameraSnapshot, memCmdInOut );
            }

            static void debugDraw( Divide::SceneManager* mgr, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                mgr->debugDraw( bufferInOut, memCmdInOut );
            }

            static void drawCustomUI( Divide::SceneManager* mgr, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                mgr->drawCustomUI( targetViewport, bufferInOut, memCmdInOut );
            }

            static void postRender( Divide::SceneManager* mgr, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
            {
                mgr->postRender(bufferInOut, memCmdInOut);
            }

            static const Camera* playerCamera( const Divide::SceneManager* mgr ) noexcept
            {
                return mgr->playerCamera();
            }

            static const SceneStatePerPlayer& playerState( const Divide::SceneManager* mgr ) noexcept
            {
                return mgr->getActiveScene().state()->playerState();
            }

            static LightPool& lightPool( Divide::SceneManager* mgr )
            {
                return *mgr->getActiveScene().lightPool();
            }

            static  SceneRenderState& renderState( Divide::SceneManager* mgr ) noexcept
            {
                return mgr->getActiveScene().state()->renderState();
            }

            friend class Divide::RenderPass;
            friend class Divide::RenderPassManager;
            friend class Divide::RenderPassExecutor;
        };

    };  // namespace Attorney

};  // namespace Divide

#endif //DVD_SCENE_MANAGER_H
