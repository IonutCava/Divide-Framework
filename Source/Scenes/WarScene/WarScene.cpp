

#include "Headers/WarScene.h"
#include "Headers/WarSceneAIProcessor.h"

#include "GUI/Headers/GUIButton.h"
#include "GUI/Headers/GUIMessageBox.h"
#include "GUI/Headers/SceneGUIElements.h"

#include "AI/Headers/AIManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Dynamics/Entities/Units/Headers/Player.h"
#include "Managers/Headers/ProjectManager.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Rendering/Lighting/Headers/LightPool.h"

#include "Graphs/Headers/SceneGraph.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"

#include "Environment/Terrain/Headers/Terrain.h"

namespace Divide {

WarScene::WarScene(PlatformContext& context, Project& parent, const SceneEntry& entry)
   : Scene(context, parent, entry),
    _timeLimitMinutes(5),
    _scoreLimit(3)
{
    const size_t idx = parent.parent().addSelectionCallback([&](PlayerIndex /*idx*/, const vector<SceneGraphNode*>& node)
    {
        string selectionText;
        for (SceneGraphNode* it : node)
        {
            selectionText.append("\n");
            selectionText.append(it->name().c_str());
        }

        _GUI->modifyText("entityState", selectionText, true);
    });

    _selectionCallbackIndices.push_back(idx);
}

WarScene::~WarScene()
{
    if (_targetLines && !_context.gfx().destroyIMP(_targetLines) )
    {
        DebugBreak();
    }
}

void WarScene::toggleTerrainMode() {
    _terrainMode = !_terrainMode;
}

void WarScene::debugDraw(GFX::CommandBuffer& bufferInOut,
                         GFX::MemoryBarrierCommand& memCmdInOut )
{
    if (state()->renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES))
    {
        if (!_targetLines)
        {
            _targetLines = _context.gfx().newIMP("WarScene Target Lines");
            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._shaderProgramHandle = _context.gfx().imShaders()->imWorldShaderNoTexture();

            pipelineDescriptor._stateBlock = _context.gfx().getNoDepthTestBlock();
            _targetLines->setPipelineDescriptor(pipelineDescriptor);
        }
        else
        {
            _targetLines->getCommandBuffer(bufferInOut, memCmdInOut);
        }
    }
    else if (_targetLines)
    {
        if (!_context.gfx().destroyIMP(_targetLines))
        {
            DebugBreak();
        }
    }
    Scene::debugDraw(bufferInOut, memCmdInOut);
}

namespace {
    constexpr bool g_enableOldGameLogic = false;
    F32 phi = 0.0f;
    vec3<F32> initPos;
    bool initPosSet = false;
}

namespace{
    SceneGraphNode* g_terrain = nullptr;
}

