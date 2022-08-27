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
#ifndef _SCENE_H_
#define _SCENE_H_

#include "SceneState.h"
#include "SceneInput.h"

#include "Core/Headers/PlatformContextComponent.h"
#include "Environment/Sky/Headers/Sun.h"
#include "Graphs/Headers/SceneNodeFwd.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide {

class Sky;
class GUI;
class Light;
class Object3D;
class LoadSave;
class ByteBuffer;
class IMPrimitive;
class ParticleData;
class ParamHandler;
class SceneManager;
class ResourceCache;
class SceneGraphNode;
class ParticleEmitter;
class PlatformContext;
class SceneShaderData;
class RenderPassManager;
class TerrainDescriptor;
class ResourceDescriptor;
class DirectionalLightComponent;
class EnvironmentProbeComponent;

FWD_DECLARE_MANAGED_CLASS(Mesh);
FWD_DECLARE_MANAGED_CLASS(Player);
FWD_DECLARE_MANAGED_CLASS(SceneGraph);
FWD_DECLARE_MANAGED_CLASS(LightPool);
FWD_DECLARE_MANAGED_CLASS(SceneGUIElements);
FWD_DECLARE_MANAGED_CLASS(SceneEnvironmentProbePool);

namespace AI {
    FWD_DECLARE_MANAGED_CLASS(AIManager);
}

namespace GFX {
    class CommandBuffer;
}

namespace Attorney {
    class SceneManager;
    class SceneGraph;
    class SceneRenderPass;
    class SceneLoadSave;
    class SceneGUI;
    class SceneInput;
    class SceneEnvironmentProbeComponent;
}

struct Selections
{
    static constexpr U8 MAX_SELECTIONS = 254u;

    std::array<I64, MAX_SELECTIONS> _selections = create_array<MAX_SELECTIONS, I64>(-1);
    U8 _selectionCount = 0u;
};

struct DragSelectData
{
    Rect<I32> _sourceViewport;
    Rect<I32> _targetViewport;
    vec2<I32> _startDragPos;
    vec2<I32> _endDragPos;
    bool _isDragging = false;
};

class Scene : public Resource, public PlatformContextComponent {
    friend class Attorney::SceneManager;
    friend class Attorney::SceneGraph;
    friend class Attorney::SceneRenderPass;
    friend class Attorney::SceneLoadSave;
    friend class Attorney::SceneGUI;
    friend class Attorney::SceneInput;
    friend class Attorney::SceneEnvironmentProbeComponent;

    public:
        static constexpr U32 SUN_LIGHT_TAG  = 0xFFF0F0;

        static I64 DEFAULT_SCENE_GUID;

        struct DayNightData
        {
            Sky* _skyInstance = nullptr;
            DirectionalLightComponent* _sunLight = nullptr;
            F32 _speedFactor = 1.0f;
            F32 _timeAccumulatorSec = 0.0f;
            F32 _timeAccumulatorHour = 0.0f;
            SimpleTime _time = { 14u, 30u };
            SimpleLocation _location = { 51.4545f, -2.5879f };
            bool _resetTime = true;
        };

    protected:
        static Mutex s_perFrameArenaMutex;
        static MyArena<Config::REQUIRED_RAM_SIZE_IN_BYTES / 3> s_perFrameArena;

    protected:
        static bool OnStartup(PlatformContext& context);
        static bool OnShutdown(PlatformContext& context);
        static string GetPlayerSGNName(PlayerIndex idx);

    public:

        explicit Scene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name);
        virtual ~Scene();

        /// Scene is rendering, so add intensive tasks here to save CPU cycles
        [[nodiscard]] bool idle();

#pragma region Logic Loop
        /// Get all input commands from the user
        virtual void processInput(PlayerIndex idx, U64 deltaTimeUS);
        /// Update the scene based on the inputs
        virtual void processTasks(U64 deltaTimeUS);
        virtual void processGUI(U64 deltaTimeUS);
        /// The application has lost or gained focus
        virtual void onChangeFocus(bool hasFocus);
        virtual void updateSceneStateInternal(U64 deltaTimeUS);
        /// Update animations, network data, sounds, triggers etc.
        void updateSceneState(U64 deltaTimeUS);
#pragma endregion

#pragma region Task Management
        void registerTask(Task& taskItem, bool start = true, TaskPriority priority = TaskPriority::DONT_CARE);
        void clearTasks();
        void removeTask(const Task& task);
#pragma endregion

