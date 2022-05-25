#include "stdafx.h"

#include "Headers/WarScene.h"
#include "Headers/WarSceneAIProcessor.h"

#include "AI/Headers/AIManager.h"
#include "Graphs/Headers/SceneGraph.h"
#include "GUI/Headers/GUIMessageBox.h"
#include "Managers/Headers/SceneManager.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "ECS/Components/Headers/UnitComponent.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"
#include "Core/Headers/Application.h"

namespace Divide {

namespace {
    bool g_navMeshStarted = false;
}

void WarScene::printMessage(const U8 eventId, const string& unitName) const {
    string eventName;
    switch (eventId) {
        case 0:
          eventName = "Captured flag";
          break;
        case 1:
          eventName = "Recovered flag";
          break;
        default: 
          eventName = "Unknown!";
          break;
    }

    U32 elapsedTimeMinutes = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) / 60 % 60;
    U32 elapsedTimeSeconds = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) % 60;

    Console::d_printfn(Util::StringFormat("[GAME TIME: %d:%d][%s]: %s", elapsedTimeMinutes, elapsedTimeSeconds, eventName.c_str(), unitName.c_str()).c_str());
}

void WarScene::checkGameCompletion() {
    bool restartGame = false;
    bool timeReason = false;
    if (AI::WarSceneAIProcessor::getScore(0) == _scoreLimit ||
        AI::WarSceneAIProcessor::getScore(1) == _scoreLimit) {
        _elapsedGameTime = 0;
        if (_scoreLimit == 3) {
            _scoreLimit = 5;
            _timeLimitMinutes = 8;
            restartGame = true;
        } else if (_scoreLimit == 5) {
            _scoreLimit = 20;
            _timeLimitMinutes = 30;
            restartGame = true;
        }
        else if (_scoreLimit == 20) {
            if (_runCount == 1) {
                _scoreLimit = 3;
                _timeLimitMinutes = 5;
                _runCount = 2;
                restartGame = true;
            } else if (_runCount == 2) {
                _context.app().RequestShutdown();
            }
        }
    }

    const U32 elapsedTimeMinutes = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) / 60 % 60;
    if (elapsedTimeMinutes >= _timeLimitMinutes) {
        timeReason = true;
        if (_timeLimitMinutes == 5) {
            _timeLimitMinutes = 8;
            _scoreLimit = 5;
        } else if (_timeLimitMinutes == 8) {
            _timeLimitMinutes = 30;
            _scoreLimit = 20;
        } else if (_timeLimitMinutes == 30) {
            if (_runCount == 1) {
                _timeLimitMinutes = 5;
                _scoreLimit = 3;
                _runCount = 2;
            }
            else if (_runCount == 2) {
                _context.app().RequestShutdown();
            }
        }
        restartGame = true;
    }

    if (restartGame) {
        _elapsedGameTime = 0;
        Console::d_printfn(Util::StringFormat("\n\nRESTARTING GAME!. Reason: %s \n\n", timeReason ? "TIME" : "SCORE").c_str());
        AI::WarSceneAIProcessor::resetScore(0);
        AI::WarSceneAIProcessor::resetScore(1);
        if (timeReason) {
            _resetUnits = true;
            for (U8 i = 0; i < 2; ++i) {
                TransformComponent* flagtComp = _flag[i]->get<TransformComponent>();
                flagtComp->popTransforms();
                _flag[i]->setParent(_sceneGraph->getRoot());
                flagtComp->setPosition(vec3<F32>(25.0f, 0.1f, i == 0 ? -206.0f : 206.0f));
            }
            AI::WarSceneAIProcessor::reset();
            AI::WarSceneAIProcessor::registerFlags(_flag[0], _flag[1]);
        }
    }
}

