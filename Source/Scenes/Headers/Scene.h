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
#ifndef DVD_SCENE_H_
#define DVD_SCENE_H_

#include "SceneState.h"

#include "Core/Resources/Headers/Resource.h"
#include "Core/Headers/PlatformContextComponent.h"
#include "Environment/Sky/Headers/Sun.h"
#include "Graphs/Headers/SceneNodeFwd.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide {

class Camera;
struct Task;

namespace Input {
    struct MouseMoveEvent;
    enum class InputDeviceType : U8;
}

class Sky;
class GUI;
class Light;
class Project;
class Object3D;
class LoadSave;
class ByteBuffer;
class IMPrimitive;
class ParticleData;
class ProjectManager;
class SceneGraphNode;
class ParticleEmitter;
class PlatformContext;
class SceneShaderData;
class RenderPassManager;
class DirectionalLightComponent;
class EnvironmentProbeComponent;

struct SceneEntry;
struct InputParams;
struct ResourceDescriptorBase;

FWD_DECLARE_MANAGED_CLASS(Mesh);
FWD_DECLARE_MANAGED_CLASS(Player);
FWD_DECLARE_MANAGED_CLASS(LightPool);
FWD_DECLARE_MANAGED_CLASS(SceneGraph);
FWD_DECLARE_MANAGED_CLASS(SceneInput);
FWD_DECLARE_MANAGED_CLASS(SceneGUIElements);
FWD_DECLARE_MANAGED_CLASS(SceneEnvironmentProbePool);

namespace AI {
    FWD_DECLARE_MANAGED_CLASS(AIManager);
}

namespace GFX
{
    class CommandBuffer;
    struct MemoryBarrierCommand;
}

namespace Attorney {
    class SceneProjectManager;
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
    int2 _startDragPos;
    int2 _endDragPos;
    bool _isDragging = false;
    bool _simulationPaused = false;
};

struct SceneEntry
{
    Str<256> _name{};
};

using SceneEntries = vector<SceneEntry>;
using PlayerList = eastl::array<Player_ptr, Config::MAX_LOCAL_PLAYER_COUNT>;

class Scene : public Resource, public PlatformContextComponent {
    friend class Attorney::SceneProjectManager;
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

        /// Return the full path to the scene's location on disk. It's equivalent to GetSceneRootFolder(scene.parent()) + scene.name(). (e.g. ./Projects/Foo/Scenes/MyScene/ for project "Foo" and scene "MyScene")
        [[nodiscard]] static ResourcePath GetSceneFullPath( const Scene& scene );
        /// Return the full path to the location of Scenes folder in the project (e.g. ./Projects/Foo/Scenes/ for project "Foo")
        [[nodiscard]] static ResourcePath GetSceneRootFolder( const Project& project );

    protected:
        static bool OnStartup(PlatformContext& context);
        static bool OnShutdown(PlatformContext& context);
        static string GetPlayerSGNName(PlayerIndex idx);

    public:

        explicit Scene(PlatformContext& context, Project& parent, const SceneEntry& entry);
        virtual ~Scene() override;

        /// Scene is rendering, so add intensive tasks here to save CPU cycles
        [[nodiscard]] bool idle();

        [[nodiscard]] inline Project& parent() noexcept { return _parent; }
        [[nodiscard]] inline const Project& parent() const noexcept { return _parent; }

#pragma region Logic Loop
        /// Get all input commands from the user
        virtual void processInput(PlayerIndex idx, U64 gameDeltaTimeUS, U64 appDeltaTimeUS );
        /// Update the scene based on the inputs
        virtual void processTasks( U64 gameDeltaTimeUS, U64 appDeltaTimeUS );
        virtual void processGUI(U64 gameDeltaTimeUS, U64 appDeltaTimeUS);
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
        [[nodiscard]] bool              findSelection(PlayerIndex idx, bool clearOld, bool recursive);

        [[nodiscard]] bool resetSelection(PlayerIndex idx, const bool resetIfLocked);
        void setSelected(PlayerIndex idx, const vector<SceneGraphNode*>& SGNs, bool recursive);

        void beginDragSelection(PlayerIndex idx, int2 mousePos);
        void endDragSelection(PlayerIndex idx, bool clearSelection);
#pragma endregion

#pragma region Entity Management
                      void            addMusic(MusicType type, const std::string_view name, const ResourcePath& srcFile);
        [[nodiscard]] SceneGraphNode* addSky(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName = "");
        [[nodiscard]] SceneGraphNode* addInfPlane(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName = "");
                      void            addWater(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName = "");
                      void            addTerrain(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str<64>& nodeName = "");
        [[nodiscard]] SceneGraphNode* addParticleEmitter(const std::string_view name, std::shared_ptr<ParticleData> data,SceneGraphNode* parentNode);
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