#pragma region Object Picking
        [[nodiscard]] const Selections& getCurrentSelection(const PlayerIndex index = 0) const;
        [[nodiscard]] bool              findSelection(PlayerIndex idx, bool clearOld);

        [[nodiscard]] bool resetSelection(PlayerIndex idx, const bool resetIfLocked);
        void setSelected(PlayerIndex idx, const vector<SceneGraphNode*>& SGNs, bool recursive);

        void beginDragSelection(PlayerIndex idx, const vec2<I32>& mousePos);
        void endDragSelection(PlayerIndex idx, bool clearSelection);
#pragma endregion

#pragma region Entity Management
                      void            addMusic(MusicType type, const Str64& name, const ResourcePath& srcFile) const;
        [[nodiscard]] SceneGraphNode* addSky(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
        [[nodiscard]] SceneGraphNode* addInfPlane(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
                      void            addWater(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
                      void            addTerrain(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
        [[nodiscard]] SceneGraphNode* addParticleEmitter(const Str64& name, std::shared_ptr<ParticleData> data,SceneGraphNode* parentNode) const;
#pragma endregion

#pragma region Time Of Day
        void initDayNightCycle(Sky& skyInstance, DirectionalLightComponent& sunLight) noexcept;

                      /// Negative values should work
                      void setDayNightCycleTimeFactor(F32 factor) noexcept;
        [[nodiscard]] F32  getDayNightCycleTimeFactor() const noexcept;

                      void              setTimeOfDay(const SimpleTime& time) noexcept;
        [[nodiscard]] const SimpleTime& getTimeOfDay() const noexcept;

                      void                  setGeographicLocation(const SimpleLocation& location) noexcept;
        [[nodiscard]] const SimpleLocation& getGeographicLocation() const noexcept;

        [[nodiscard]] vec3<F32>  getSunPosition() const;
        [[nodiscard]] vec3<F32>  getSunDirection() const;
        [[nodiscard]] SunInfo    getCurrentSunDetails() const noexcept;
        [[nodiscard]] Atmosphere getCurrentAtmosphere() const noexcept;
                      void       setCurrentAtmosphere(const Atmosphere& atmosphere) const noexcept;
#pragma endregion

#pragma region Player Camera
        [[nodiscard]] Camera* playerCamera(const bool skipOverride = false) const;
        [[nodiscard]] Camera* playerCamera(U8 index, const bool skipOverride = false) const;
        bool lockCameraToPlayerMouse(PlayerIndex index, bool lockState) const noexcept;
#pragma endregion

        /// Contains all game related info for the scene (wind speed, visibility ranges, etc)
        PROPERTY_R(SceneState_uptr, state);
        PROPERTY_RW(bool, dayNightCycleEnabled, true);
        PROPERTY_R_IW(DayNightData, dayNightData);
        PROPERTY_R(SceneGraph_uptr, sceneGraph);
        PROPERTY_R(AI::AIManager_uptr, aiManager);
        PROPERTY_R(SceneGUIElements_uptr, GUI);
        POINTER_R(ResourceCache, resourceCache, nullptr);
        PROPERTY_R(LightPool_uptr, lightPool);
        PROPERTY_R(SceneInput_uptr, input);
        PROPERTY_R(SceneEnvironmentProbePool_uptr, envProbePool);
        PROPERTY_R_IW(bool, loadComplete, false);
        PROPERTY_R_IW(U64, sceneRuntimeUS, 0ULL);
    protected:
                      virtual void onSetActive();
                      virtual void onRemoveActive();
        [[nodiscard]] virtual bool frameStarted();
        [[nodiscard]] virtual bool frameEnded();

        /// Returns the first available action ID
        [[nodiscard]] virtual U16 registerInputActions();

        void rebuildShaders(bool selectionOnly = true) const;

        void          onNodeDestroy(SceneGraphNode* node);
        SceneNode_ptr createNode(SceneNodeType type, const ResourceDescriptor& descriptor) const;
        void          loadAsset(const Task* parentTask, const XML::SceneNode& sceneNode, SceneGraphNode* parent);

        /// Draw debug entities
        virtual void debugDraw(GFX::CommandBuffer& bufferInOut);
        /// Draw custom ui elements
        virtual void drawCustomUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut);
        /// Return true if input was consumed
        [[nodiscard]] virtual bool mouseMoved(const Input::MouseMoveEvent& arg);

#pragma region Save Load
        [[nodiscard]] virtual bool save(ByteBuffer& outputBuffer) const;
        [[nodiscard]] virtual bool load(ByteBuffer& inputBuffer);

        /// Can save at any time, I guess?
        [[nodiscard]] virtual bool saveXML(const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "") const;

        [[nodiscard]]         bool saveNodeToXML(const SceneGraphNode* node) const;
        [[nodiscard]]         bool loadNodeFromXML(SceneGraphNode* node) const;
        [[nodiscard]] virtual bool loadXML();

        [[nodiscard]] virtual bool load();
        [[nodiscard]] virtual bool unload();
        [[nodiscard]] virtual bool postLoad();
        /// Gets called on the main thread when the scene finishes loading (e.g. used by the GUI system)
        virtual void postLoadMainThread();
#pragma endregion

#pragma region Player Management
        void findHoverTarget(PlayerIndex idx, const vec2<I32>& aimPos);
        void clearHoverTarget(PlayerIndex idx);
        void toggleFlashlight(PlayerIndex idx);

        void addPlayerInternal(bool queue);
        void removePlayerInternal(PlayerIndex idx);
        void onPlayerAdd(const Player_ptr& player);
        void onPlayerRemove(const Player_ptr& player);
        void currentPlayerPass(U64 deltaTimeUS, PlayerIndex idx);

        [[nodiscard]] U8      getSceneIndexForPlayer(PlayerIndex idx) const;
        [[nodiscard]] Player* getPlayerForIndex(PlayerIndex idx) const;
        [[nodiscard]] U8      getPlayerIndexForDevice(U8 deviceIndex) const;
#pragma endregion

    private:
        /// Returns true if the camera was moved/rotated/etc
        bool updateCameraControls(U64 deltaTimeUS, PlayerIndex idx) const;
        void updateSelectionData(PlayerIndex idx, DragSelectData& data, bool remapped);
        [[nodiscard]] bool checkCameraUnderwater(PlayerIndex idx) const;
        [[nodiscard]] bool checkCameraUnderwater(const Camera& camera) const noexcept;
        [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Scene"; }

    protected:
        SceneManager&                         _parent;
        vector<Player*>                       _scenePlayers;
        vector<D64>                           _taskTimers;
        vector<D64>                           _guiTimersMS;
        std::atomic_uint                      _loadingTasks;
        XML::SceneNode                        _xmlSceneGraphRootNode;

        std::array<Selections,      Config::MAX_LOCAL_PLAYER_COUNT> _currentSelection;
        std::array<Selections,      Config::MAX_LOCAL_PLAYER_COUNT> _tempSelection;
        std::array<I64,             Config::MAX_LOCAL_PLAYER_COUNT> _currentHoverTarget;
        std::array<DragSelectData,  Config::MAX_LOCAL_PLAYER_COUNT> _dragSelectData;
        std::array<SceneGraphNode*, Config::MAX_LOCAL_PLAYER_COUNT> _flashLight;
        std::array<U32,             Config::MAX_LOCAL_PLAYER_COUNT> _cameraUpdateListeners;
    private:
        SharedMutex          _tasksMutex;
        vector<Task*>        _tasks;
        vector<SGNRayResult> _sceneSelectionCandidates;

    protected:
        IMPrimitive*         _linesPrimitive = nullptr;
        vector<size_t>       _selectionCallbackIndices;
};

namespace Attorney {
class SceneManager {
   private:
    static bool loadComplete(const Scene& scene) noexcept {
        return scene.loadComplete();
    }

    static void onPlayerAdd(Scene& scene, const Player_ptr& player) {
        scene.onPlayerAdd(player);
    }

    static void onPlayerRemove(Scene& scene, const Player_ptr& player) {
        scene.onPlayerRemove(player);
    }

    static void currentPlayerPass(Scene& scene, const U64 deltaTimeUS, const PlayerIndex idx) {
        scene.currentPlayerPass(deltaTimeUS, idx);
    }

    static void debugDraw(Scene& scene, GFX::CommandBuffer& bufferInOut) {
        scene.debugDraw(bufferInOut);
    }

    static void drawCustomUI(Scene& scene, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) {
        scene.drawCustomUI(targetViewport, bufferInOut);
    }

    static bool frameStarted(Scene& scene) { 
        return scene.frameStarted();
    }

    static bool frameEnded(Scene& scene) {
        return scene.frameEnded();
    }

    static bool load(Scene& scene) {
        return scene.load();
    }

    static bool unload(Scene& scene) { 
        return scene.unload();
    }

    static void postLoadMainThread(Scene& scene) {
        scene.postLoadMainThread();
    }

    static void onSetActive(Scene& scene) {
        scene.onSetActive();
    }

    static void onRemoveActive(Scene& scene) {
        scene.onRemoveActive();
    }

    static bool onStartup(PlatformContext& context) {
        return Scene::OnStartup(context);
    }

    static bool onShutdown(PlatformContext& context) {
        return Scene::OnShutdown(context);
    }

    static SceneGUIElements* gui(const Scene& scene) noexcept {
        return scene._GUI.get();
    }

    static bool resetSelection(Scene& scene, const PlayerIndex idx, const bool resetIfLocked) {
        return scene.resetSelection(idx, resetIfLocked);
    }

    static void setSelected(Scene& scene, const PlayerIndex idx, const vector<SceneGraphNode*>& sgns, const bool recursive) {
        scene.setSelected(idx, sgns, recursive);
    }

    static void clearHoverTarget(Scene& scene, const Input::MouseMoveEvent& arg) {
        scene.clearHoverTarget(scene.input()->getPlayerIndexForDevice(arg._deviceIndex));
    }

    static SceneNode_ptr createNode(const Scene& scene, const SceneNodeType type, const ResourceDescriptor& descriptor) {
        return scene.createNode(type, descriptor);
    }

    static SceneEnvironmentProbePool* getEnvProbes(const Scene& scene) noexcept {
        return scene._envProbePool.get();
    }

    friend class Divide::SceneManager;
};

class SceneRenderPass {
    static SceneEnvironmentProbePool* getEnvProbes(const Scene& scene) noexcept {
        return scene._envProbePool.get();
    }

    friend class Divide::RenderPass;
    friend class Divide::RenderPassManager;
};

class SceneEnvironmentProbeComponent
{
    static void registerProbe(const Scene& scene, EnvironmentProbeComponent* probe);
    static void unregisterProbe(const Scene& scene, const EnvironmentProbeComponent* const probe);

    friend class Divide::EnvironmentProbeComponent;
};

class SceneLoadSave {
    static bool save(const Scene& scene, ByteBuffer& outputBuffer) {
        return scene.save(outputBuffer);
    }

    static bool load(Scene& scene, ByteBuffer& inputBuffer) {
        return scene.load(inputBuffer);
    }


    static bool saveNodeToXML(const Scene& scene, const SceneGraphNode* node) {
        return scene.saveNodeToXML(node);
    }

    static bool loadNodeFromXML(const Scene& scene, SceneGraphNode* node) {
        return scene.loadNodeFromXML(node);
    }  
    
    static bool saveXML(const Scene& scene, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride = "") {
        return scene.saveXML(msgCallback, finishCallback, sceneNameOverride);
    }

    friend class Divide::LoadSave;
};

class SceneGraph {
    static void onNodeDestroy(Scene& scene, SceneGraphNode* node) {
        scene.onNodeDestroy(node);
    }

    static SceneEnvironmentProbePool* getEnvProbes(const Scene& scene) noexcept {
        return scene._envProbePool.get();
    } 
    
    static LightPool* getLightPool(const Scene& scene) noexcept {
        return scene.lightPool().get();
    }

    static void addSceneGraphToLoad(Scene& scene, const XML::SceneNode&& rootNode) { 
        scene._xmlSceneGraphRootNode = rootNode; 
    }

    friend class Divide::SceneGraph;
};

class SceneGUI {
    static SceneGUIElements* guiElements(const Scene& scene) noexcept {
        return scene._GUI.get();
    }

    friend class Divide::GUI;
};

class SceneInput {
    static bool mouseMoved(Scene& scene, const Input::MouseMoveEvent& arg) {
        return scene.mouseMoved(arg);
    }

    friend class Divide::SceneInput;
};

}  // namespace Attorney

#pragma region Scene Factory
namespace SceneList {
    template<typename T>
    using SharedPtrFactory = boost::factory<std::shared_ptr<T>>;
    using ScenePtrFactory = std::function<std::shared_ptr<Scene>(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name)>;
    using SceneFactoryMap = std::unordered_map<U64, ScenePtrFactory>;
    using SceneNameMap = std::unordered_map<U64, Str256>;

    void registerSceneFactory(const char* name, const ScenePtrFactory& factoryFunc);

    template<typename T>
    inline void registerScene(const char* name, const SharedPtrFactory<T>& scenePtr) {
        registerSceneFactory(name, scenePtr);
    }
}

#define STRUCT_NAME(M) BOOST_PP_CAT(M, RegisterStruct)
#define REGISTER_SCENE(SceneName)                                                    \
class SceneName;                                                                     \
static struct STRUCT_NAME(SceneName) {                                               \
  STRUCT_NAME(SceneName)()                                                           \
  {                                                                                  \
     SceneList::registerScene(#SceneName, SceneList::SharedPtrFactory<SceneName>()); \
  }                                                                                  \
} BOOST_PP_CAT(SceneName, RegisterVariable);
#define BEGIN_SCENE(SceneName)         \
REGISTER_SCENE(SceneName);             \
class SceneName final : public Scene { \
    public:
#define END_SCENE(SceneName) };
#pragma endregion

}  // namespace Divide

#endif