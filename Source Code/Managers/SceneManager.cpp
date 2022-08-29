#include "stdafx.h"

#include "Headers/SceneManager.h"
#include "Headers/FrameListenerManager.h"
#include "Headers/RenderPassManager.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include "Editor/Headers/Editor.h"

#include "GUI/Headers/GUI.h"
#include "GUI/Headers/GUIButton.h"

#include "AI/PathFinding/Headers/DivideRecast.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Scenes/Headers/ScenePool.h"
#include "Scenes/Headers/SceneShaderData.h"
#include "Graphs/Headers/SceneGraph.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/File/Headers/FileManagement.h"

#include "Environment/Vegetation/Headers/Vegetation.h"
#include "Environment/Sky/Headers/Sky.h"
#include "Environment/Water/Headers/Water.h"

#include "Dynamics/Entities/Units/Headers/Player.h"
#include "Geometry/Importer/Headers/DVDConverter.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/SelectionComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"

namespace Divide {

constexpr U16 BYTE_BUFFER_VERSION = 1u;

bool SceneManager::OnStartup(PlatformContext& context) {
    return Attorney::SceneManager::onStartup(context);
}

bool SceneManager::OnShutdown(PlatformContext& context) {
    return Attorney::SceneManager::onShutdown(context);
}

SceneManager::SceneManager(Kernel& parentKernel)
    : FrameListener("SceneManager", parentKernel.frameListenerMgr(), 2),
      InputAggregatorInterface(),
      KernelComponent(parentKernel)
{
}

SceneManager::~SceneManager()
{
    destroy();
}

Scene& SceneManager::getActiveScene() noexcept {
    return _scenePool->activeScene();
}

const Scene& SceneManager::getActiveScene() const noexcept {
    return _scenePool->activeScene();
}

void SceneManager::idle() {
    if (_sceneSwitchTarget._isSet) {
        parent().platformContext().gfx().getRenderer().postFX().setFadeOut(UColour3(0), 1000.0, 0.0);
        if (!switchSceneInternal()) {
            DIVIDE_UNEXPECTED_CALL();
        }
        WaitForAllTasks(getActiveScene().context(), true);
        parent().platformContext().gfx().getRenderer().postFX().setFadeIn(2750.0);
    } else {
        if (_playerQueueDirty) {
            while (!_playerAddQueue.empty()) {
                auto& [targetScene, playerSGN] = _playerAddQueue.front();
                addPlayerInternal(*targetScene, playerSGN);
                _playerAddQueue.pop();
            }
            while (!_playerRemoveQueue.empty()) {
                auto& [targetScene, playerSGN] = _playerRemoveQueue.front();
                removePlayerInternal(*targetScene, playerSGN);
                _playerRemoveQueue.pop();
            }
            _playerQueueDirty = false;
        }

        if (getActiveScene().idle()) {
            NOP();
        }
    }
}

bool SceneManager::init(PlatformContext& platformContext, ResourceCache* cache) {
    if (_platformContext == nullptr) {
        _platformContext = &platformContext;
        _resourceCache = cache;
        platformContext.kernel().frameListenerMgr().registerFrameListener(this, 1);

        _recast = eastl::make_unique<AI::Navigation::DivideRecast>();

        _scenePool = MemoryManager_NEW ScenePool(*this);

        _sceneGraphCullTimers[to_U32(RenderStage::DISPLAY)] = &Time::ADD_TIMER(Util::StringFormat("SceneGraph cull timer: Display").c_str());
        _sceneGraphCullTimers[to_U32(RenderStage::REFLECTION)] = &Time::ADD_TIMER(Util::StringFormat("SceneGraph cull timer: Reflection").c_str());
        _sceneGraphCullTimers[to_U32(RenderStage::REFRACTION)] = &Time::ADD_TIMER(Util::StringFormat("SceneGraph cull timer: Refraction").c_str());
        _sceneGraphCullTimers[to_U32(RenderStage::SHADOW)] = &Time::ADD_TIMER(Util::StringFormat("SceneGraph cull timer: Shadow").c_str());

        _renderPassCuller = MemoryManager_NEW RenderPassCuller();
        _init = true;
    } else {
        _init = false;
    }
    return _init;
}

void SceneManager::destroy() {
    if (_init) {
        Vegetation::destroyStaticData();
        Console::printfn(Locale::Get(_ID("STOP_SCENE_MANAGER")));
        // Console::printfn(Locale::Get("SCENE_MANAGER_DELETE"));
        Console::printfn(Locale::Get(_ID("SCENE_MANAGER_REMOVE_SCENES")));
        MemoryManager::DELETE(_scenePool);
        MemoryManager::DELETE(_renderPassCuller);
        _recast.reset();
        _platformContext = nullptr;
        _init = false;
    }
}

Scene* SceneManager::load(const Str256& sceneName) {
    bool foundInCache = false;
    Scene* loadingScene = _scenePool->getOrCreateScene(*_platformContext, parent().resourceCache(), *this, sceneName, foundInCache);

    if (!loadingScene) {
        Console::errorfn(Locale::Get(_ID("ERROR_XML_LOAD_INVALID_SCENE")));
        return nullptr;
    }

    _platformContext->paramHandler().setParam(_ID("currentScene"), string(sceneName.c_str()));

    if (loadingScene->getState() != ResourceState::RES_LOADED &&
        !Attorney::SceneManager::load(*loadingScene)) 
    {
        return nullptr;
    }

    return loadingScene;
}

bool SceneManager::unloadScene(Scene* scene) {
    assert(scene != nullptr);
    if (_saveTask != nullptr) {
        Wait(*_saveTask, parent().platformContext().taskPool(TaskPoolType::LOW_PRIORITY));
    }

    _platformContext->gui().onUnloadScene(scene);
    Attorney::SceneManager::onRemoveActive(*scene);
    return Attorney::SceneManager::unload(*scene);
}

void SceneManager::setActiveScene(Scene* const scene) {
    assert(scene != nullptr);
    if (_saveTask != nullptr) {
        Wait(*_saveTask, parent().platformContext().taskPool(TaskPoolType::LOW_PRIORITY));
    }

    Attorney::SceneManager::onRemoveActive(_scenePool->defaultSceneActive() ? _scenePool->defaultScene()
                                                                            : getActiveScene());

    _scenePool->activeScene(*scene);
    Attorney::SceneManager::onSetActive(*scene);
    if (!LoadSave::loadScene(*scene)) {
        //corrupt save
    }

    _platformContext->gui().onChangeScene(scene);
    _platformContext->editor().onChangeScene(scene);
}

bool SceneManager::switchScene(const Str256& name, bool unloadPrevious, bool deferToIdle, const bool threaded) {
    const ResourcePath scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation + name;
    const ResourcePath sceneFile = scenePath + ".xml";
    if (!pathExists(scenePath) || !fileExists(sceneFile)) {
        return false;
    }

    _sceneSwitchTarget = { name, unloadPrevious, threaded, true };

    if (!deferToIdle) {
        return switchSceneInternal();
    }

    return true;
}

bool SceneManager::switchSceneInternal() {
    assert(_sceneSwitchTarget._isSet);

    const Str256 name = _sceneSwitchTarget._targetSceneName;
    const bool unloadPrevious = _sceneSwitchTarget._unloadPreviousScene;
    const bool threaded = _sceneSwitchTarget._loadInSeparateThread;
    _sceneSwitchTarget = {};

    assert(!name.empty());

    Scene* sceneToUnload = &_scenePool->activeScene();

    // We use our rendering task pool for scene changes because we might be creating / loading GPU assets (shaders, textures, buffers, etc)
    Start(*CreateTask(
        [this, unloadPrevious, &name, &sceneToUnload](const Task& /*parentTask*/)
        {
            // Load first, unload after to make sure we don't reload common resources
            if (load(name) != nullptr) {
                if (unloadPrevious && sceneToUnload) {
                    Attorney::SceneManager::onRemoveActive(*sceneToUnload);
                    unloadScene(sceneToUnload);
                }
            }
        }),
        _platformContext->taskPool(TaskPoolType::HIGH_PRIORITY),
        threaded ? TaskPriority::DONT_CARE : TaskPriority::REALTIME, 
        [this, name, unloadPrevious, &sceneToUnload]()
        {
            bool foundInCache = false;
            Scene* loadedScene = _scenePool->getOrCreateScene(*_platformContext, parent().resourceCache(), *this, name, foundInCache);
            assert(loadedScene != nullptr && foundInCache);

            if(loadedScene->getState() == ResourceState::RES_LOADING) {
                Attorney::SceneManager::postLoadMainThread(*loadedScene);
            }
            assert(loadedScene->getState() == ResourceState::RES_LOADED);
            setActiveScene(loadedScene);

            if (unloadPrevious) {
                _scenePool->deleteScene(sceneToUnload != nullptr ? sceneToUnload->getGUID() : -1);
            }

            _renderPassCuller->clear();
            _parent.platformContext().app().timer().resetFPSCounter();
            
        });

    return true;
}

vector<Str256> SceneManager::sceneNameList(const bool sorted) const {
    return _scenePool->sceneNameList(sorted);
}

void SceneManager::initPostLoadState() noexcept {
    _processInput = true;
    if_constexpr(Config::Build::IS_EDITOR_BUILD) {
        static_assert(Config::Build::ENABLE_EDITOR);
        DisplayWindow& window = _platformContext->mainWindow();
        if (window.type() == WindowType::WINDOW) {
            window.maximized(true);
        }

        _platformContext->editor().toggle(true);
    }
}

void SceneManager::onResolutionChange(const SizeChangeParams& params) {
    if (_init) {
        const F32 aspectRatio = to_F32(params.width) / params.height;
        const F32 vFoV = Angle::to_VerticalFoV(_platformContext->config().runtime.horizontalFOV, to_D64(aspectRatio));
        const vec2<F32> zPlanes(Camera::s_minNearZ, _platformContext->config().runtime.cameraViewDistance);

        for (const UnitComponent* player : _players) {
            if (player != nullptr) {
                player->getUnit<Player>()->camera()->setProjection(aspectRatio, vFoV, zPlanes);
            }
        }
    }
}

void SceneManager::addPlayer(Scene& parentScene, SceneGraphNode* playerNode, const bool queue) {
    if (queue) {
        _playerAddQueue.push(std::make_pair(&parentScene, playerNode));
        _playerQueueDirty = true;
    } else {
        addPlayerInternal(parentScene, playerNode);
    }
}

void SceneManager::addPlayerInternal(Scene& parentScene, SceneGraphNode* playerNode) {
    const I64 sgnGUID = playerNode->getGUID();
    for (const UnitComponent* crtPlayer : _players) {
        if (crtPlayer && crtPlayer->parentSGN()->getGUID() == sgnGUID) {
            return;
        }
    }

    U32 i = 0u;
    for (; i < Config::MAX_LOCAL_PLAYER_COUNT; ++i) {
        if (_players[i] == nullptr) {
            break;
        }
    }

    if (i < Config::MAX_LOCAL_PLAYER_COUNT) {
        const Player_ptr player = std::make_shared<Player>(to_U8(i));
        player->camera()->fromCamera(*Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT));
        player->camera()->setFixedYawAxis(true);

        {
            boost::property_tree::ptree pt;
            const ResourcePath scenePath = Paths::g_xmlDataLocation + Paths::g_scenesLocation;
            const ResourcePath sceneLocation(scenePath + "/" + parentScene.resourceName().c_str());
            const ResourcePath sceneDataFile(sceneLocation + ".xml");
            XML::readXML(sceneDataFile.c_str(), pt);
            player->camera()->loadFromXML(pt);
        }

        _players[i] = playerNode->get<UnitComponent>();
        _players[i]->setUnit(player);

        ++_activePlayerCount;

        Attorney::SceneManager::onPlayerAdd(parentScene, player);
    }
}

