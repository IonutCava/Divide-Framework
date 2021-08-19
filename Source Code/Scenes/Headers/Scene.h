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
#include "Platform/Threading/Headers/Task.h"

/*All these includes are useful for a scene, so instead of forward declaring the
  classes, we include the headers
  to make them available in every scene source file. To reduce compile times,
  forward declare the "Scene" class instead
*/

#include "GUI/Headers/GUI.h"
#include "GUI/Headers/SceneGUIElements.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/LightPool.h"
#include "AI/Headers/AIManager.h"
#include "SceneEnvironmentProbePool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Environment/Sky/Headers/Sky.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide {
class SceneShaderData;
class Sky;
class Light;
class Object3D;
class LoadSave;
class ParamHandler;
class TerrainDescriptor;
class ParticleEmitter;
class EnvironmentProbeComponent;

FWD_DECLARE_MANAGED_CLASS(Mesh);
FWD_DECLARE_MANAGED_CLASS(Player);

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
    static constexpr U8 MaxSelections = 254u;

    std::array<I64, MaxSelections> _selections;
    U8 _selectionCount = 0u;

    void reset() noexcept {
        _selections.fill(-1);
        _selectionCount = 0u;
    }
};

struct DragSelectData
{
    Rect<I32> _sourceViewport;
    Rect<I32> _targetViewport;
    vec2<I32> _startDragPos;
    vec2<I32> _endDragPos;
    bool _isDragging = false;
};

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

/// The scene is a resource (to enforce load/unload and setName) and it has a 2 states:
/// one for game information and one for rendering information
class Scene : public Resource, public PlatformContextComponent {
    friend class Attorney::SceneManager;
    friend class Attorney::SceneGraph;
    friend class Attorney::SceneRenderPass;
    friend class Attorney::SceneLoadSave;
    friend class Attorney::SceneGUI;
    friend class Attorney::SceneInput;
    friend class Attorney::SceneEnvironmentProbeComponent;

   protected:
    static bool OnStartup(PlatformContext& context);
    static bool OnShutdown(PlatformContext& context);
    static stringImpl GetPlayerSGNName(PlayerIndex idx);

   public:
       static constexpr U32 SUN_LIGHT_TAG  = 0xFFF0F0;

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

   public:

    explicit Scene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name);
    virtual ~Scene();

    /**Begin scene logic loop*/
    /// Get all input commands from the user
    virtual void processInput(PlayerIndex idx, U64 deltaTimeUS);
    /// Update the scene based on the inputs
    virtual void processTasks(U64 deltaTimeUS);
    virtual void processGUI(U64 deltaTimeUS);
    /// Scene is rendering, so add intensive tasks here to save CPU cycles
    bool idle();  
    /// The application has lost focus
    void onLostFocus();  
    void onGainFocus();
    /**End scene logic loop*/

    /// Update animations, network data, sounds, triggers etc.
    void updateSceneState(U64 deltaTimeUS);
    void onStartUpdateLoop(U8 loopNumber) const;
    /// Override this for Scene specific updates
    virtual void updateSceneStateInternal(U64 deltaTimeUS) { ACKNOWLEDGE_UNUSED(deltaTimeUS); }

    SceneRenderState& renderState() { return _sceneState->renderState(); }
    const SceneRenderState& renderState() const { return _sceneState->renderState(); }

    SceneState* state()      const noexcept { return _sceneState.get(); }
    SceneInput* input()      const noexcept { return _input.get(); }
    SceneGraph* sceneGraph() const noexcept { return _sceneGraph.get(); }

    void registerTask(Task& taskItem, bool start = true, TaskPriority priority = TaskPriority::DONT_CARE);
    void clearTasks();
    void removeTask(Task& task);
    void addSceneGraphToLoad(XML::SceneNode& rootNode) { _xmlSceneGraphRootNode = rootNode; }

    void addMusic(MusicType type, const Str64& name, const ResourcePath& srcFile) const;

    SceneGraphNode* addSky(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
    SceneGraphNode* addInfPlane(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
    void addWater(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");
    void addTerrain(SceneGraphNode* parentNode, const boost::property_tree::ptree& pt, const Str64& nodeName = "");

    /// Object picking
    Selections getCurrentSelection(const PlayerIndex index = 0) const {
        const auto it = _currentSelection.find(index);
        if (it != cend(_currentSelection)) {
            return it->second;
        }

        return {};
    }

    bool findSelection(PlayerIndex idx, bool clearOld);
    void beginDragSelection(PlayerIndex idx, const vec2<I32>& mousePos);
    void endDragSelection(PlayerIndex idx, bool clearSelection);

    SceneGraphNode* addParticleEmitter(const Str64& name,
                                       std::shared_ptr<ParticleData> data,
                                       SceneGraphNode* parentNode) const;

    AI::AIManager& aiManager() noexcept { return *_aiManager; }
    const AI::AIManager& aiManager() const noexcept { return *_aiManager; }

    ResourceCache* resourceCache() noexcept { return _resCache; }
    const ResourceCache* resourceCache() const noexcept { return _resCache; }

    SceneShaderData* shaderData() const noexcept;

    Camera* playerCamera() const;
    Camera* playerCamera(U8 index) const;

    LightPool& lightPool() noexcept { return *_lightPool; }
    const LightPool& lightPool() const noexcept { return *_lightPool; }

    // can save at any time, I guess?
    virtual bool saveXML(const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback) const;

    bool saveNodeToXML(const SceneGraphNode* node) const;
    bool loadNodeFromXML(SceneGraphNode* node) const;

    void initDayNightCycle(Sky& skyInstance, DirectionalLightComponent& sunLight) noexcept;

    // negative values should work
    void setDayNightCycleTimeFactor(F32 factor) noexcept;
    F32 getDayNightCycleTimeFactor() const noexcept;

    void setTimeOfDay(const SimpleTime& time) noexcept;
    const SimpleTime& getTimeOfDay() const noexcept;

    void setGeographicLocation(const SimpleLocation& location) noexcept;
    const SimpleLocation& getGeographicLocation() const noexcept;

    SunDetails getCurrentSunDetails() const noexcept;
    Sky::Atmosphere getCurrentAtmosphere() const noexcept;
    void setCurrentAtmosphere(const Sky::Atmosphere& atmosphere) const noexcept;

    PROPERTY_RW(bool, dayNightCycleEnabled, true);
    PROPERTY_R_IW(DayNightData, dayNightData);

   protected:
    virtual void rebuildShaders();
    virtual void onSetActive();
    virtual void onRemoveActive();
    // returns the first available action ID
    virtual U16 registerInputActions();
    virtual void loadKeyBindings();

    void onNodeDestroy(SceneGraphNode* node);
    void findHoverTarget(PlayerIndex idx, const vec2<I32>& aimPos);
    void clearHoverTarget(PlayerIndex idx);

    bool checkCameraUnderwater(PlayerIndex idx) const;
    bool checkCameraUnderwater(const Camera& camera) const;
    void toggleFlashlight(PlayerIndex idx);

    SceneNode_ptr createNode(SceneNodeType type, const ResourceDescriptor& descriptor) const;

    virtual bool save(ByteBuffer& outputBuffer) const;
    virtual bool load(ByteBuffer& inputBuffer);

    virtual bool frameStarted();
    virtual bool frameEnded();

    virtual void loadDefaultCamera();

    virtual bool loadXML(const Str256& name);

    virtual bool load(const Str256& name);
    void loadAsset(const Task* parentTask, const XML::SceneNode& sceneNode, SceneGraphNode* parent);
    virtual bool unload();
    virtual void postLoad();
    // gets called on the main thread when the scene finishes loading (e.g. used by the GUI system)
    // We may be rendering in a different viewport (splash screen, loading screen, etc) but we need our GUI stuff
    // to be ready for our game viewport, so we pass it here to make sure we are using the proper one
    virtual void postLoadMainThread(const Rect<U16>& targetRenderViewport);
    /// Check if Scene::load() was called
    bool checkLoadFlag() const noexcept { return _loadComplete; }
    /// Unload scenegraph
    void clearObjects();
    /**End loading and unloading logic*/
    /// returns true if the camera was moved/rotated/etc
    bool updateCameraControls(PlayerIndex idx) const;
    /// Draw debug entities
    virtual void debugDraw(const Camera* activeCamera, RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut);
    /// Draw custom ui elements
    virtual void drawCustomUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut);

    //Return true if input was consumed
    virtual bool mouseMoved(const Input::MouseMoveEvent& arg);
    
    U8 getSceneIndexForPlayer(PlayerIndex idx) const;
    Player* getPlayerForIndex(PlayerIndex idx) const;

    U8 getPlayerIndexForDevice(U8 deviceIndex) const;

    void addPlayerInternal(bool queue);
    void removePlayerInternal(PlayerIndex idx);
    void onPlayerAdd(const Player_ptr& player);
    void onPlayerRemove(const Player_ptr& player);

    /// simple function to load the scene elements.
    bool SCENE_LOAD(const Str256& name) {
        if (!Scene::load(name)) {
            Console::errorfn(Locale::Get(_ID("ERROR_SCENE_LOAD")), "scene load function");
            return false;
        }

        registerInputActions();
        loadKeyBindings();

        return true;
    }

    void currentPlayerPass(PlayerIndex idx);

    void resetSelection(PlayerIndex idx);
    void setSelected(PlayerIndex idx, const vectorEASTL<SceneGraphNode*>& SGNs, bool recursive);

    bool lockCameraToPlayerMouse(PlayerIndex index, bool lockState) const;

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Scene"; }

    void updateSelectionData(PlayerIndex idx, DragSelectData& data, bool remapped);

   protected:
       /// Global info
       SceneManager& _parent;
       ResourceCache* _resCache = nullptr;

       eastl::unique_ptr<SceneGraph>    _sceneGraph;
       eastl::unique_ptr<AI::AIManager> _aiManager;
       eastl::unique_ptr<SceneGUIElements> _GUI;

       vectorEASTL<Player*> _scenePlayers;
       U64 _sceneTimerUS = 0ULL;
       vectorEASTL<D64> _taskTimers;
       vectorEASTL<D64> _guiTimersMS;
       /// Datablocks for models,vegetation,terrains,tasks etc
       std::atomic_uint _loadingTasks;
       XML::SceneNode _xmlSceneGraphRootNode;

       F32 _LRSpeedFactor = 1.0f;
       /// Current selection
       hashMap<PlayerIndex, Selections> _currentSelection;
       hashMap<PlayerIndex, I64> _currentHoverTarget;
       hashMap<PlayerIndex, DragSelectData> _dragSelectData;

       hashMap<PlayerIndex, SceneGraphNode*> _flashLight;
       hashMap<PlayerIndex, U32> _cameraUpdateListeners;
       /// Scene::load must be called by every scene. Add a load flag to make sure!
       bool _loadComplete = false;

   private:
       SharedMutex _tasksMutex;
       vectorEASTL<Task*> _tasks;
       /// Contains all game related info for the scene (wind speed, visibility ranges, etc)
       eastl::unique_ptr<SceneState> _sceneState;
       vectorEASTL<SGNRayResult> _sceneSelectionCandidates;

   protected:
       eastl::unique_ptr<LightPool> _lightPool;
       eastl::unique_ptr<SceneInput> _input;
       eastl::unique_ptr<SceneEnvironmentProbePool> _envProbePool;

       IMPrimitive* _linesPrimitive = nullptr;
       vectorEASTL<IMPrimitive*> _octreePrimitives;
       vectorEASTL<BoundingBox> _octreeBoundingBoxes;

       mutable Mutex _perFrameArenaMutex;
       mutable MyArena<Config::REQUIRED_RAM_SIZE_IN_BYTES / 3> _perFrameArena;
};

namespace Attorney {
class SceneManager {
   private:
    static bool updateCameraControls(Scene& scene, const PlayerIndex idx) {
        return scene.updateCameraControls(idx);
    }

    static bool checkLoadFlag(const Scene& scene) noexcept {
        return scene.checkLoadFlag();
    }

    static void onPlayerAdd(Scene& scene, const Player_ptr& player) {
        scene.onPlayerAdd(player);
    }

    static void onPlayerRemove(Scene& scene, const Player_ptr& player) {
        scene.onPlayerRemove(player);
    }

    static void currentPlayerPass(Scene& scene, const PlayerIndex idx) {
        scene.currentPlayerPass(idx);
    }

    /// Draw debug entities
    static void debugDraw(Scene& scene, const Camera* activeCamera, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut) {
        scene.debugDraw(activeCamera, stagePass, bufferInOut);
    }

    static void drawCustomUI(Scene& scene, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) {
        scene.drawCustomUI(targetViewport, bufferInOut);
    }

    static bool frameStarted(Scene& scene) { return scene.frameStarted(); }
    static bool frameEnded(Scene& scene) { return scene.frameEnded(); }

    static bool loadXML(Scene& scene, const Str256& name) {
        return scene.loadXML(name);
    }
    static bool load(Scene& scene, const Str256& name) {
        return scene.load(name);
    }

    static bool unload(Scene& scene) { 
        return scene.unload();
    }

    static void postLoad(Scene& scene) {
        scene.postLoad();
    }

    static void postLoadMainThread(Scene& scene, const Rect<U16>& targetRenderViewport) {
        scene.postLoadMainThread(targetRenderViewport);
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

    static SceneGUIElements* gui(Scene& scene) noexcept {
        return scene._GUI.get();
    }

    static void resetSelection(Scene& scene, const PlayerIndex idx) {
        scene.resetSelection(idx);
    }

    static void setSelected(Scene& scene, const PlayerIndex idx, const vectorEASTL<SceneGraphNode*>& sgns, const bool recursive) {
        scene.setSelected(idx, sgns, recursive);
    }

    static void clearHoverTarget(Scene& scene, const Input::MouseMoveEvent& arg) {
        scene.clearHoverTarget(scene.input()->getPlayerIndexForDevice(arg._deviceIndex));
    }

    static SceneNode_ptr createNode(Scene& scene, const SceneNodeType type, const ResourceDescriptor& descriptor) {
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
    static void registerProbe(Scene& scene, EnvironmentProbeComponent* probe) noexcept {
        DIVIDE_ASSERT(scene._envProbePool != nullptr);

        scene._envProbePool->registerProbe(probe);
    }

    static void unregisterProbe(Scene& scene, EnvironmentProbeComponent* probe) noexcept {
        DIVIDE_ASSERT(scene._envProbePool != nullptr);

        scene._envProbePool->unregisterProbe(probe);
    }
    friend class Divide::EnvironmentProbeComponent;
};

class SceneLoadSave {
    static bool save(const Scene& scene, ByteBuffer& outputBuffer) {
        return scene.save(outputBuffer);
    }

    static bool load(Scene& scene, ByteBuffer& inputBuffer) {
        return scene.load(inputBuffer);
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

    friend class Divide::SceneGraph;
};

class SceneGUI {
    static SceneGUIElements* guiElements(Scene& scene) noexcept {
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
}  // namespace Divide

#endif