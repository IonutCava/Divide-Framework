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
#ifndef DVD_WAR_SCENE_AI_ACTION_LIST_H_
#define DVD_WAR_SCENE_AI_ACTION_LIST_H_

#include "AI/ActionInterface/Headers/AITeam.h"
#include "AI/ActionInterface/Headers/AIProcessor.h"
#include "Scenes/WarScene/AESOPActions/Headers/WarSceneActions.h"

namespace Divide {
class Unit;

namespace AI {

enum class UnitAttributes : U8 {
    HEALTH_POINTS = 0,
    DAMAGE = 1,
    ALIVE_FLAG = 2,
    COUNT
};

enum class AIMsg : U8 {
    HAVE_FLAG = 0,
    RETURNED_FLAG = 1,
    ENEMY_HAS_FLAG = 2,
    HAVE_SCORED = 3,
    ATTACK = 4,
    HAVE_DIED = 5,
    COUNT
};

enum class FactType : U8 {
    POSITION = 0,
    COUNTER_SMALL = 1,
    COUNTER_MEDIUM = 2,
    COUNTER_LARGE = 3,
    TOGGLE_STATE = 4,
    AI_NODE = 5,
    SGN_NODE = 6,
    COUNT
};

template <typename T, FactType F>
class WorkingMemoryFact {
   public:
    WorkingMemoryFact()
    {
        _value = T();
        _type = F;
        _belief = 0.0f;
    }

    void value(const T& val) {
        _value = val;
        belief(1.0f);
    }

    void belief(const F32 belief) { _belief = belief; }

    [[nodiscard]] const T& value() const noexcept { return _value; }
    [[nodiscard]] FactType type() const noexcept { return _type; }
    [[nodiscard]] F32 belief() const noexcept { return _belief; }

   protected:
    T _value;
    F32 _belief;
    FactType _type;
};

using AINodeFact = WorkingMemoryFact<AIEntity*, FactType::AI_NODE>;
using SGNNodeFact = WorkingMemoryFact<SceneGraphNode*, FactType::SGN_NODE>;
using PositionFact = WorkingMemoryFact<float3, FactType::POSITION>;
using SmallCounterFact = WorkingMemoryFact<U8, FactType::COUNTER_SMALL>;
using MediumCounterFact = WorkingMemoryFact<U16, FactType::COUNTER_MEDIUM>;
using LargeCounterFact = WorkingMemoryFact<U32, FactType::COUNTER_LARGE>;
using ToggleStateFact = WorkingMemoryFact<bool, FactType::TOGGLE_STATE>;

class GlobalWorkingMemory {
public:
    GlobalWorkingMemory()
    {
        _flags[0].value(nullptr);
        _flags[1].value(nullptr);
        _flagCarriers[0].value(nullptr);
        _flagCarriers[1].value(nullptr);
        _flagsAtBase[0].value(true);
        _flagsAtBase[1].value(true);
    }

    SGNNodeFact _flags[2];
    AINodeFact  _flagCarriers[2];
    SmallCounterFact _score[2];
    SmallCounterFact _teamAliveCount[2];
    SmallCounterFact _flagRetrieverCount[2];
    PositionFact _teamFlagPosition[2];
    ToggleStateFact _flagsAtBase[2];
};

class LocalWorkingMemory {
   public:
    LocalWorkingMemory()
    {
        _hasEnemyFlag.value(false);
        _enemyHasFlag.value(false);
       _isFlagRetriever.value(false);
       _currentTarget.value(nullptr);
    }

    SGNNodeFact _currentTarget;
    ToggleStateFact _hasEnemyFlag;
    ToggleStateFact _enemyHasFlag;
    ToggleStateFact _isFlagRetriever;
};

class WarSceneOrder final : public Order {
   public:
    enum class WarOrder : U8 {
        IDLE = 0,
        CAPTURE_ENEMY_FLAG = 1,
        SCORE_ENEMY_FLAG = 2,
        KILL_ENEMY = 3,
        PROTECT_FLAG_CARRIER = 4,
        RECOVER_FLAG = 5,
        COUNT
    };

    WarSceneOrder(const WarOrder order) : Order(to_U32(order))
    {
    }


