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
#ifndef DVD_AI_ENTITY_H_
#define DVD_AI_ENTITY_H_

#include "AI/Sensors/Headers/VisualSensor.h"
#include <any>

struct dtCrowdAgent;

namespace Divide {

class SceneGraphNode;

class NPC;
namespace AI {
class AITeam;
class AIProcessor;
class Order;
enum class AIMsg : U8; ///< scene dependent message list

namespace Navigation {
    class DivideRecast;
    class DivideDtCrowd;
}

namespace Attorney {
    class AIEntityAITeam;
}

/// Based on OgreCrowd.
class AIEntity final : public GUIDWrapper {
    friend class Attorney::AIEntityAITeam;
   public:
    enum class PresetAgentRadius : U8 {
        AGENT_RADIUS_SMALL = 0,
        AGENT_RADIUS_MEDIUM = 1,
        AGENT_RADIUS_LARGE = 2,
        AGENT_RADIUS_EXTRA_LARGE = 3,
        COUNT
    };

    AIEntity( NPC* parent, const float3& currentPosition, std::string_view name);
    ~AIEntity() override;

    void load( const float3& currentPosition );
    void unload();

    [[nodiscard]] bool addSensor(SensorType type);
    [[nodiscard]] bool setAndSurrenderAIProcessor(AIProcessor* processor);

    void sendMessage(AIEntity& receiver,  AIMsg msg, const std::any& msg_content);
    void receiveMessage(AIEntity& sender, AIMsg msg, const std::any& msg_content);
    void processMessage(AIEntity& sender, AIMsg msg, const std::any& msg_content);

    [[nodiscard]] Sensor* getSensor(SensorType type);

    [[nodiscard]] AITeam* getTeam() const noexcept { return _teamPtr; }
    [[nodiscard]] U32 getTeamID() const;
    [[nodiscard]] const string& name() const noexcept { return _name; }

    [[nodiscard]] NPC* getUnitRef() const noexcept { return _unitRef; }

    /// PathFinding
    /// Index ID identifying the agent of this character in the crowd
    [[nodiscard]] I32 getAgentID() const noexcept { return _agentID; }
    /// The agent that steers this character within the crowd
    [[nodiscard]] const dtCrowdAgent* getAgent() const noexcept { return _agent; }
    [[nodiscard]] bool isAgentLoaded() const noexcept { return _agentID >= 0; }
    /// Update the crowding system
    void resetCrowd();
    /// The height of the agent for this character.
    [[nodiscard]] F32 getAgentHeight() const noexcept;
    /// The radius of the agent for this character.
    [[nodiscard]] F32 getAgentRadius() const noexcept;
    /// The radius category of this character
    [[nodiscard]] PresetAgentRadius getAgentRadiusCategory() const noexcept { return _agentRadiusCategory; }
    /**
      * Update the destination for this agent.
      * If updatePreviousPath is set to true the previous path will be reused instead
      * of calculating a completely new path, but this can only be used if the new
      * destination is close to the previous (eg. when chasing a moving entity).
     **/
    [[nodiscard]] bool updateDestination(const float3& destination, bool updatePreviousPath = false);
    /// The destination set for this agent.
    [[nodiscard]] const float3& getDestination() const noexcept;
    /// Returns true when this agent has reached its set destination.
    [[nodiscard]] bool destinationReached() const;
    /// Place agent at new position.
    [[nodiscard]] bool setPosition(const float3& position);
    /// The current position of the agent.
    /// Is only up to date once update() has been called in a frame.
    [[nodiscard]] const float3& getPosition() const noexcept;
    /// The maximum speed this character can attain.
    /// This parameter is configured for the agent controlling this character.
    [[nodiscard]] F32 getMaxSpeed() const noexcept;
    /// The maximum acceleration this character has towards its maximum speed.
    /// This parameter is configured for the agent controlling this character.
    [[nodiscard]] F32 getMaxAcceleration() const noexcept;
    /**
     * Request to set a manual velocity for this character, to control it
     * manually.
     * The set velocity will stay active, meaning that the character will
     * keep moving in the set direction, until you stop() it.
     * Manually controlling a character offers no absolute control as the
     * laws of acceleration and max speed still apply to an agent, as well
     * as the fact that it cannot steer off the navmesh or into other agents.
     * You will notice small corrections to steering when walking into objects
     * or the border of the navmesh (which is usually a wall or object).
    **/
    void setVelocity(const float3& velocity);
    /// Manually control the character moving it forward.
    void moveForward();
    /// Manually control the character moving it backwards.
    void moveBackwards();
    /// Stop any movement this character is currently doing.
    /// This means losing the requested velocity or target destination.
    void stop();
    /// The current velocity (speed and direction) this character is traveling
    /// at.
    [[nodiscard]] float3 getVelocity() const noexcept;
    /// The current speed this character is traveling at.
    [[nodiscard]] F32 getSpeed() const noexcept { return getVelocity().length(); }
    /// Returns true if this character is moving.
    [[nodiscard]] bool isMoving() const noexcept { return !_stopped || !IS_ZERO(getSpeed()); }