void WarScene::updateSceneStateInternal(const U64 deltaTimeUS) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    if (!_sceneReady) {
        return;
    }

    if (_terrainMode) {
        if (g_terrain == nullptr) {
            auto objects = sceneGraph()->getNodesByType(SceneNodeType::TYPE_TERRAIN);
            if (!objects.empty())
            {
                g_terrain = objects.front();
            }
        } else {
            vec3<F32> camPos = playerCamera()->snapshot()._eye;
            if (g_terrain->get<BoundsComponent>()->getBoundingBox().containsPoint(camPos)) {
                const Terrain& ter = g_terrain->getNode<Terrain>();

                F32 headHeight = state()->playerState(state()->playerPass())._headHeight;
                camPos -= g_terrain->get<TransformComponent>()->getWorldPosition();
                playerCamera()->setEye(ter.getVertFromGlobal(camPos.x, camPos.z, true)._position + vec3<F32>(0.0f, headHeight, 0.0f));
            }
        }
    }

    if constexpr(g_enableOldGameLogic) {
        if (_resetUnits) {
            resetUnits();
            _resetUnits = false;
        }

        SceneGraphNode* particles = _particleEmitter;
        const F32 radius = 200;

        if (particles) {
            phi += 0.001f;
            if (phi > 360.0f) {
                phi = 0.0f;
            }

            TransformComponent* tComp = particles->get<TransformComponent>();
            if (!initPosSet) {
                initPos.set(tComp->getWorldPosition());
                initPosSet = true;
            }

            tComp->setPosition(radius * std::cos(phi) + initPos.x,
                              (radius * 0.5f) * std::sin(phi) + initPos.y,
                               radius * std::sin(phi) + initPos.z);
            tComp->rotateY(phi);
        }

        if (!_aiManager->getNavMesh(_armyNPCs[0][0]->get<UnitComponent>()->getUnit<NPC>()->getAIEntity()->getAgentRadiusCategory())) {
            return;
        }

        // renderState().drawDebugLines(true);
        vec3<F32> tempDestination;
        UColour4 redLine(255, 0, 0, 128);
        UColour4 blueLine(0, 0, 255, 128);
        vector<Line> paths;
        paths.reserve(_armyNPCs[0].size() + _armyNPCs[1].size());
        for (U8 i = 0; i < 2; ++i) {
            for (SceneGraphNode* node : _armyNPCs[i]) {
                AI::AIEntity* const character = node->get<UnitComponent>()->getUnit<NPC>()->getAIEntity();
                if (!node->IsActive()) {
                    continue;
                }
                tempDestination.set(character->getDestination());
                if (!tempDestination.isZeroLength()) {
                    paths.emplace_back(
                        character->getPosition(),
                        tempDestination,
                        i == 0 ? blueLine : redLine,
                        i == 0 ? blueLine / 2 : redLine / 2);
                }
            }
        }
        if (_targetLines) {
            _targetLines->fromLines(paths.data(), paths.size());
        }

        if (!_aiManager->updatePaused()) {
            _elapsedGameTime += deltaTimeUS;
            checkGameCompletion();
        }
    }
}

bool WarScene::load() {
    // Load scene resources
    const bool loadState = Scene::load();
    setDayNightCycleTimeFactor(24);

    // Position camera
    Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT)->setEye(vec3<F32>(43.13f, 147.09f, -4.41f));
    Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT)->setGlobalRotation(-90.0f /*yaw*/, 59.21f /*pitch*/);

    // Add some obstacles

    /*
    constexpr U32 lightMask = to_base(ComponentType::TRANSFORM) |
                              to_base(ComponentType::BOUNDS) |
                              to_base(ComponentType::RENDERING);

    constexpr U32 normalMask = lightMask |
                           to_base(ComponentType::RIGID_BODY) |
                           to_base(ComponentType::NAVIGATION) |
                           to_base(ComponentType::NETWORKING);

    SceneGraphNode* cylinder[5];
    cylinder[0] = _sceneGraph->findNode("cylinderC");
    cylinder[1] = _sceneGraph->findNode("cylinderNW");
    cylinder[2] = _sceneGraph->findNode("cylinderNE");
    cylinder[3] = _sceneGraph->findNode("cylinderSW");
    cylinder[4] = _sceneGraph->findNode("cylinderSE");

    for (U8 i = 0; i < 5; ++i) {
        RenderingComponent* const renderable = cylinder[i]->getChild(0).get<RenderingComponent>();
        renderable->getMaterialInstance()->setDoubleSided(true);
        cylinder[i]->getChild(0).getNode().getMaterialTpl()->setDoubleSided(true);
    }

    // Make the center cylinder reflective
    const Material_ptr& matInstance = cylinder[0]->getChild(0).get<RenderingComponent>()->getMaterialInstance();
    matInstance->shininess(Material::MAX_SHININESS);
    */
    string currentName;