void WarScene::registerPoint(const U16 teamID, const string& unitName) {
    if (!_resetUnits) {
        _resetUnits = true;

        for (U8 i = 0; i < 2; ++i) {
            TransformComponent* flagtComp = _flag[i]->get<TransformComponent>();
            WAIT_FOR_CONDITION(!flagtComp->popTransforms());
            _flag[i]->setParent(_sceneGraph->getRoot());
            flagtComp->setPosition(vec3<F32>(25.0f, 0.1f, i == 0 ? -206.0f : 206.0f));
        }
        AI::WarSceneAIProcessor::reset();
        AI::WarSceneAIProcessor::incrementScore(teamID);
        AI::WarSceneAIProcessor::registerFlags(_flag[0], _flag[1]);

        U32 elapsedTimeMinutes = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) / 60 % 60;
        U32 elapsedTimeSeconds = Time::MicrosecondsToSeconds<U32>(_elapsedGameTime) % 60;
        Console::d_printfn(Util::StringFormat("[GAME TIME: %d:%d][TEAM %s REGISTER POINT]: %s", elapsedTimeMinutes, elapsedTimeSeconds, teamID == 0 ? "A" : "B", unitName.c_str()).c_str());
        checkGameCompletion();
    }
}

bool WarScene::initializeAI(bool /*continueOnErrors*/) {
    //------------------------Artificial Intelligence------------------------//
    // Create 2 AI teams
    for (U8 i = 0; i < 2; ++i) {
        _faction[i] = MemoryManager_NEW AI::AITeam(i, *_aiManager);
    }
    // Make the teams fight each other
    _faction[0]->addEnemyTeam(_faction[1]->getTeamID());
    _faction[1]->addEnemyTeam(_faction[0]->getTeamID());

    //if (addUnits()) 
    {
        //_sceneGraph->findNode("Soldier1")->setActive(false);
        //_sceneGraph->findNode("Soldier2")->setActive(false);
        //_sceneGraph->findNode("Soldier3")->setActive(false);
        return true;
    }

    //return false;
}

bool WarScene::removeUnits() {
    WAIT_FOR_CONDITION(!_aiManager->updating());
    for (U8 i = 0; i < 2; ++i) {
        for (SceneGraphNode* npc : _armyNPCs[i]) {
            _aiManager->unregisterEntity(npc->get<UnitComponent>()->getUnit<NPC>()->getAIEntity());
            _sceneGraph->removeNode(npc);
        }
        _armyNPCs[i].clear();
    }

    return true;
}