void SceneManager::removePlayer(Scene& parentScene, SceneGraphNode* playerNode, const bool queue) {
    if (queue) {
        _playerRemoveQueue.push(std::make_pair(&parentScene, playerNode));
        _playerQueueDirty = true;
    } else {
        removePlayerInternal(parentScene, playerNode);
    }
}

void SceneManager::removePlayerInternal(Scene& parentScene, SceneGraphNode* playerNode) {
    if (playerNode == nullptr) {
        return;
    }

    const I64 targetGUID = playerNode->getGUID();
    for (U32 i = 0; i < Config::MAX_LOCAL_PLAYER_COUNT; ++i) {
        if (_players[i] != nullptr && _players[i]->parentSGN()->getGUID() == targetGUID) {
            --_activePlayerCount;
            Attorney::SceneManager::onPlayerRemove(parentScene, _players[i]->getUnit<Player>());
            _players[i] = nullptr;
            break;
        }
    }
}

vector<SceneGraphNode*> SceneManager::getNodesInScreenRect(const Rect<I32>& screenRect, const Camera& camera, const Rect<I32>& viewport) const {
    OPTICK_EVENT();
    constexpr std::array<SceneNodeType, 6> s_ignoredNodes = {
        SceneNodeType::TYPE_TRANSFORM,
        SceneNodeType::TYPE_WATER,
        SceneNodeType::TYPE_SKY,
        SceneNodeType::TYPE_PARTICLE_EMITTER,
        SceneNodeType::TYPE_INFINITEPLANE,
        SceneNodeType::TYPE_VEGETATION
    };
    static vector<SGNRayResult> rayResults = {};

    const auto& sceneGraph = getActiveScene().sceneGraph();
    const vec3<F32>& eye = camera.getEye();
    const vec2<F32>& zPlanes = camera.getZPlanes();

    SGNIntersectionParams intersectionParams = {};
    intersectionParams._includeTransformNodes = false;
    intersectionParams._ignoredTypes = s_ignoredNodes.data();
    intersectionParams._ignoredTypesCount = s_ignoredNodes.size();

    const auto CheckPointLoS = [&](const vec3<F32>& point, const I64 nodeGUID, const I64 parentNodeGUID) -> bool {
        intersectionParams._ray = { point, point.direction(eye) };
        intersectionParams._range = { 0.f, zPlanes.y };

        const F32 distanceToPoint = eye.distance(point);

        sceneGraph->intersect(intersectionParams, rayResults);

        for (const SGNRayResult& result : rayResults) {
            if (result.sgnGUID == nodeGUID ||
                result.sgnGUID == parentNodeGUID)
            {
                continue;
            }

            if (result.inside || result.dist < distanceToPoint) {
                return false;
            }
        }
        return true;
    };

    const auto HasLoSToCamera = [&](SceneGraphNode* node, const vec3<F32>& point) {
        I64 parentNodeGUID = -1;
        const I64 nodeGUID = node->getGUID();
        if (node->getNode().type() == SceneNodeType::TYPE_OBJECT3D) {
            parentNodeGUID = node->parent()->getGUID();
        }
        return CheckPointLoS(point, nodeGUID, parentNodeGUID);
    };

    const auto IsNodeInRect = [&screenRect, &camera, &viewport](SceneGraphNode* node) {
        assert(node != nullptr);
        const SceneNode& sNode = node->getNode();
        if (sNode.type() == SceneNodeType::TYPE_OBJECT3D) {
            auto* sComp = node->get<SelectionComponent>();
            if (sComp == nullptr && 
                (sNode.type() == SceneNodeType::TYPE_OBJECT3D && node->getNode<Object3D>().geometryType() == ObjectType::SUBMESH))
            {
                if (node->parent() != nullptr) {
                    // Already selected. Skip.
                    if (node->parent()->hasFlag(SceneGraphNode::Flags::SELECTED)) {
                        return false;
                    }
                    sComp = node->parent()->get<SelectionComponent>();
                }
            }
            if (sComp != nullptr && sComp->enabled()) {
                const BoundsComponent* bComp = node->get<BoundsComponent>();
                if (bComp != nullptr) {
                    const vec3<F32>& center = bComp->getBoundingSphere().getCenter();
                    return screenRect.contains(camera.project(center, viewport));
                }
            }
        }

        return false;
    };

    //Step 1: Grab ALL nodes in rect
    vector<SceneGraphNode*> ret = {};

    VisibleNodeList<VisibleNode, 1024> inRectList;
    const VisibleNodeList<>& visNodes = getNodeCache(RenderStage::DISPLAY);
    for (size_t i = 0u; i < visNodes.size(); ++i) {
        const VisibleNode& node = visNodes.node(i);
        if (IsNodeInRect(node._node)) {
            inRectList.append(node);
        }
    }

    //Step 2: Check Straight LoS to camera
    VisibleNodeList<VisibleNode, 1024> LoSList;
    for (size_t i = 0u; i < inRectList.size(); ++i) {
        const VisibleNode& node = inRectList.node(i);
        if (HasLoSToCamera(node._node, node._node->get<BoundsComponent>()->getBoundingSphere().getCenter())) {
            LoSList.append(node);
        } else {
            // This is gonna hurt.The raycast failed, but the node might still be visible
            const OBB& obb = node._node->get<BoundsComponent>()->getOBB();
            for (U8 p = 0; p < 8; ++p) {
                if (HasLoSToCamera(node._node, obb.cornerPoint(p))) {
                    LoSList.append(node);
                    break;
                }
            }
        }
    }

    //Step 3: Create list of visible nodes
    for (size_t i = 0; i < LoSList.size(); ++i) {
        SceneGraphNode* parsedNode = LoSList.node(i)._node;
        if (parsedNode != nullptr) {
            while (true) {
                const SceneNode& node = parsedNode->getNode();
                if (node.type() == SceneNodeType::TYPE_OBJECT3D && static_cast<const Object3D&>(node).geometryType() == ObjectType::SUBMESH) {
                    parsedNode = parsedNode->parent();
                } else {
                    break;
                }
            }

            if (eastl::find(cbegin(ret), cend(ret), parsedNode) == cend(ret)) {
                ret.push_back(parsedNode);
            }
        }
    }

    return ret;
}