    void lock() override { Order::lock(); }
    void unlock() override { Order::unlock(); }
};

struct GOAPPackage {
    GOAPWorldState _worldState;
    GOAPGoalList _goalList;
    vector<WarSceneAction> _actionSet;
};

namespace Attorney {
    class WarAISceneWarAction;
}

class AudioSensor;
class VisualSensor;
class WarSceneAIProcessor final : public AIProcessor {
    friend class Attorney::WarAISceneWarAction;
   public:
       using NodeToUnitMap = hashMap<I64, AIEntity*>;
   public:
       enum class AIType {
           ANIMAL = 0,
           LIGHT = 1,
           HEAVY = 2,
           COUNT
       };
    WarSceneAIProcessor(AIType type, AIManager& parentManager);
    ~WarSceneAIProcessor() override;

    void registerGOAPPackage(const GOAPPackage& package);

    bool processData(U64 deltaTimeUS) override;
    bool processInput(U64 deltaTimeUS) override;
    bool update(U64 deltaTimeUS, NPC* unitRef = nullptr) override;
    void processMessage(AIEntity& sender, AIMsg msg, const std::any& msg_content) override;

    static void registerFlags(SceneGraphNode* flag1, SceneGraphNode* flag2) {
        _globalWorkingMemory._flags[0].value(flag1);
        _globalWorkingMemory._flags[1].value(flag2);
    }

    static void registerScoreCallback(const DELEGATE<void, U8, const string&>& cbk) {
        _scoreCallback = cbk;
    }

    static void registerMessageCallback(const DELEGATE<void, U8, const string&>& cbk) {
        _messageCallback = cbk;
    }

    static U8 getScore(const U16 teamID) {
        return _globalWorkingMemory._score[teamID].value();
    }

    static void reset();
    static void incrementScore(const U16 teamID) {
        _globalWorkingMemory._score[teamID].value(getScore(teamID) + 1);
    }

    static void resetScore(const U8 teamID) {
        _globalWorkingMemory._score[teamID].value(0);
    }
   protected:
    bool preAction(ActionType type, const WarSceneAction* warAction);
    bool postAction(ActionType type, const WarSceneAction* warAction);
    bool checkCurrentActionComplete(const GOAPAction& planStep);
    void invalidateCurrentPlan() override;
    string toString(bool state = false) const override;

   private:
    bool DIE();
    void requestOrders();
    void updatePositions();
    bool performAction(const GOAPAction& planStep) override;
    bool performActionStep(GOAPAction::operationsIterator step) override;
    const char* printActionStats(const GOAPAction& planStep) const override;
    void printWorkingMemory() const;
    void initInternal() override;
    void beginPlan(const GOAPGoal& currentGoal);
    AIEntity* getUnitForNode(U32 teamID, SceneGraphNode* node) const;

    template <typename... T>
    static void PRINT(const char* format, T&&... args);

    bool atHomeBase() const;
    bool nearOwnFlag() const;
    bool nearEnemyFlag() const;

  private:
    AIType _type;
    U64 _deltaTime = 0ULL;
    U8 _visualSensorUpdateCounter = 0;
    U64 _attackTimer = 0ULL;
    string _planStatus = "None";
    string _tempString;
    VisualSensor* _visualSensor = nullptr;
    AudioSensor* _audioSensor = nullptr;
    LocalWorkingMemory _localWorkingMemory;
    /// Keep this in memory at this level
    vector<WarSceneAction> _actionList;
    NodeToUnitMap _nodeToUnitMap[2];
    std::array<bool, to_base(ActionType::COUNT)> _actionState;
    static DELEGATE<void, U8, const string&> _scoreCallback;
    static DELEGATE<void, U8, const string&> _messageCallback;
    static GlobalWorkingMemory _globalWorkingMemory;
    static float3 _initialFlagPositions[2];

#if defined(PRINT_AI_TO_FILE)
    mutable std::ofstream _WarAIOutputStream;
#endif
};

template <typename... Args>
void WarSceneAIProcessor::PRINT([[maybe_unused]] const char* format, [[maybe_unused]] Args&&... args) {
#if defined(PRINT_AI_TO_FILE)
    Console::d_printfn(_WarAIOutputStream, format, FWD(args)...);
#endif //PRINT_AI_TO_FILE
}

namespace Attorney {
class WarAISceneWarAction {
   private:
    static bool preAction(WarSceneAIProcessor& aiProcessor,
                          const ActionType type,
                          const WarSceneAction* warAction) {
        return aiProcessor.preAction(type, warAction);
    }
    static bool postAction(WarSceneAIProcessor& aiProcessor,
                           const ActionType type,
                           const WarSceneAction* warAction) {
        return aiProcessor.postAction(type, warAction);
    }

    friend class AI::WarSceneAction;
};

}  // namespace Attorney
}  // namespace AI
}  // namespace Divide

#endif //DVD_WAR_SCENE_AI_ACTION_LIST_H_