bool WarScene::addUnits() {
    using namespace AI;

    // Go near the enemy flag.
    // The enemy flag will never be at the home base, because that is a scoring condition
    WarSceneAction approachEnemyFlag(ActionType::APPROACH_ENEMY_FLAG, "ApproachEnemyFlag");
    approachEnemyFlag.setPrecondition(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(false));
    approachEnemyFlag.setEffect(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(true));
    approachEnemyFlag.setEffect(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(false));

    WarSceneAction protectFlagInBase(ActionType::APPROACH_ENEMY_FLAG, "ProtectFlagInBase");
    protectFlagInBase.setPrecondition(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(false));
    protectFlagInBase.setPrecondition(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));
    protectFlagInBase.setEffect(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(true));

    // Grab the enemy flag only if we do not already have it and if we are near it
    // The enemy flag will never be at the home base, because that is a scoring condition
    WarSceneAction captureEnemyFlag(ActionType::CAPTURE_ENEMY_FLAG, "CaptureEnemyFlag");
    captureEnemyFlag.setPrecondition(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(true));
    captureEnemyFlag.setPrecondition(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(false));
    captureEnemyFlag.setPrecondition(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(false));
    captureEnemyFlag.setEffect(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(true));

    // Kill the enemy flag carrier to recover our own flag and return it to base
    WarSceneAction recoverFlag(ActionType::RECOVER_FLAG, "RecoverFlag");
    recoverFlag.setPrecondition(GOAPFact(Fact::ENEMY_HAS_FLAG), GOAPValue(true));
    recoverFlag.setPrecondition(GOAPFact(Fact::ENEMY_DEAD), GOAPValue(true));
    recoverFlag.setEffect(GOAPFact(Fact::ENEMY_HAS_FLAG), GOAPValue(false));

    // A general purpose "go home" action
    WarSceneAction returnToBase(ActionType::RETURN_TO_BASE, "ReturnToBase");
    returnToBase.setPrecondition(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(false));
    returnToBase.setEffect(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));

    // We can only score if we and both flags are in the same base location
    WarSceneAction scoreEnemyFlag(ActionType::SCORE_FLAG, "ScoreEnemyFlag");
    scoreEnemyFlag.setPrecondition(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));
    scoreEnemyFlag.setPrecondition(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(true));
    scoreEnemyFlag.setPrecondition(GOAPFact(Fact::ENEMY_HAS_FLAG), GOAPValue(false));
    scoreEnemyFlag.setEffect(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(false));

    // Do nothing, but do nothing at home
    WarSceneAction idleAction(ActionType::IDLE, "Idle");
    idleAction.setPrecondition(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));
    idleAction.setEffect(GOAPFact(Fact::IDLING), GOAPFact(true));

    // Kill stuff in lack of a better occupation
    WarSceneAction attackAction(ActionType::ATTACK_ENEMY, "Attack");
    attackAction.setPrecondition(GOAPFact(Fact::ENEMY_DEAD), GOAPValue(false));
    attackAction.setEffect(GOAPFact(Fact::ENEMY_DEAD), GOAPValue(true));

    GOAPGoal recoverOwnFlag("Recover flag", to_base(WarSceneOrder::WarOrder::RECOVER_FLAG));
    recoverOwnFlag.setVariable(GOAPFact(Fact::ENEMY_HAS_FLAG), GOAPValue(false));
    
    GOAPGoal captureFlag("Capture enemy flag", to_base(WarSceneOrder::WarOrder::CAPTURE_ENEMY_FLAG));
    captureFlag.setVariable(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(true));
    captureFlag.setVariable(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));

    GOAPGoal scoreFlag("Score", to_base(WarSceneOrder::WarOrder::SCORE_ENEMY_FLAG));
    scoreFlag.setVariable(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(false));

    GOAPGoal idle("Idle", to_base(WarSceneOrder::WarOrder::IDLE));
    idle.setVariable(GOAPFact(Fact::IDLING), GOAPValue(true));

    GOAPGoal killEnemy("Kill", to_base(WarSceneOrder::WarOrder::KILL_ENEMY));
    killEnemy.setVariable(GOAPFact(Fact::ENEMY_DEAD), GOAPValue(true));

    GOAPGoal protectFlagCarrier("Protect Flag Carrier", to_base(WarSceneOrder::WarOrder::PROTECT_FLAG_CARRIER));
    protectFlagCarrier.setVariable(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(true));

    std::array<GOAPPackage,  to_base(WarSceneAIProcessor::AIType::COUNT)> goapPackages;
    for (GOAPPackage& goapPackage : goapPackages) {
        goapPackage._worldState.setVariable(GOAPFact(Fact::AT_HOME_BASE), GOAPValue(true));
        goapPackage._worldState.setVariable(GOAPFact(Fact::ENEMY_DEAD), GOAPValue(false));
        goapPackage._worldState.setVariable(GOAPFact(Fact::ENEMY_HAS_FLAG), GOAPValue(false));
        goapPackage._worldState.setVariable(GOAPFact(Fact::HAS_ENEMY_FLAG), GOAPValue(false));
        goapPackage._worldState.setVariable(GOAPFact(Fact::IDLING), GOAPValue(true));
        goapPackage._worldState.setVariable(GOAPFact(Fact::NEAR_ENEMY_FLAG), GOAPValue(false));

        goapPackage._actionSet.push_back(idleAction);
        goapPackage._actionSet.push_back(attackAction);
        goapPackage._actionSet.push_back(recoverFlag);
        goapPackage._actionSet.push_back(protectFlagInBase);
        goapPackage._goalList.push_back(idle);
        goapPackage._goalList.push_back(killEnemy);
        goapPackage._goalList.push_back(recoverOwnFlag);
    }

    GOAPPackage& lightPackage = goapPackages[to_base(WarSceneAIProcessor::AIType::LIGHT)];
    GOAPPackage& heavyPackage = goapPackages[to_base(WarSceneAIProcessor::AIType::HEAVY)];

    heavyPackage._actionSet.push_back(approachEnemyFlag);
    heavyPackage._actionSet.push_back(captureEnemyFlag);
    heavyPackage._actionSet.push_back(returnToBase);
    heavyPackage._actionSet.push_back(scoreEnemyFlag);
    lightPackage._actionSet.push_back(approachEnemyFlag);

    heavyPackage._goalList.push_back(captureFlag);
    heavyPackage._goalList.push_back(scoreFlag);
    heavyPackage._goalList.push_back(protectFlagCarrier);
    lightPackage._goalList.push_back(protectFlagCarrier);