bool SceneManager::frameStarted(const FrameEvent& evt) {
    return Attorney::SceneManager::frameStarted(getActiveScene());
}

bool SceneManager::frameEnded(const FrameEvent& evt) {
    return Attorney::SceneManager::frameEnded(getActiveScene());
}

void SceneManager::updateSceneState(const U64 deltaGameTimeUS, const U64 deltaAppTimeUS) {
    OPTICK_EVENT();

    Scene& activeScene = getActiveScene();
    assert(activeScene.getState() == ResourceState::RES_LOADED);
    // Update internal timers
    _elapsedGameTime += deltaGameTimeUS;
    _elapsedGameTimeMS = Time::MicrosecondsToMilliseconds<U32>(_elapsedGameTime);
    _elapsedAppTime += deltaAppTimeUS;
    _elapsedAppTimeMS = Time::MicrosecondsToMilliseconds<U32>(_elapsedAppTime);

    const Scene::DayNightData& dayNightData = activeScene.dayNightData();

    const FColour3 sunColour = dayNightData._sunLight != nullptr 
                                            ? dayNightData._sunLight->getDiffuseColour()
                                            : DefaultColours::WHITE.rgb;

    const GFXDevice& gfx = parent().platformContext().gfx();
    SceneShaderData* sceneData = gfx.sceneData();
    sceneData->sunDetails(activeScene.getSunPosition(), sunColour);
    sceneData->appData(_elapsedAppTimeMS, gfx.materialDebugFlag());

    //_sceneData->skyColour(horizonColour, zenithColour);

    FogDetails fog = activeScene.state()->renderState().fogDetails();
    fog._colourSunScatter.rgb = sunColour;

    if (!_platformContext->config().rendering.enableFog) {
        fog._colourAndDensity.a = 0.f;
    }
    sceneData->fogDetails(fog);

    const auto& activeSceneState = activeScene.state();
    sceneData->windDetails(activeSceneState->windDirX(),
                           0.0f,
                           activeSceneState->windDirZ(),
                           activeSceneState->windSpeed());

    Attorney::GFXDeviceSceneManager::shadowingSettings(_parent.platformContext().gfx(), activeSceneState->lightBleedBias(), activeSceneState->minShadowVariance());

    activeScene.updateSceneState(deltaGameTimeUS);

    U8 index = 0u;

    const auto& waterBodies = activeSceneState->waterBodies()._data;
    for (const auto& body : waterBodies) {
        sceneData->waterDetails(index++, body);
    }
    _saveTimer += deltaGameTimeUS;

    if (_saveTimer >= Time::SecondsToMicroseconds(Config::Build::IS_DEBUG_BUILD ? 5 : 10)) {
        if (!saveActiveScene(true, true)) {
            NOP();
        }
        _saveTimer = 0ULL;
    }
    if (dayNightData._skyInstance != nullptr) {
        _parent.platformContext().gfx().getRenderer().postFX().isDayTime(dayNightData._skyInstance->isDay());
    }
}