#if 0
    SceneNode_ptr cylinderMeshNW = cylinder[1]->getNode();
    SceneNode_ptr cylinderMeshNE = cylinder[2]->getNode();
    SceneNode_ptr cylinderMeshSW = cylinder[3]->getNode();
    SceneNode_ptr cylinderMeshSE = cylinder[4]->getNode();

    SceneNode_ptr currentMesh;
    SceneGraphNode* baseNode;

    SceneGraphNodeDescriptor sceneryNodeDescriptor;
    sceneryNodeDescriptor._serialize = false;
    sceneryNodeDescriptor._componentMask = normalMask;

    U8 locationFlag = 0;
    vec2<I32> currentPos;
    for (U8 i = 0; i < 40; ++i) {
        if (i < 10) {
            baseNode = cylinder[1];
            currentMesh = cylinderMeshNW;
            currentName = "Cylinder_NW_" + Util::to_string((I32)i);
            currentPos.x = -200 + 40 * i + 50;
            currentPos.y = -200 + 40 * i + 50;
        } else if (i >= 10 && i < 20) {
            baseNode = cylinder[2];
            currentMesh = cylinderMeshNE;
            currentName = "Cylinder_NE_" + Util::to_string((I32)i);
            currentPos.x = 200 - 40 * (i % 10) - 50;
            currentPos.y = -200 + 40 * (i % 10) + 50;
            locationFlag = 1;
        } else if (i >= 20 && i < 30) {
            baseNode = cylinder[3];
            currentMesh = cylinderMeshSW;
            currentName = "Cylinder_SW_" + Util::to_string((I32)i);
            currentPos.x = -200 + 40 * (i % 20) + 50;
            currentPos.y = 200 - 40 * (i % 20) - 50;
            locationFlag = 2;
        } else {
            baseNode = cylinder[4];
            currentMesh = cylinderMeshSE;
            currentName = "Cylinder_SE_" + Util::to_string((I32)i);
            currentPos.x = 200 - 40 * (i % 30) - 50;
            currentPos.y = 200 - 40 * (i % 30) - 50;
            locationFlag = 3;
        }


        
        sceneryNodeDescriptor._node = currentMesh;
        sceneryNodeDescriptor._name = currentName;
        sceneryNodeDescriptor._usageContext = baseNode->usageContext();
        sceneryNodeDescriptor._physicsGroup = baseNode->get<RigidBodyComponent>()->physicsGroup();

        SceneGraphNode* crtNode = _sceneGraph->getRoot().addNode(sceneryNodeDescriptor);
        
        TransformComponent* tComp = crtNode->get<TransformComponent>();
        NavigationComponent* nComp = crtNode->get<NavigationComponent>();
        nComp->navigationContext(baseNode->get<NavigationComponent>()->navigationContext());
        nComp->navigationDetailOverride(baseNode->get<NavigationComponent>()->navMeshDetailOverride());

        vec3<F32> position(to_F32(currentPos.x), -0.01f, to_F32(currentPos.y));
        tComp->setScale(baseNode->get<TransformComponent>()->getScale());
        tComp->setPosition(position);
        {
            ResourceDescriptor<Light> tempLight(Util::StringFormat("Light_point_random_1", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::POINT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(25.0f);
            //light->setCastShadows(i == 0 ? true : false);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 8.0f, 0.0f));
        }
        {
            ResourceDescriptor<Light> tempLight(Util::StringFormat("Light_point_{}_2", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::POINT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(35.0f);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 8.0f, 0.0f));
        }
        {
            ResourceDescriptor<Light> tempLight(Util::StringFormat("Light_spot_{}", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::SPOT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(55.0f);
            //light->setCastShadows(i == 1 ? true : false);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 10.0f, 0.0f));
            lightSGN->get<TransformComponent>()->rotateX(-20);
        }
    }

    SceneGraphNode* flag;
    flag = _sceneGraph->findNode("flag");
    RenderingComponent* const renderable = flag->getChild(0).get<RenderingComponent>();
    renderable->getMaterialInstance()->setDoubleSided(true);
    const Material_ptr& mat = flag->getChild(0).getNode()->getMaterialTpl();
    mat->setDoubleSided(true);
    flag->setActive(false);
    SceneNode_ptr flagNode = flag->getNode();

    sceneryNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
    sceneryNodeDescriptor._node = flagNode;
    sceneryNodeDescriptor._physicsGroup = flag->get<RigidBodyComponent>()->physicsGroup();
    sceneryNodeDescriptor._name = "Team1Flag";

    _flag[0] = _sceneGraph->getRoot().addNode(sceneryNodeDescriptor);

    SceneGraphNode* flag0(_flag[0]);

    TransformComponent* flagtComp = flag0->get<TransformComponent>();
    NavigationComponent* flagNComp = flag0->get<NavigationComponent>();
    RenderingComponent* flagRComp = flag0->getChild(0).get<RenderingComponent>();

    flagtComp->setScale(flag->get<TransformComponent>()->getScale());
    flagtComp->setPosition(vec3<F32>(25.0f, 0.1f, -206.0f));

    flagNComp->navigationContext(NavigationComponent::NavigationContext::NODE_IGNORE);

    flagRComp->getMaterialInstance()->setDiffuse(DefaultColours::BLUE);

    sceneryNodeDescriptor._name = "Team2Flag";
    _flag[1] = _sceneGraph->getRoot().addNode(sceneryNodeDescriptor);
    SceneGraphNode* flag1(_flag[1]);
    flag1->usageContext(flag->usageContext());

    flagtComp = flag1->get<TransformComponent>();
    flagNComp = flag1->get<NavigationComponent>();
    flagRComp = flag1->getChild(0).get<RenderingComponent>();

    flagtComp->setPosition(vec3<F32>(25.0f, 0.1f, 206.0f));
    flagtComp->setScale(flag->get<TransformComponent>()->getScale());

    flagNComp->navigationContext(NavigationComponent::NavigationContext::NODE_IGNORE);

    flagRComp->getMaterialInstance()->setDiffuse(DefaultColours::RED);

    sceneryNodeDescriptor._name = "FirstPersonFlag";
    SceneGraphNode* firstPersonFlag = _sceneGraph->getRoot().addNode(sceneryNodeDescriptor);
    firstPersonFlag->lockVisibility(true);

    flagtComp = firstPersonFlag->get<TransformComponent>();
    flagtComp->setScale(0.0015f);
    flagtComp->setPosition(1.25f, -1.5f, 0.15f);
    flagtComp->rotate(-20.0f, -70.0f, 50.0f);

    auto collision = [this](const RigidBodyComponent& collider) {
        weaponCollision(collider);
    };
    RigidBodyComponent* rComp = firstPersonFlag->get<RigidBodyComponent>();
    rComp->onCollisionCbk(collision);
    flagRComp = firstPersonFlag->getChild(0).get<RenderingComponent>();
    flagRComp->getMaterialInstance()->setDiffuse(DefaultColours::GREEN);

    firstPersonFlag->get<RigidBodyComponent>()->physicsGroup(PhysicsGroup::GROUP_KINEMATIC);

    _firstPersonWeapon = firstPersonFlag;

    AI::WarSceneAIProcessor::registerFlags(_flag[0], _flag[1]);

    AI::WarSceneAIProcessor::registerScoreCallback([&](U8 teamID, const string& unitName) {
        registerPoint(teamID, unitName);
    });

    AI::WarSceneAIProcessor::registerMessageCallback([&](U8 eventID, const string& unitName) {
        printMessage(eventID, unitName);
    });
#endif

    //state().renderState().generalVisibility(state().renderState().generalVisibility() * 2);

    ResourceDescriptor<TransformNode> transformDescriptor("LightTransform");

    SceneGraphNodeDescriptor lightParentNodeDescriptor;
    lightParentNodeDescriptor._nodeHandle = FromHandle( CreateResource( transformDescriptor ) );
    lightParentNodeDescriptor._serialize = false;
    lightParentNodeDescriptor._name = "Point Lights";
    lightParentNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
    lightParentNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                               to_base(ComponentType::BOUNDS) |
                                               to_base(ComponentType::NETWORKING);
    SceneGraphNode* pointLightNode = _sceneGraph->getRoot()->addChildNode(lightParentNodeDescriptor);
    pointLightNode->get<BoundsComponent>()->collisionsEnabled(false);

    SceneGraphNodeDescriptor lightNodeDescriptor;
    lightNodeDescriptor._serialize = false;
    lightNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
    lightNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                         to_base(ComponentType::BOUNDS) |
                                         to_base(ComponentType::NETWORKING) |
                                         to_base(ComponentType::POINT_LIGHT);

    constexpr U8 rowCount = 10;
    constexpr U8 colCount = 10;
    for (U8 row = 0; row < rowCount; row++)
    {
        for (U8 col = 0; col < colCount; col++)
        {
            Util::StringFormat( lightNodeDescriptor._name, "Light_point_{}_{}", row, col);
            lightNodeDescriptor._nodeHandle = FromHandle( CreateResource( transformDescriptor ) );

            SceneGraphNode* lightSGN = pointLightNode->addChildNode(lightNodeDescriptor);

            PointLightComponent* pointLight = lightSGN->get<PointLightComponent>();
            pointLight->castsShadows(false);
            pointLight->range(50.0f);
            pointLight->setDiffuseColour(DefaultColours::RANDOM().rgb);

            TransformComponent* tComp = lightSGN->get<TransformComponent>();
            tComp->setPosition(vec3<F32>(-21.0f + 115 * row, 20.0f, -21.0f + 115 * col));

            lightSGN->get<BoundsComponent>()->collisionsEnabled(false);

            _lightNodeTransforms.push_back(tComp);
        }
    }
    
    Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT)->setHorizontalFoV(110);

    _sceneReady = true;
    if (loadState) {
        return initializeAI(true);
    }
    return false;
}


bool WarScene::unload() {
    deinitializeAI(true);
    return Scene::unload();
}

U16 WarScene::registerInputActions() {
    U16 actionID = Scene::registerInputActions();

    //ToDo: Move these to per-scene XML file
    {
        PressReleaseActions::Entry actionEntry = {};
        actionEntry.releaseIDs().insert(actionID);
        if (!_input->actionList().registerInputAction(actionID, [this](const InputParams& param) {toggleCamera(param); })) {
            DIVIDE_UNEXPECTED_CALL();
        }
        _input->addKeyMapping(Input::KeyCode::KC_TAB, actionEntry);
        actionID++;
    }
    {
        PressReleaseActions::Entry actionEntry = {};
        if (!_input->actionList().registerInputAction(actionID, [this](const InputParams& /*param*/) {registerPoint(0u, ""); })) {
            DIVIDE_UNEXPECTED_CALL();
        }
        actionEntry.releaseIDs().insert(actionID);
        _input->addKeyMapping(Input::KeyCode::KC_1, actionEntry);
        actionID++;
    }
    {
        PressReleaseActions::Entry actionEntry = {};
        if (!_input->actionList().registerInputAction(actionID, [this](const InputParams& /*param*/) {registerPoint(1u, ""); })) {
            DIVIDE_UNEXPECTED_CALL();
        }
        actionEntry.releaseIDs().insert(actionID);
        _input->addKeyMapping(Input::KeyCode::KC_2, actionEntry);
        actionID++;
    }
    {
        PressReleaseActions::Entry actionEntry = {};
        if (!_input->actionList().registerInputAction(actionID, [this](InputParams /*param*/) {
            /// TTT -> TTF -> TFF -> FFT -> FTT -> TFT -> TTT
            const bool dir   = _lightPool->lightTypeEnabled(LightType::DIRECTIONAL);
            const bool point = _lightPool->lightTypeEnabled(LightType::POINT);
            const bool spot  = _lightPool->lightTypeEnabled(LightType::SPOT);
            if (dir && point && spot) {
                _lightPool->toggleLightType(LightType::SPOT, false);
            } else if (dir && point && !spot) {
                _lightPool->toggleLightType(LightType::POINT, false);
            } else if (dir && !point && !spot) {
                _lightPool->toggleLightType(LightType::DIRECTIONAL, false);
                _lightPool->toggleLightType(LightType::SPOT, true);
            } else if (!dir && !point && spot) {
                _lightPool->toggleLightType(LightType::POINT, true);
            } else if (!dir && point && spot) {
                _lightPool->toggleLightType(LightType::DIRECTIONAL, true);
                _lightPool->toggleLightType(LightType::POINT, false);
            } else {
                _lightPool->toggleLightType(LightType::POINT, true);
            }
        })) {
            DIVIDE_UNEXPECTED_CALL();
        }
        actionEntry.releaseIDs().insert(actionID);
        _input->addKeyMapping(Input::KeyCode::KC_L, actionEntry);
    }

    return actionID++;
}

void WarScene::toggleCamera(const InputParams param) {
    // None of this works with multiple players
    static bool tpsCameraActive = false;
    static bool flyCameraActive = true;
    static Camera* tpsCamera = nullptr;

    if (!tpsCamera) {
        tpsCamera = Camera::FindCamera(_ID("tpsCamera"));
    }

    const PlayerIndex idx = getPlayerIndexForDevice(param._deviceIndex);
    if (_currentSelection[idx]._selectionCount > 0u) {
        SceneGraphNode* node = sceneGraph()->findNode(_currentSelection[idx]._selections[0]);
        if (node != nullptr) {
            if (flyCameraActive) {
                state()->playerState(idx).overrideCamera(tpsCamera);
                tpsCamera->setTarget(node->get<TransformComponent>(), vec3<F32>(0.f, 0.75f, 1.f));
                flyCameraActive = false;
                tpsCameraActive = true;
                return;
            }
        }
    }
    if (tpsCameraActive) {
        state()->playerState(idx).overrideCamera(nullptr);
        tpsCameraActive = false;
        flyCameraActive = true;
    }
}

void WarScene::postLoadMainThread() {
    const vec2<U16> screenResolution = _context.gfx().renderTargetPool().getRenderTarget(RenderTargetNames::SCREEN)->getResolution();
    const Rect<U16> targetRenderViewport = { 0u, 0u, screenResolution.width, screenResolution.height };

    GUIButton* btn = _GUI->addButton("Simulate",
                                     "Simulate",
                                     pixelPosition(targetRenderViewport.sizeX - 220, 60),
                                     pixelScale(100, 25));
    btn->setEventCallback(GUIButton::Event::MouseClick, [this](const I64 btnGUID) { startSimulation(btnGUID); });

    btn = _GUI->addButton("TerrainMode",
                          "Terrain Mode Toggle",
                          pixelPosition(targetRenderViewport.sizeX - 240, 90),
                          pixelScale(120, 25));
    btn->setEventCallback(GUIButton::Event::MouseClick,
        [this](I64 /*btnID*/) { toggleTerrainMode(); });

    _GUI->addText("RenderBinCount", 
                  pixelPosition(60, 83),
                  Font::DIVIDE_DEFAULT,
                  UColour4(164, 50, 50, 255),
                  Util::StringFormat("Number of items in Render Bin: {}", 0));

    _GUI->addText("camPosition", pixelPosition(60, 103),
                  Font::DIVIDE_DEFAULT,
                  UColour4(50, 192, 50, 255),
                  Util::StringFormat("Position [ X: {:5.0f} | Y: {:5.0f} | Z: {:5.0f} ] [Pitch: {:5.2f} | Yaw: {:5.2f}]",
                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f));


    _GUI->addText("scoreDisplay",
                  pixelPosition(60, 123),  // Position
                  Font::DIVIDE_DEFAULT,  // Font
                  UColour4(50, 192, 50, 255),// Colour
                  Util::StringFormat("Score: A -  {} B - {}", 0, 0));  // Text and arguments

    _GUI->addText("terrainInfoDisplay",
                  pixelPosition(60, 163),  // Position
                  Font::DIVIDE_DEFAULT,  // Font
                  UColour4(128, 0, 0, 255),// Colour
                  "Terrain Data");  // Text and arguments

    _GUI->addText("entityState",
                  pixelPosition(60, 223),
                  Font::DIVIDE_DEFAULT,
                  UColour4(0, 0, 0, 255),
                  "",
                  true);

    _infoBox = _GUI->addMsgBox("infoBox", "Info", "Blabla");

    // Add a first person camera
    {
        Camera* cam = Camera::CreateCamera("fpsCamera", Camera::Mode::FIRST_PERSON );
        cam->fromCamera(*Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT));
        cam->speedFactor().move = 10.f;
        cam->speedFactor().turn = 10.f;
    }
    // Add a third person camera
    {
        Camera* cam = Camera::CreateCamera("tpsCamera", Camera::Mode::THIRD_PERSON );
        cam->fromCamera(*Camera::GetUtilityCamera(Camera::UtilityCamera::DEFAULT));
        cam->speedFactor().move = 0.02f;
        cam->speedFactor().turn = 0.01f;
    }

    /*
    addTaskTimer(TimerClass::GAME_TIME,
                 Time::SecondsToMicroseconds( 5.0 ),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                     for (SceneGraphNode* npc : _armyNPCs[0]) {
                         assert(npc);
                         npc->get<UnitComponent>()->getUnit<NPC>()->playNextAnimation();
                     
                 } );

    addTaskTimer( TimerClass::GAME_TIME,
                 Time::SecondsToMicroseconds( 10.0 ),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                     for (SceneGraphNode* npc : _armyNPCs[1]) {
                         assert(npc);
                         npc->get<UnitComponent>()->getUnit<NPC>()->playNextAnimation();
                     }
                 });
    */

    addTaskTimer( TimerClass::GAME_TIME,
                  Time::MillisecondsToMicroseconds( 33 ),
                  [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                  {
                      thread_local F32 phiLight = 0.0f;
                      thread_local bool initPosSetLight = false;
                      NO_DESTROY thread_local vector<vec3<F32>> initPosLight;

                      if ( !initPosSetLight )
                      {
                          const size_t lightCount = _lightNodeTransforms.size();
                          initPosLight.resize( lightCount );
                          for ( size_t i = 0u; i < lightCount; ++i )
                          {
                              initPosLight[i].set( _lightNodeTransforms[i]->getWorldPosition() );
                          }
                          initPosSetLight = true;
                      }

                      const size_t lightCount = _lightNodeTransforms.size();
                      constexpr F32 radius = 150.f;
                      constexpr F32 height = 25.f;
                      
                      phiLight += 0.01f;
                      if (phiLight > 360.0f)
                      {
                          phiLight = 0.0f;
                      }
                      
                      const F32 s1 = std::sin( phiLight);
                      const F32 c1 = std::cos( phiLight);
                      const F32 s2 = std::sin(-phiLight);
                      const F32 c2 = std::cos(-phiLight);
                      
                      for ( size_t i = 0u; i < lightCount; ++i )
                      {
                          const F32 c = i % 2 == 0 ? c1 : c2;
                          const F32 s = i % 2 == 0 ? s1 : s2;
                      
                          _lightNodeTransforms[i]->setPosition(
                              radius  * c + initPosLight[i].x,
                              height  * s + initPosLight[i].y,
                              radius  * s + initPosLight[i].z
                          );
                      }
                  });

    addGuiTimer( TimerClass::APP_TIME,
                 Time::SecondsToMicroseconds( 0.25),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                     const Camera& cam = *_scenePlayers.front()->camera();
                     vec3<F32> eyePos = cam.snapshot()._eye;
                     const vec3<F32>& euler = cam.euler();
     
                     _GUI->modifyText("RenderBinCount",
                                         Util::StringFormat("Number of items in Render Bin: {}.",
                                         _context.kernel().renderPassManager()->getLastTotalBinSize(RenderStage::DISPLAY)), false);
     
                     _GUI->modifyText("camPosition",
                                         Util::StringFormat("Position [ X: {:5.2f} | Y: {:5.2f} | Z: {:5.2f} ] [Pitch: {:5.2f} | Yaw: {:5.2f}]",
                                                         eyePos.x, eyePos.y, eyePos.z, euler.pitch, euler.yaw), false); 
         
                     static SceneGraphNode* terrain = nullptr;
                     if ( terrain == nullptr )
                     {
                         vector<SceneGraphNode*> terrains = _sceneGraph->getNodesByType( SceneNodeType::TYPE_TERRAIN );
                         if ( !terrains.empty() )
                         {
                             terrain = terrains.front();
                         }
                     }

                     if (terrain != nullptr)
                     {
                         const Terrain& ter = terrain->getNode<Terrain>();
                         CLAMP<F32>(eyePos.x,
                                     ter.getDimensions().width * 0.5f * -1.0f,
                                     ter.getDimensions().width * 0.5f);
                         CLAMP<F32>(eyePos.z,
                                     ter.getDimensions().height * 0.5f * -1.0f,
                                     ter.getDimensions().height * 0.5f);
                         mat4<F32> mat = MAT4_IDENTITY;
                         terrain->get<TransformComponent>()->getWorldMatrix(mat);
                         Terrain::Vert terVert = ter.getVertFromGlobal(eyePos.x, eyePos.z, true);
                         const vec3<F32> terPos = mat * terVert._position;
                         const vec3<F32>& terNorm = terVert._normal;
                         const vec3<F32>& terTan = terVert._tangent;
                         _GUI->modifyText("terrainInfoDisplay",
                                             Util::StringFormat("Position [ X: {:5.2f} | Y: {:5.2f} | Z: {:5.2f} ]\nNormal [ X: {:5.2f} | Y: {:5.2f} | Z: {:5.2f} ]\nTangent [ X: {:5.2f} | Y: {:5.2f} | Z: {:5.2f} ]",
                                                 terPos.x, terPos.y, terPos.z, 
                                                 terNorm.x, terNorm.y, terNorm.z,
                                                 terTan.x, terTan.y, terTan.z),
                                         true);
                     }
                 });

    addGuiTimer( TimerClass::GAME_TIME,
                 Time::SecondsToMicroseconds( 1.0 ),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                    string selectionText;
                    const Selections& selections = _currentSelection[0];
                    for (U8 i = 0u; i < selections._selectionCount; ++i) {
                        SceneGraphNode* node = sceneGraph()->findNode(selections._selections[i]);
                        if (node != nullptr) {
                            AI::AIEntity* entity = findAI(node);
                            if (entity) {
                                selectionText.append("\n");
                                selectionText.append(entity->toString());
                            }
                        }
                    }
                    if (!selectionText.empty()) {
                        _GUI->modifyText("entityState", selectionText, true);
                    }
                 });

    addGuiTimer( TimerClass::GAME_TIME,
                 Time::SecondsToMicroseconds( 5.0 ),
                 [this]( [[maybe_unused]] const U64 elapsedTimeUS )
                 {
                    U32 elapsedTimeMinutes = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) / 60 % 60;
                    U32 elapsedTimeSeconds = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) % 60;
                    U32 elapsedTimeMilliseconds = Time::MicrosecondsToMilliseconds<U32>(_elapsedGameTime) % 1000;


                    U32 limitTimeMinutes = _timeLimitMinutes;
                    U32 limitTimeSeconds = 0;
                    U32 limitTimeMilliseconds = 0;

                    _GUI->modifyText("scoreDisplay",
                        Util::StringFormat("Score: A -  {} B - {} [Limit: {}]\nElapsed game time [ {}:{}:{} / {}:{}:{}]",
                                            AI::WarSceneAIProcessor::getScore(0),
                                            AI::WarSceneAIProcessor::getScore(1),
                                            _scoreLimit,
                                            elapsedTimeMinutes,
                                            elapsedTimeSeconds,
                                            elapsedTimeMilliseconds,
                                            limitTimeMinutes,
                                            limitTimeSeconds,
                                            limitTimeMilliseconds),
                                            true);
                 });

    Scene::postLoadMainThread();
}

void WarScene::onSetActive() {
    Scene::onSetActive();
    //playerCamera()->lockToObject(_firstPersonWeapon);
}

void WarScene::weaponCollision(const RigidBodyComponent& collider) {
    Console::d_printfn("Weapon touched [ {} ]", collider.parentSGN()->name().c_str());
}

}
