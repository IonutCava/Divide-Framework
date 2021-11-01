#include "stdafx.h"

#include "Headers/WarScene.h"
#include "Headers/WarSceneAIProcessor.h"

#include "GUI/Headers/GUIButton.h"
#include "GUI/Headers/GUIMessageBox.h"
#include "Geometry/Material/Headers/Material.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "Rendering/Camera/Headers/ThirdPersonCamera.h"
#include "Dynamics/Entities/Units/Headers/Player.h"
#include "Managers/Headers/SceneManager.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"

#include "ECS/Components/Headers/BoundsComponent.h"
#include "ECS/Components/Headers/RigidBodyComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"

#include "Environment/Terrain/Headers/Terrain.h"
#include "Platform/Video/Headers/RenderStateBlock.h"

namespace Divide {

WarScene::WarScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name)
   : Scene(context, cache, parent, name),
    _timeLimitMinutes(5),
    _scoreLimit(3)
{
    const size_t idx = parent.addSelectionCallback([&](PlayerIndex /*idx*/, const vector<SceneGraphNode*>& node) {
        string selectionText;
        for (SceneGraphNode* it : node) {
            selectionText.append("\n");
            selectionText.append(it->name().c_str());
        }

        _GUI->modifyText("entityState", selectionText, true);
    });

    _selectionCallbackIndices.push_back(idx);
}

WarScene::~WarScene()
{
    if (_targetLines) {
        _context.gfx().destroyIMP(_targetLines);
    }
}

void WarScene::processGUI(const U64 deltaTimeUS) {
    constexpr D64 FpsDisplay = Time::SecondsToMilliseconds(0.3);
    static SceneGraphNode* terrain = nullptr;
    if (terrain == nullptr) {
        vector<SceneGraphNode*> terrains = Object3D::filterByType(_sceneGraph->getNodesByType(SceneNodeType::TYPE_OBJECT3D), ObjectType::TERRAIN);
        if (!terrains.empty()) {
            terrain = terrains.front();
        }
    }

    if (_guiTimersMS[0] >= FpsDisplay) {
        const Camera& cam = *_scenePlayers.front()->camera();
        vec3<F32> eyePos = cam.getEye();
        const vec3<F32>& euler = cam.getEuler();

        _GUI->modifyText("RenderBinCount",
                         Util::StringFormat("Number of items in Render Bin: %d.",
                         _context.kernel().renderPassManager()->getLastTotalBinSize(RenderStage::DISPLAY)), false);

        _GUI->modifyText("camPosition",
                         Util::StringFormat("Position [ X: %5.2f | Y: %5.2f | Z: %5.2f ] [Pitch: %5.2f | Yaw: %5.2f]",
                                            eyePos.x, eyePos.y, eyePos.z, euler.pitch, euler.yaw), false); 
        
        if (terrain != nullptr) {
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
                             Util::StringFormat("Position [ X: %5.2f | Y: %5.2f | Z: %5.2f ]\nNormal [ X: %5.2f | Y: %5.2f | Z: %5.2f ]\nTangent [ X: %5.2f | Y: %5.2f | Z: %5.2f ]",
                                 terPos.x, terPos.y, terPos.z, 
                                 terNorm.x, terNorm.y, terNorm.z,
                                 terTan.x, terTan.y, terTan.z),
                            true);
        }
        _guiTimersMS[0] = 0.0;
    }

    if (_guiTimersMS[1] >= Time::SecondsToMilliseconds(1)) {
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
    }

    if (_guiTimersMS[2] >= 66) {
        U32 elapsedTimeMinutes = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) / 60 % 60;
        U32 elapsedTimeSeconds = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) % 60;
        U32 elapsedTimeMilliseconds = Time::MicrosecondsToMilliseconds<U32>(_elapsedGameTime) % 1000;


        U32 limitTimeMinutes = _timeLimitMinutes;
        U32 limitTimeSeconds = 0;
        U32 limitTimeMilliseconds = 0;

        _GUI->modifyText("scoreDisplay",
            Util::StringFormat("Score: A -  %d B - %d [Limit: %d]\nElapsed game time [ %d:%d:%d / %d:%d:%d]",
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

        _guiTimersMS[2] = 0.0;
    }

    Scene::processGUI(deltaTimeUS);
}

namespace {
    F32 phiLight = 0.0f;
    bool initPosSetLight = false;
    vector<vec3<F32>> initPosLight;
}

void WarScene::toggleTerrainMode() {
    _terrainMode = !_terrainMode;
}