void SceneManager::drawCustomUI(const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) {
    //Set a 2D camera for rendering
    EnqueueCommand(bufferInOut, GFX::SetCameraCommand{ Camera::GetUtilityCamera(Camera::UtilityCamera::_2D)->snapshot() });
    EnqueueCommand(bufferInOut, GFX::SetViewportCommand{ targetViewport });

    Attorney::SceneManager::drawCustomUI(getActiveScene(), targetViewport, bufferInOut);
}

void SceneManager::debugDraw(GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    Scene& activeScene = getActiveScene();

    Attorney::SceneManager::debugDraw(activeScene, bufferInOut);
    // Draw bounding boxes, skeletons, axis gizmo, etc.
    _platformContext->gfx().debugDraw(activeScene.state()->renderState(), bufferInOut);
}

Camera* SceneManager::playerCamera(const PlayerIndex idx, const bool skipOverride) const noexcept {
    if (getActivePlayerCount() <= idx) {
        return nullptr;
    }

    if (!skipOverride) {
        Camera* overrideCamera = getActiveScene().state()->playerState(idx).overrideCamera();
        if (overrideCamera != nullptr) {
            return overrideCamera;
        }
    }

    return _players[idx]->getUnit<Player>()->camera();
}

Camera* SceneManager::playerCamera(const bool skipOverride) const noexcept {
    return playerCamera(_currentPlayerPass, skipOverride);
}