        [[nodiscard]] float3  getSunPosition() const;
        [[nodiscard]] float3  getSunDirection() const;
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
        PROPERTY_R(LightPool_uptr, lightPool);
        PROPERTY_R(SceneInput_uptr, input);
        PROPERTY_R(SceneEnvironmentProbePool_uptr, envProbePool);
        PROPERTY_R_IW(bool, loadComplete, false);
        PROPERTY_R_IW(U64, sceneRuntimeUS, 0ULL);
        PROPERTY_R(SceneEntry, entry);
        PROPERTY_R(U8, playerCount, 0u);

    protected:
        enum class TimerClass : U8
        {
            GAME_TIME = 0,
            APP_TIME,
            COUNT
        };

                      virtual void onSetActive();
                      virtual void onRemoveActive();
        [[nodiscard]] virtual bool frameStarted();
        [[nodiscard]] virtual bool frameEnded();

        /// Returns the first available action ID
        [[nodiscard]] virtual U16 registerInputActions();

        void       onNodeSpatialChange(const SceneGraphNode& node);
        void       onNodeDestroy(SceneGraphNode* node);
        void       loadAsset(const Task* parentTask, const XML::SceneNode& sceneNode, SceneGraphNode* parent);

        /// Draw debug entities
        virtual void debugDraw(GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        /// Draw custom ui elements
        virtual void drawCustomUI( const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut );
        /// Return true if input was consumed
        [[nodiscard]] virtual bool mouseMoved(const Input::MouseMoveEvent& arg);

#pragma region Save Load
        [[nodiscard]] virtual bool save(ByteBuffer& outputBuffer) const;
        [[nodiscard]] virtual bool load(ByteBuffer& inputBuffer);

        /// Can save at any time, I guess?
        [[nodiscard]] virtual bool saveXML(const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback) const;

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
        void findHoverTarget(PlayerIndex idx, int2 aimPos, bool recursive);
        void clearHoverTarget(PlayerIndex idx);
        void toggleFlashlight(PlayerIndex idx);

        void addPlayerInternal(bool queue);
        void removePlayerInternal(PlayerIndex idx);
        void onPlayerAdd(const Player_ptr& player);
        void onPlayerRemove(const Player_ptr& player);
        void currentPlayerPass( PlayerIndex idx);

        [[nodiscard]] U8          getSceneIndexForPlayer(PlayerIndex idx) const;
        [[nodiscard]] Player*     getPlayerForIndex(PlayerIndex idx) const;
        [[nodiscard]] PlayerIndex getPlayerIndexForDevice(Input::InputDeviceType deviceType, U32 deviceIndex) const;
#pragma endregion

        void addGuiTimer( TimerClass intervalClass, U64 intervalUS, DELEGATE<void, U64/*elapsed time*/> cbk);
        void addTaskTimer( TimerClass intervalClass, U64 intervalUS, DELEGATE<void, U64/*elapsed time*/> cbk);

    private:
        struct TimerStruct
        {
            U64 _internalTimer{0u};
            U64 _internalTimerTotal{0u};
            const U64 _callbackIntervalUS{0u};
            const TimerClass _timerClass{TimerClass::GAME_TIME};
            DELEGATE<void, U64/*elapsed time US*/> _cbk;
        };

    private:
        void processInternalTimers( U64 appDeltaUS, U64 gameDeltaUS, vector<TimerStruct>& timers );
        /// Returns true if the camera was moved/rotated/etc
        bool updateCameraControls(PlayerIndex idx) const;
        void updateSelectionData(PlayerIndex idx, DragSelectData& data);
        [[nodiscard]] bool checkCameraUnderwater(PlayerIndex idx) const;
        [[nodiscard]] bool checkCameraUnderwater(const Camera& camera) const noexcept;

    protected:
        Project&         _parent;

        PlayerList _scenePlayers;

        std::atomic_uint _loadingTasks{0u};
        XML::SceneNode   _xmlSceneGraphRootNode;
        IMPrimitive*     _linesPrimitive = nullptr;
        vector<size_t>   _selectionCallbackIndices;

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

        vector<TimerStruct> _taskTimers;
        vector<TimerStruct> _guiTimers;
};

namespace Attorney {
class SceneProjectManager
{
   private:
    static bool loadComplete(Scene* scene) noexcept
    {
        return scene->loadComplete();
    }

    static void onPlayerAdd(Scene* scene, const Player_ptr& player) {
        scene->onPlayerAdd(player);
    }

    static void onPlayerRemove(Scene* scene, const Player_ptr& player) {
        scene->onPlayerRemove(player);
    }

    static void currentPlayerPass(Scene* scene, const PlayerIndex idx) {
        scene->currentPlayerPass(idx);
    }

    static void debugDraw(Scene* scene, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) {
        scene->debugDraw(bufferInOut, memCmdInOut);
    }