#if 0
    SceneGraphNode* lightNode(_sceneGraph->findNode("Soldier1"));
    SceneGraphNode* animalNode(_sceneGraph->findNode("Soldier2"));
    SceneGraphNode* heavyNode(_sceneGraph->findNode("Soldier3"));

    SceneNode_ptr lightNodeMesh = lightNode->getNode();
    SceneNode_ptr animalNodeMesh = animalNode->getNode();
    SceneNode_ptr heavyNodeMesh = heavyNode->getNode();
    assert(lightNodeMesh && animalNodeMesh && heavyNodeMesh);

    AIEntity* aiSoldier = nullptr;
    SceneNode_ptr currentMesh;

    vec3<F32> currentScale;
    string currentName;

    constexpr U32 normalMask = to_base(ComponentType::NAVIGATION) |
                                to_base(ComponentType::TRANSFORM) |
                                to_base(ComponentType::RIGID_BODY) |
                                to_base(ComponentType::BOUNDS) |
                                to_base(ComponentType::RENDERING) |
                                to_base(ComponentType::UNIT) |
                                to_base(ComponentType::NETWORKING);

    SceneGraphNode* root = _sceneGraph->getRoot();
    for (I32 k = 0; k < 2; ++k) {
        for (I32 i = 0; i < 15; ++i) {

            F32 speed = Metric::Base(-1.0f);
            F32 acc = Metric::Base(-1.0f);

            F32 zFactor = 0.0f;
            I32 damage = 5;
            AI::WarSceneAIProcessor::AIType type;
            if (IS_IN_RANGE_INCLUSIVE(i, 0, 4)) {
                currentMesh = lightNodeMesh;
                currentScale =
                    lightNode->get<TransformComponent>()->getScale();
                currentName = Util::StringFormat("Soldier_1_%d_%d", k, i);
                speed = Metric::Base(Random(6.5f, 9.5f));
                acc = Metric::Base(Random(4.5f, 8.0f));
                type = AI::WarSceneAIProcessor::AIType::LIGHT;
            } else if (IS_IN_RANGE_INCLUSIVE(i, 5, 9)) {
                currentMesh = animalNodeMesh;
                currentScale =
                    animalNode->get<TransformComponent>()->getScale();
                currentName = Util::StringFormat("Soldier_2_%d_%d", k, i % 5);
                speed = Metric::Base(Random(8.5f, 11.5f));
                acc = Metric::Base(Random(6.0f, 9.0f));
                zFactor = 1.0f;
                type = AI::WarSceneAIProcessor::AIType::ANIMAL;
                damage = 10;
            } else {
                currentMesh = heavyNodeMesh;
                currentScale =
                    heavyNode->get<TransformComponent>()->getScale();
                currentName = Util::StringFormat("Soldier_3_%d_%d", k, i % 10);
                speed = Metric::Base(Random(4.5f, 7.5f));
                acc = Metric::Base(Random(4.0f, 6.5f));
                zFactor = 2.0f;
                type = AI::WarSceneAIProcessor::AIType::HEAVY;
                damage = 15;
            }

            SceneGraphNodeDescriptor npcNodeDescriptor;
            npcNodeDescriptor._serialize = false;
            npcNodeDescriptor._node = currentMesh;
            npcNodeDescriptor._usageContext = NodeUsageContext::NODE_DYNAMIC;
            npcNodeDescriptor._componentMask = normalMask | to_base(ComponentType::SELECTION);
            npcNodeDescriptor._name = currentName;

            SceneGraphNode* currentNode = root.addNode(npcNodeDescriptor);
            currentNode->setSelectable(true);

            currentNode->get<RigidBodyComponent>()->physicsGroup(PhysicsGroup::GROUP_KINEMATIC);

            TransformComponent* tComp =
                currentNode->get<TransformComponent>();
            tComp->setScale(currentScale);

            if (k == 0) {
                zFactor *= -25;
                zFactor -= 200;
                tComp->translateX(Metric::Base(-25.0f));
            } else {
                zFactor *= 25;
                zFactor += 200;
                tComp->rotateY(Angle::DEGREES<F32>(180.0f));
                tComp->translateX(Metric::Base(100.0f));
                tComp->translateX(Metric::Base(25.0f));
            }

            tComp->setPosition(vec3<F32>(Metric::Base(-125.0f + 25 * (i % 5)),
                                         Metric::Base(-0.01f),
                                         Metric::Base(zFactor)));

            aiSoldier = MemoryManager_NEW AI::AIEntity(tComp->getPosition(),
                                                       currentNode->name());
            aiSoldier->addSensor(AI::SensorType::VISUAL_SENSOR);

            AI::WarSceneAIProcessor* brain = MemoryManager_NEW AI::WarSceneAIProcessor(type, *_aiManager);

            // GOAP
            brain->registerGOAPPackage(goapPackages[to_U32(type)]);
            aiSoldier->setAIProcessor(brain);
            std::shared_ptr<NPC> soldier = std::make_shared<NPC>(aiSoldier, context().kernel().frameListenerMgr(), 1988);
            soldier->setAttribute(to_base(AI::UnitAttributes::HEALTH_POINTS), 100);
            soldier->setAttribute(to_base(AI::UnitAttributes::DAMAGE), damage);
            soldier->setAttribute(to_base(AI::UnitAttributes::ALIVE_FLAG), 1);
            soldier->setMovementSpeed(speed);
            soldier->setAcceleration(acc);
            currentNode->get<UnitComponent>()->setUnit(soldier);
            _armyNPCs[k].push_back(currentNode);
            _aiManager->registerEntity(k, aiSoldier);
            
        }
    }

    //----------------------- AI controlled units ---------------------//
    return !(_armyNPCs[0].empty() || _armyNPCs[1].empty());