void SceneManager::currentPlayerPass(const U64 deltaTimeUS, const PlayerIndex idx) {
    OPTICK_EVENT();

    _currentPlayerPass = idx;
    Attorney::SceneManager::currentPlayerPass(getActiveScene(), deltaTimeUS, _currentPlayerPass);
    playerCamera()->updateLookAt();
}

void SceneManager::moveCameraToNode(const SceneGraphNode* targetNode) const {
    OPTICK_EVENT();

    vec3<F32> targetPos = VECTOR3_ZERO;

    /// Root node just means a teleport to (0,0,0)
    if (targetNode->parent() != nullptr) {
        targetPos = playerCamera()->getEye();
        const BoundsComponent* bComp = targetNode->get<BoundsComponent>();
        if (bComp != nullptr) {
            const BoundingSphere& bSphere = bComp->getBoundingSphere();
            targetPos = bSphere.getCenter();
            targetPos -= bSphere.getRadius() * 1.5f * playerCamera()->getForwardDir();
        } else {
            const TransformComponent* tComp = targetNode->get<TransformComponent>();
            if (tComp != nullptr) {
                targetPos = tComp->getWorldPosition();
                targetPos -= playerCamera()->getForwardDir() * 3.0f;
            }
        }
    }

    playerCamera()->setEye(targetPos);
}