    static void drawCustomUI(Scene* scene, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) {
        scene->drawCustomUI(targetViewport, bufferInOut, memCmdInOut);
    }

    static bool frameStarted(Scene* scene) { 
        return scene->frameStarted();
    }

    static bool frameEnded(Scene* scene) {
        return scene->frameEnded();
    }

    static bool load(Scene* scene) {
        return scene->load();
    }

    static bool unload(Scene* scene) { 
        return scene->unload();
    }

    static void postLoadMainThread(Scene* scene) {
        scene->postLoadMainThread();
    }

    static void onSetActive(Scene* scene) {
        scene->onSetActive();
    }

    static void onRemoveActive(Scene* scene)
    {
        scene->onRemoveActive();
    }

    static bool onStartup(PlatformContext& context) {
        return Scene::OnStartup(context);
    }

    static bool onShutdown(PlatformContext& context) {
        return Scene::OnShutdown(context);
    }

    static SceneGUIElements* gui(Scene* scene) noexcept {
        return scene->_GUI.get();
    }

    static bool resetSelection(Scene* scene, const PlayerIndex idx, const bool resetIfLocked) {
        return scene->resetSelection(idx, resetIfLocked);
    }

    static void setSelected(Scene* scene, const PlayerIndex idx, const vector<SceneGraphNode*>& sgns, const bool recursive) {
        scene->setSelected(idx, sgns, recursive);
    }

    static void clearHoverTarget(Scene* scene, const Input::MouseMoveEvent& arg);

    static SceneEnvironmentProbePool* getEnvProbes(Scene* scene) noexcept {
        return scene->_envProbePool.get();
    }

    [[nodiscard]] static PlayerList& getPlayers( Scene* scene) noexcept
    {
        return scene->_scenePlayers;
    }

    friend class Divide::Project;
    friend class Divide::ProjectManager;
};

class SceneRenderPass {
    static SceneEnvironmentProbePool* getEnvProbes(Scene* scene) noexcept
    {
        return scene->_envProbePool.get();
    }

    friend class Divide::RenderPass;
    friend class Divide::RenderPassManager;
};

class SceneEnvironmentProbeComponent
{
    static void registerProbe(Scene* scene, EnvironmentProbeComponent* probe);
    static void unregisterProbe(Scene* scene, const EnvironmentProbeComponent* const probe);

    friend class Divide::EnvironmentProbeComponent;
};

class SceneLoadSave {
    static bool save(const Scene* scene, ByteBuffer& outputBuffer) {
        return scene->save(outputBuffer);
    }

    static bool load(Scene* scene, ByteBuffer& inputBuffer) {
        return scene->load(inputBuffer);
    }

    static bool saveNodeToXML(Scene* scene, const SceneGraphNode* node) {
        return scene->saveNodeToXML(node);
    }

    static bool loadNodeFromXML(Scene* scene, SceneGraphNode* node) {
        return scene->loadNodeFromXML(node);
    }  
    
    static bool saveXML(Scene* scene, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback ) {
        return scene->saveXML(msgCallback, finishCallback);
    }

    friend class Divide::LoadSave;
};

class SceneGraph
{
    static void onNodeDestroy(Scene* scene, SceneGraphNode* node)
    {
        scene->onNodeDestroy(node);
    }

    static void onNodeSpatialChange(Scene* scene, const SceneGraphNode& node)
    {
        scene->onNodeSpatialChange(node);
    }

    static SceneEnvironmentProbePool* getEnvProbes(Scene* scene) noexcept
    {
        return scene->_envProbePool.get();
    } 
    
    static LightPool* getLightPool(Scene* scene) noexcept
    {
        return scene->lightPool().get();
    }

    static void addSceneGraphToLoad(Scene* scene, const XML::SceneNode&& rootNode)
    { 
        scene->_xmlSceneGraphRootNode = rootNode; 
    }

    friend class Divide::SceneGraph;
};

class SceneGUI {
    static SceneGUIElements* guiElements(Scene* scene) noexcept {
        return scene->_GUI.get();
    }

    friend class Divide::GUI;
};

class SceneInput {
    static bool mouseMoved(Scene* scene, const Input::MouseMoveEvent& arg) {
        return scene->mouseMoved(arg);
    }

    friend class Divide::SceneInput;
};

}  // namespace Attorney

#pragma region Scene Factory
namespace SceneList {
    template<typename T>
    using SharedPtrFactory = boost::factory<std::shared_ptr<T>>;
    using ScenePtrFactory = std::function<std::shared_ptr<Scene>(PlatformContext& context, Project& parent, const SceneEntry& name)>;
    using SceneFactoryMap = std::unordered_map<U64, ScenePtrFactory>;
    using SceneNameMap = std::unordered_map<U64, Str<256>>;

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

#endif //DVD_SCENE_H