    [[nodiscard]] string toString() const;

   protected:
    /**
     * Update current position of this character to the current position of its agent.
    **/
    void updatePosition(U64 deltaTimeUS);
    /**
     * Set destination member variable directly without updating the agent state.
     * Usually you should call updateDestination() externally, unless you are controlling
     * the agents directly and need to update the corresponding character class to reflect
     * the change in state (see OgreRecastApplication friendship).
    **/
    void setDestination(const float3& destination) noexcept;

    void setTeamPtr(AITeam* teamPtr);
    [[nodiscard]] bool processInput(U64 deltaTimeUS);
    [[nodiscard]] bool processData(U64 deltaTimeUS);
    [[nodiscard]] bool update(U64 deltaTimeUS);

   private:
    string _name;
    AITeam* _teamPtr;
    std::unique_ptr<AIProcessor> _processor;

    mutable SharedMutex _updateMutex;
    mutable SharedMutex _managerQueryMutex;

    using SensorMap = hashMap<SensorType, std::unique_ptr<Sensor>>;
    SensorMap _sensorList;
    NPC* _unitRef;
    /// PathFinding
    /// ID of mAgent within the crowd.
    I32 _agentID;
    /// Crowd in which the agent of this character is.
    Navigation::DivideDtCrowd* _detourCrowd;
    /// The agent controlling this character.
    const dtCrowdAgent* _agent;
    PresetAgentRadius _agentRadiusCategory;

    /**
     * The current destination set for this agent.
     * Take care in properly setting this variable, as it is only updated properly when
     * using Character::updateDestination() to set an individual destination for this character.
     * After updating the destination of all agents this variable should be set externally using  setDestination().
    **/
    float3 _destination;
    float3 _currentPosition;
    float3 _currentVelocity;
    F32 _distanceToTarget;
    F32 _previousDistanceToTarget;
    U64 _moveWaitTimer;
    /// True if this character is stopped.
    bool _stopped;
};

namespace Attorney {
class AIEntityAITeam {
    static void setTeamPtr(AIEntity& entity, AITeam* const teamPtr) {
        entity.setTeamPtr(teamPtr);
    }
    static bool processInput(AIEntity& entity, const U64 deltaTimeUS) {
        return entity.processInput(deltaTimeUS);
    }
    static bool processData(AIEntity& entity, const U64 deltaTimeUS) {
        return entity.processData(deltaTimeUS);
    }
    static bool update(AIEntity& entity, const U64 deltaTimeUS) {
        return entity.update(deltaTimeUS);
    }
    friend class AI::AITeam;
};
}  // namespace Attorney
}  // namespace AI
}  // namespace Divide

#endif //DVD_AI_ENTITY_H_