void WarScene::debugDraw(const Camera* activeCamera, const RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut) {
    if (state()->renderState().isEnabledOption(SceneRenderState::RenderOptions::RENDER_CUSTOM_PRIMITIVES)) {
        if (!_targetLines) {
            _targetLines = _context.gfx().newIMP();

            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._shaderProgramHandle = ShaderProgram::DefaultShader()->getGUID();

            RenderStateBlock primitiveStateBlockNoZRead = {};
            primitiveStateBlockNoZRead.depthTestEnabled(false);
            pipelineDescriptor._stateHash = primitiveStateBlockNoZRead.getHash();
            _targetLines->pipeline(*_context.gfx().newPipeline(pipelineDescriptor));
        } else {
            bufferInOut.add(_targetLines->toCommandBuffer());
        }
    } else if (_targetLines) {
        _context.gfx().destroyIMP(_targetLines);
    }
    Scene::debugDraw(activeCamera, stagePass, bufferInOut);
}

void WarScene::processTasks(const U64 deltaTimeUS) {
    if (!_sceneReady) {
        return;
    }

    constexpr D64 AnimationTimer1 = Time::SecondsToMilliseconds(5);
    constexpr D64 AnimationTimer2 = Time::SecondsToMilliseconds(10);
    constexpr D64 updateLights = Time::Milliseconds(32);

    if (_taskTimers[1] >= AnimationTimer1) {
        /*for (SceneGraphNode* npc : _armyNPCs[0]) {
            assert(npc);
            npc->get<UnitComponent>()->getUnit<NPC>()->playNextAnimation();
        }*/
        _taskTimers[1] = 0.0;
    }

    if (_taskTimers[2] >= AnimationTimer2) {
        /*for (SceneGraphNode* npc : _armyNPCs[1]) {
            assert(npc);
            npc->get<UnitComponent>()->getUnit<NPC>()->playNextAnimation();
        }*/
        _taskTimers[2] = 0.0;
    }

    const size_t lightCount = _lightNodeTransforms.size();
    if (!initPosSetLight) {
        initPosLight.resize(lightCount);
        for (U8 i = 0; i < lightCount; ++i) {
            initPosLight[i].set(_lightNodeTransforms[i]->getPosition());
        }
        initPosSetLight = true;
    }

    if (_taskTimers[3] >= updateLights) {
        constexpr F32 radius = 20;
        constexpr F32 halfRadius = radius * 0.5f;

        phiLight += 0.01f;
        if (phiLight > 360.0f) {
            phiLight = 0.0f;
        }

        const F32 s1 = std::sin(phiLight);
        const F32 c1 = std::cos(phiLight);
        const F32 s2 = std::sin(-phiLight);
        const F32 c2 = std::cos(-phiLight);
        
        for (size_t i = 0; i < lightCount; ++i) {
            const F32 c = i % 2 == 0 ? c1 : c2;
            const F32 s = i % 2 == 0 ? s1 : s2;

            const vec3<F32>& initPos = initPosLight[i];
            _lightNodeTransforms[i]->setPosition(
                radius * c + initPos.x,
                halfRadius * s + initPos.y,
                radius * s + initPos.z
            );
        }
        _taskTimers[3] = 0.0;
    }

    Scene::processTasks(deltaTimeUS);
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
    //OPTICK_EVENT();

    if (!_sceneReady) {
        return;
    }

    if (_terrainMode) {
        if (g_terrain == nullptr) {
            auto objects = sceneGraph()->getNodesByType(SceneNodeType::TYPE_OBJECT3D);
            for (SceneGraphNode* object : objects) {
                if (object->getNode<Object3D>().getObjectType() == ObjectType::TERRAIN) {
                    g_terrain = object;
                    break;
                }
            }
        } else {
            vec3<F32> camPos = playerCamera()->getEye();
            if (g_terrain->get<BoundsComponent>()->getBoundingBox().containsPoint(camPos)) {
                const Terrain& ter = g_terrain->getNode<Terrain>();

                F32 headHeight = state()->playerState(state()->playerPass())._headHeight;
                camPos -= g_terrain->get<TransformComponent>()->getPosition();
                playerCamera()->setEye(ter.getVertFromGlobal(camPos.x, camPos.z, true)._position + vec3<F32>(0.0f, headHeight, 0.0f));
            }
        }
    }

    if_constexpr(g_enableOldGameLogic) {
        if (_resetUnits) {
            resetUnits();
            _resetUnits = false;
        }

        SceneGraphNode* particles = _particleEmitter;
        const F32 radius = 20;

        if (particles) {
            phi += 0.001f;
            if (phi > 360.0f) {
                phi = 0.0f;
            }

            TransformComponent* tComp = particles->get<TransformComponent>();
            if (!initPosSet) {
                initPos.set(tComp->getPosition());
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

bool WarScene::load(const Str256& name) {
    // Load scene resources
    const bool loadState = Scene::load(name);
    setDayNightCycleTimeFactor(24);

    // Position camera
    Camera::utilityCamera(Camera::UtilityCamera::DEFAULT)->setEye(vec3<F32>(43.13f, 147.09f, -4.41f));
    Camera::utilityCamera(Camera::UtilityCamera::DEFAULT)->setGlobalRotation(-90.0f /*yaw*/, 59.21f /*pitch*/);

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
    matInstance->shininess(200);
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
    std::pair<I32, I32> currentPos;
    for (U8 i = 0; i < 40; ++i) {
        if (i < 10) {
            baseNode = cylinder[1];
            currentMesh = cylinderMeshNW;
            currentName = "Cylinder_NW_" + Util::to_string((I32)i);
            currentPos.first = -200 + 40 * i + 50;
            currentPos.second = -200 + 40 * i + 50;
        } else if (i >= 10 && i < 20) {
            baseNode = cylinder[2];
            currentMesh = cylinderMeshNE;
            currentName = "Cylinder_NE_" + Util::to_string((I32)i);
            currentPos.first = 200 - 40 * (i % 10) - 50;
            currentPos.second = -200 + 40 * (i % 10) + 50;
            locationFlag = 1;
        } else if (i >= 20 && i < 30) {
            baseNode = cylinder[3];
            currentMesh = cylinderMeshSW;
            currentName = "Cylinder_SW_" + Util::to_string((I32)i);
            currentPos.first = -200 + 40 * (i % 20) + 50;
            currentPos.second = 200 - 40 * (i % 20) - 50;
            locationFlag = 2;
        } else {
            baseNode = cylinder[4];
            currentMesh = cylinderMeshSE;
            currentName = "Cylinder_SE_" + Util::to_string((I32)i);
            currentPos.first = 200 - 40 * (i % 30) - 50;
            currentPos.second = 200 - 40 * (i % 30) - 50;
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

        vec3<F32> position(to_F32(currentPos.first), -0.01f, to_F32(currentPos.second));
        tComp->setScale(baseNode->get<TransformComponent>()->getScale());
        tComp->setPosition(position);
        {
            ResourceDescriptor tempLight(Util::StringFormat("Light_point_random_1", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::POINT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource<Light>(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(25.0f);
            //light->setCastShadows(i == 0 ? true : false);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 8.0f, 0.0f));
            _lightNodes2.emplace_back(lightSGN, false));
        }
        {
            ResourceDescriptor tempLight(Util::StringFormat("Light_point_%s_2", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::POINT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource<Light>(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(35.0f);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 8.0f, 0.0f));
            _lightNodes2.emplace_back(lightSGN, true);
        }
        {
            ResourceDescriptor tempLight(Util::StringFormat("Light_spot_%s", currentName.c_str()));
            tempLight.setEnumValue(to_base(LightType::SPOT));
            tempLight.setUserPtr(_lightPool);
            std::shared_ptr<Light> light = CreateResource<Light>(_resCache, tempLight);
            light->setDrawImpostor(false);
            light->setRange(55.0f);
            //light->setCastShadows(i == 1 ? true : false);
            light->setCastShadows(false);
            light->setDiffuseColour(DefaultColours::RANDOM());
            SceneGraphNode* lightSGN = _sceneGraph->getRoot().addNode(light, lightMask);
            lightSGN->get<TransformComponent>()->setPosition(position + vec3<F32>(0.0f, 10.0f, 0.0f));
            lightSGN->get<TransformComponent>()->rotateX(-20);
            _lightNodes3.push_back(lightSGN);
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

    
    SceneGraphNodeDescriptor lightParentNodeDescriptor;
    lightParentNodeDescriptor._serialize = false;
    lightParentNodeDescriptor._name = "Point Lights";
    lightParentNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
    lightParentNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                               to_base(ComponentType::BOUNDS) |
                                               to_base(ComponentType::NETWORKING);
    SceneGraphNode* pointLightNode = _sceneGraph->getRoot()->addChildNode(lightParentNodeDescriptor);

    SceneGraphNodeDescriptor lightNodeDescriptor;
    lightNodeDescriptor._serialize = false;
    lightNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
    lightNodeDescriptor._componentMask = to_base(ComponentType::TRANSFORM) |
                                         to_base(ComponentType::BOUNDS) |
                                         to_base(ComponentType::NETWORKING) |
                                         to_base(ComponentType::POINT_LIGHT);

    constexpr U8 rowCount = 10;
    constexpr U8 colCount = 10;
    for (U8 row = 0; row < rowCount; row++) {
        for (U8 col = 0; col < colCount; col++) {
            lightNodeDescriptor._name = Util::StringFormat("Light_point_%d_%d", row, col);
            SceneGraphNode* lightSGN = pointLightNode->addChildNode(lightNodeDescriptor);
            PointLightComponent* pointLight = lightSGN->get<PointLightComponent>();
            pointLight->castsShadows(false);
            pointLight->range(50.0f);
            pointLight->setDiffuseColour(DefaultColours::RANDOM().rgb);
            TransformComponent* tComp = lightSGN->get<TransformComponent>();
            tComp->setPosition(vec3<F32>(-21.0f + 115 * row, 20.0f, -21.0f + 115 * col));
            _lightNodeTransforms.push_back(tComp);
        }
    }
    
    Camera::utilityCamera(Camera::UtilityCamera::DEFAULT)->setHorizontalFoV(110);

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
        tpsCamera = Camera::findCamera(_ID("tpsCamera"));
    }

    const PlayerIndex idx = getPlayerIndexForDevice(param._deviceIndex);
    if (_currentSelection[idx]._selectionCount > 0u) {
        SceneGraphNode* node = sceneGraph()->findNode(_currentSelection[idx]._selections[0]);
        if (node != nullptr) {
            if (flyCameraActive) {
                state()->playerState(idx).overrideCamera(tpsCamera);
                static_cast<ThirdPersonCamera*>(tpsCamera)->setTarget(node);
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
    const vec2<U16> screenResolution = _context.gfx().renderTargetPool().screenTarget().getResolution();
    const Rect<U16> targetRenderViewport = { 0u, 0u, screenResolution.width, screenResolution.height };

    GUIButton* btn = _GUI->addButton("Simulate",
                                     "Simulate",
                                     pixelPosition(targetRenderViewport.sizeX - 220, 60),
                                     pixelScale(100, 25));
    btn->setEventCallback(GUIButton::Event::MouseClick, [this](const I64 btnGUID) { startSimulation(btnGUID); });

    btn = _GUI->addButton("ShaderReload",
                          "Shader Reload",
                          pixelPosition(targetRenderViewport.sizeX - 220, 30),
                          pixelScale(100, 25));
    btn->setEventCallback(GUIButton::Event::MouseClick,
                         [this](I64 /*btnID*/) { rebuildShaders(); });

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
                  Util::StringFormat("Number of items in Render Bin: %d", 0));

    _GUI->addText("camPosition", pixelPosition(60, 103),
                  Font::DIVIDE_DEFAULT,
                  UColour4(50, 192, 50, 255),
                  Util::StringFormat("Position [ X: %5.0f | Y: %5.0f | Z: %5.0f ] [Pitch: %5.2f | Yaw: %5.2f]",
                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f));


    _GUI->addText("scoreDisplay",
                  pixelPosition(60, 123),  // Position
                  Font::DIVIDE_DEFAULT,  // Font
                  UColour4(50, 192, 50, 255),// Colour
                  Util::StringFormat("Score: A -  %d B - %d", 0, 0));  // Text and arguments

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
        FreeFlyCamera* cam = Camera::createCamera<FreeFlyCamera>("fpsCamera");
        cam->fromCamera(*Camera::utilityCamera(Camera::UtilityCamera::DEFAULT));
        cam->setMoveSpeedFactor(10.0f);
        cam->setTurnSpeedFactor(10.0f);
    }
    // Add a third person camera
    {
        ThirdPersonCamera* cam = Camera::createCamera<ThirdPersonCamera>("tpsCamera");
        cam->fromCamera(*Camera::utilityCamera(Camera::UtilityCamera::DEFAULT));
        cam->setMoveSpeedFactor(0.02f);
        cam->setTurnSpeedFactor(0.01f);
    }
    _guiTimersMS.push_back(0.0);  // Fps
    _guiTimersMS.push_back(0.0);  // AI info
    _guiTimersMS.push_back(0.0);  // Game info
    _taskTimers.push_back(0.0); // Sun animation
    _taskTimers.push_back(0.0); // animation team 1
    _taskTimers.push_back(0.0); // animation team 2
    _taskTimers.push_back(0.0); // light timer

    Scene::postLoadMainThread();
}

void WarScene::onSetActive() {
    Scene::onSetActive();
    //_firstPersonWeapon->lockToCamera(_ID(playerCamera()->name()));
}

void WarScene::weaponCollision(const RigidBodyComponent& collider) {
    Console::d_printfn("Weapon touched [ %s ]", collider.getSGN()->name().c_str());
}

}