bool SceneManager::saveNode(const SceneGraphNode* targetNode) const {
    return LoadSave::saveNodeToXML(getActiveScene(), targetNode);
}

bool SceneManager::loadNode(SceneGraphNode* targetNode) const {
    return LoadSave::loadNodeFromXML(getActiveScene(), targetNode);
}

void SceneManager::getSortedReflectiveNodes(const Camera* camera, const RenderStage stage, const bool inView, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    ScopedLock<Mutex> w_lock(s_searchNodesLock);

    static vector<SceneGraphNode*> allNodes = {};
    getActiveScene().sceneGraph()->getNodesByType({ SceneNodeType::TYPE_WATER, SceneNodeType::TYPE_OBJECT3D }, allNodes);

    erase_if(allNodes,
             [](SceneGraphNode* node) noexcept ->  bool {
                const Material_ptr& mat = node->get<RenderingComponent>()->getMaterialInstance();
                return node->getNode().type() != SceneNodeType::TYPE_WATER && (mat == nullptr || !mat->isReflective());
             });

    if (inView) {
        NodeCullParams cullParams = {};
        cullParams._lodThresholds = getActiveScene().state()->renderState().lodThresholds();
        cullParams._stage = stage;
        cullParams._cameraEyePos = camera->getEye();
        cullParams._frustum = &camera->getFrustum();
        cullParams._cullMaxDistance = camera->getZPlanes().y;

        _renderPassCuller->frustumCull(parent().platformContext(), cullParams, to_base(CullOptions::DEFAULT_CULL_OPTIONS), allNodes, nodesOut);
    } else {
        _renderPassCuller->toVisibleNodes(parent().platformContext(), camera, allNodes, nodesOut);
    }
}

void SceneManager::getSortedRefractiveNodes(const Camera* camera, const RenderStage stage, const bool inView, VisibleNodeList<>& nodesOut) const {
    OPTICK_EVENT();

    ScopedLock<Mutex> w_lock(s_searchNodesLock);

    static vector<SceneGraphNode*> allNodes = {};
    getActiveScene().sceneGraph()->getNodesByType({ SceneNodeType::TYPE_WATER, SceneNodeType::TYPE_OBJECT3D }, allNodes);

    erase_if(allNodes,
             [](SceneGraphNode* node) noexcept ->  bool {
                  const Material_ptr& mat = node->get<RenderingComponent>()->getMaterialInstance();
                  return node->getNode().type() != SceneNodeType::TYPE_WATER && (mat == nullptr || !mat->isRefractive());
             });
    if (inView) {
        NodeCullParams cullParams = {};
        cullParams._lodThresholds = getActiveScene().state()->renderState().lodThresholds();
        cullParams._stage = stage;
        cullParams._cameraEyePos = camera->getEye();
        cullParams._frustum = &camera->getFrustum();
        cullParams._cullMaxDistance = camera->getZPlanes().y;

        _renderPassCuller->frustumCull(parent().platformContext(), cullParams, to_base(CullOptions::DEFAULT_CULL_OPTIONS), allNodes, nodesOut);
    } else {
        _renderPassCuller->toVisibleNodes(parent().platformContext(), camera, allNodes, nodesOut);
    }
}

const VisibleNodeList<>& SceneManager::getNodeCache(const RenderStage stage) const noexcept {
    return _renderPassCuller->getNodeCache(stage);
}