#endif
    return true;
}

AI::AIEntity* WarScene::findAI(SceneGraphNode* node) {
    const I64 targetGUID = node->getGUID();

    for (U8 i = 0; i < 2; ++i) {
        for (SceneGraphNode* npc : _armyNPCs[i]) {
            if (npc->getGUID() == targetGUID) {
                return npc->get<UnitComponent>()->getUnit<NPC>()->getAIEntity();
            }
        }
    }

    return nullptr;
}

bool WarScene::resetUnits() {
    _aiManager->pauseUpdate(true);
    bool state = false;
    if (removeUnits()) {
       state = addUnits();
    }
    _aiManager->pauseUpdate(false);
    return state;
}

bool WarScene::deinitializeAI(bool /*continueOnErrors*/) {
    _aiManager->pauseUpdate(true);
    if (removeUnits()) {
        for (U8 i = 0; i < 2; ++i) {
            MemoryManager::DELETE(_faction[i]);
        }
        return true;
    }

    return false;
}

void WarScene::startSimulation(I64 /*btnGUID*/) {
    if (g_navMeshStarted || _armyNPCs[0].empty()) {
        return;
    }

    g_navMeshStarted = true;

    _aiManager->pauseUpdate(true);
    _infoBox->setTitle("NavMesh state");
    _infoBox->setMessageType(GUIMessageBox::MessageType::MESSAGE_INFO);
    bool previousMesh = false;
    bool loadedFromFile = true;
    const U64 currentTime = Time::Game::ElapsedMicroseconds();
    const U64 diffTime = currentTime - _lastNavMeshBuildTime;

    AI::AIEntity* aiEntity = _armyNPCs[0][0]->get<UnitComponent>()->getUnit<NPC>()->getAIEntity();
    if (_lastNavMeshBuildTime == 0UL ||
        diffTime > Time::SecondsToMicroseconds(10)) {
        AI::Navigation::NavigationMesh* navMesh = 
            _aiManager->getNavMesh(aiEntity->getAgentRadiusCategory());
        if (navMesh) {
            previousMesh = true;
            _aiManager->destroyNavMesh(aiEntity->getAgentRadiusCategory());
        }
        navMesh = MemoryManager_NEW AI::Navigation::NavigationMesh(_context, *_parent.recast());
        navMesh->setFileName(resourceName());

        if (!navMesh->load(_sceneGraph->getRoot())) {
            loadedFromFile = false;
            AI::AIEntity::PresetAgentRadius radius = aiEntity->getAgentRadiusCategory();
            navMesh->build(
                _sceneGraph->getRoot(),
                [&radius, this](AI::Navigation::NavigationMesh* mesh) {
                _aiManager->toggleNavMeshDebugDraw(true);
                _aiManager->addNavMesh(radius, mesh);
                g_navMeshStarted = false;
            });
        } else {
            _aiManager->addNavMesh(aiEntity->getAgentRadiusCategory(), navMesh);
            if_constexpr(Config::Build::IS_DEBUG_BUILD) {
                navMesh->debugDraw(true);
            }
        }

        if (previousMesh) {
            if (loadedFromFile) {
                _infoBox->setMessage(
                    "Re-loaded the navigation mesh from file!");
            }
            else {
                _infoBox->setMessage(
                    "Re-building the navigation mesh in a background thread!");
            }
        }
        else {
            if (loadedFromFile) {
                _infoBox->setMessage("Navigation mesh loaded from file!");
            }
            else {
                _infoBox->setMessage(
                    "Navigation mesh building in a background thread!");
            }
        }
        //_infoBox->show();
        _lastNavMeshBuildTime = currentTime;
        for (U8 i = 0; i < 2; ++i) {
            _faction[i]->clearOrders();
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::CAPTURE_ENEMY_FLAG));
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::SCORE_ENEMY_FLAG));
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::IDLE));
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::KILL_ENEMY));
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::PROTECT_FLAG_CARRIER));
            _faction[i]->addOrder(std::make_shared<AI::WarSceneOrder>(
                AI::WarSceneOrder::WarOrder::RECOVER_FLAG));
        }
    } else {
        string info(
            "Can't reload the navigation mesh this soon.\n Please wait \\[ ");
        info.append(
            Util::to_string(Time::MicrosecondsToSeconds<I32>(diffTime)));
        info.append(" ] seconds more!");

        _infoBox->setMessage(info);
        _infoBox->setMessageType(GUIMessageBox::MessageType::MESSAGE_WARNING);
        _infoBox->show();
    }

    _aiManager->pauseUpdate(false);
}

}  // namespace Divide