void SceneManager::initDefaultCullValues(const RenderStage stage, NodeCullParams& cullParamsInOut) noexcept {
    const Scene& activeScene = getActiveScene();

    cullParamsInOut._stage = stage;
    cullParamsInOut._lodThresholds = activeScene.state()->renderState().lodThresholds(stage);
    if (stage != RenderStage::SHADOW) {
        cullParamsInOut._cullMaxDistance = activeScene.state()->renderState().generalVisibility();
    } else {
        cullParamsInOut._cullMaxDistance = std::numeric_limits<F32>::max();
    }
}

VisibleNodeList<>& SceneManager::cullSceneGraph(const NodeCullParams& params, const U16 cullFlags) {
    OPTICK_EVENT();

    Time::ScopedTimer timer(*_sceneGraphCullTimers[to_U32(params._stage)]);

    const Scene& activeScene = getActiveScene();
    return _renderPassCuller->frustumCull(params, cullFlags, *activeScene.sceneGraph(), *activeScene.state(), _parent.platformContext());
}

void SceneManager::prepareLightData(const RenderStage stage, const CameraSnapshot& cameraSnapshot, GFX::MemoryBarrierCommand& memCmdInOut) {
    OPTICK_EVENT();

    if (stage != RenderStage::SHADOW) {
        getActiveScene().lightPool()->sortLightData(stage, cameraSnapshot);
        getActiveScene().lightPool()->uploadLightData(stage, cameraSnapshot, memCmdInOut);
    }
}

void SceneManager::onChangeFocus(const bool hasFocus) {
    if (!_init) {
        return;
    }

    getActiveScene().onChangeFocus(hasFocus);
}

bool SceneManager::resetSelection(const PlayerIndex idx, const bool resetIfLocked) {
    OPTICK_EVENT();

    if (Attorney::SceneManager::resetSelection(getActiveScene(), idx, resetIfLocked)) {
        for (auto& cbk : _selectionChangeCallbacks) {
            cbk.second(idx, {});
        }
        return true;
    }

    return false;
}

void SceneManager::setSelected(const PlayerIndex idx, const vector<SceneGraphNode*>& SGNs, const bool recursive) {
    OPTICK_EVENT();

    Attorney::SceneManager::setSelected(getActiveScene(), idx, SGNs, recursive);
    for (auto& cbk : _selectionChangeCallbacks) {
        cbk.second(idx, SGNs);
    }
}

void SceneManager::onNodeDestroy([[maybe_unused]] SceneGraphNode* node) {
    for (PlayerIndex p = 0; p < _activePlayerCount; ++p) {
        if (!resetSelection(p, true)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
}

void SceneManager::mouseMovedExternally(const Input::MouseMoveEvent& arg) {
    Attorney::SceneManager::clearHoverTarget(getActiveScene(), arg);
}

SceneNode_ptr SceneManager::createNode(const SceneNodeType type, const ResourceDescriptor& descriptor) {
    return Attorney::SceneManager::createNode(getActiveScene(), type, descriptor);
}

SceneEnvironmentProbePool* SceneManager::getEnvProbes() const noexcept {
    return Attorney::SceneManager::getEnvProbes(getActiveScene());
}

std::pair<Texture_ptr, size_t> SceneManager::getSkyTexture() const {
    const auto& skies = getActiveScene().sceneGraph()->getNodesByType(SceneNodeType::TYPE_SKY);
    if (!skies.empty()) {
        const Sky& sky = skies.front()->getNode<Sky>();
        return std::make_pair(
            sky.activeSkyBox(),
            sky.skyboxSampler()
        );
    }

    return { nullptr, 0u };
}

///--------------------------Input Management-------------------------------------///

bool SceneManager::onKeyDown(const Input::KeyEvent& key) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->onKeyDown(key);
}

bool SceneManager::onKeyUp(const Input::KeyEvent& key) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->onKeyUp(key);
}

bool SceneManager::mouseMoved(const Input::MouseMoveEvent& arg) {
    if (!_processInput) {
        return false;

    }

    return getActiveScene().input()->mouseMoved(arg);
}

bool SceneManager::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->mouseButtonPressed(arg);
}

bool SceneManager::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->mouseButtonReleased(arg);
}

bool SceneManager::joystickAxisMoved(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickAxisMoved(arg);
}

bool SceneManager::joystickPovMoved(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickPovMoved(arg);
}

bool SceneManager::joystickButtonPressed(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickButtonPressed(arg);
}

bool SceneManager::joystickButtonReleased(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickButtonReleased(arg);
}

bool SceneManager::joystickBallMoved(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickBallMoved(arg);
}

bool SceneManager::joystickAddRemove(const Input::JoystickEvent& arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickAddRemove(arg);
}

bool SceneManager::joystickRemap(const Input::JoystickEvent &arg) {
    if (!_processInput) {
        return false;
    }

    return getActiveScene().input()->joystickRemap(arg);
}

bool SceneManager::onUTF8([[maybe_unused]] const Input::UTF8Event& arg) {
    return false;
}

bool LoadSave::loadScene(Scene& activeScene) {
    if (activeScene.state()->saveLoadDisabled()) {
        return true;
    }

    const Str256& sceneName = activeScene.resourceName();

    const ResourcePath path = Paths::g_saveLocation +  sceneName + "/";
    const ResourcePath saveFile = ResourcePath("current_save.sav");
    const ResourcePath bakSaveFile = ResourcePath("save.bak");

    bool isLoadFromBackup = false;
    // If file is missing, restore from bak
    if (!fileExists(path + saveFile)) {
        isLoadFromBackup = true;

        // Save file might be deleted if it was corrupted
        if (fileExists(path + bakSaveFile)) {
            if (copyFile(path, bakSaveFile, path, saveFile, false) != FileError::NONE) {
                NOP();
            }
        }
    }

    ByteBuffer save;
    if (save.loadFromFile(path.c_str(), saveFile.c_str())) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        save >> tempVer;
        if (tempVer == BYTE_BUFFER_VERSION) {
            if (!Attorney::SceneLoadSave::load(activeScene, save)) {
                //Remove the save and try the backup
                if (deleteFile(path, saveFile) != FileError::NONE) {
                    NOP();
                }
                if (!isLoadFromBackup) {
                    return loadScene(activeScene);
                }
            }
        }
    }
    return false;
}


bool LoadSave::saveNodeToXML(const Scene& activeScene, const SceneGraphNode* node) {
    return Attorney::SceneLoadSave::saveNodeToXML(activeScene, node);
}

bool LoadSave::loadNodeFromXML(const Scene& activeScene, SceneGraphNode* node) {
    return Attorney::SceneLoadSave::loadNodeFromXML(activeScene, node);
}

bool LoadSave::saveScene(const Scene& activeScene, const bool toCache, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride) {
    if (!toCache) {
        return Attorney::SceneLoadSave::saveXML(activeScene, msgCallback, finishCallback, sceneNameOverride);
    }

    bool ret = false;
    if (activeScene.state()->saveLoadDisabled()) {
        ret = true;
    } else {
        const Str256& sceneName = activeScene.resourceName();
        const ResourcePath path = Paths::g_saveLocation + sceneName + "/";
        const ResourcePath saveFile = ResourcePath("current_save.sav");
        const ResourcePath bakSaveFile = ResourcePath("save.bak");

        if (fileExists(path + saveFile)) {
            if (copyFile(path, saveFile, path, bakSaveFile, true) != FileError::NONE) {
                return false;
            }
        }

        ByteBuffer save;
        save << BYTE_BUFFER_VERSION;
        if (Attorney::SceneLoadSave::save(activeScene, save)) {
            ret = save.dumpToFile(path.c_str(), saveFile.c_str());
            assert(ret);
        }
    }
    if (finishCallback) {
        finishCallback(ret);
    }
    return ret;
}

bool SceneManager::saveActiveScene(bool toCache, const bool deferred, const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride) {
    OPTICK_EVENT();

    const Scene& activeScene = getActiveScene();

    // Ignore any auto-save (or manual saves) on the default scene
    if (Util::IsEmptyOrNull(sceneNameOverride) && activeScene.getGUID() == Scene::DEFAULT_SCENE_GUID) {
        return true;
    }

    TaskPool& pool = parent().platformContext().taskPool(TaskPoolType::LOW_PRIORITY);
    if (_saveTask != nullptr) {
        if (!Finished(*_saveTask)) {
            if (toCache) {
                return false;
            }
            DIVIDE_UNEXPECTED_CALL();
        }
        Wait(*_saveTask, pool);
    }

    _saveTask = CreateTask(nullptr,
                           [&activeScene, msgCallback, finishCallback, sceneNameStr = string(sceneNameOverride), toCache](const Task& /*parentTask*/) {
                               LoadSave::saveScene(activeScene, toCache, msgCallback, finishCallback, sceneNameStr.c_str());
                           },
                           false);
    Start(*_saveTask, pool, deferred ? TaskPriority::DONT_CARE : TaskPriority::REALTIME);

    return true;
}

bool SceneManager::networkUpdate([[maybe_unused]] const U64 frameCount) {
    return true;
}

};