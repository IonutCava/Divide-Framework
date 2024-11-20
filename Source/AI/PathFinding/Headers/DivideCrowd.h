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

/*
    OgreCrowd
    ---------

    Copyright (c) 2012 Jonas Hauquier

    Additional contributions by:

    - mkultra333
    - Paul Wilson

    Sincere thanks and to:

    - Mikko Mononen (developer of Recast navigation libraries)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

*/

#pragma once
#ifndef DVD_CROWD_H_
#define DVD_CROWD_H_

#include "recastnavigation/DetourCrowd.h"
#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"

/**
  * Divide wrapper around Ogre wrapper around DetourCrowd.
  * Controls a crowd of agents that can steer to avoid each other and follow
  * individual paths.
  *
  * This class is largely based on the CrowdTool used in the original
  *RecastNavigation
  * demo.
  **/

namespace Divide {
namespace AI {
namespace Navigation {

class NavigationMesh;

class DivideDtCrowd final : public NonCopyable {
   public:
    /**
      * Initialize a detour crowd that will manage agents on the specified
      * recast navmesh. It does not matter how this navmesh is constructed
      * (either with OgreRecast directly or with DetourTileCache).
      * Parameters such as agent dimensions will be taken from the specified
      * recast component.
      **/
    DivideDtCrowd(NavigationMesh* navMesh);
    ~DivideDtCrowd();
    /// Add an agent to the crowd: Returns ID of created agent
    /// (-1 if maximum agents is already created)
    [[nodiscard]] I32 addAgent(const float3& position, F32 maxSpeed, F32 acceleration);
    /// Retrieve agent with specified ID from the crowd.
    [[nodiscard]] const dtCrowdAgent* getAgent(const I32 id) const { return _crowd->getAgent(id); }
    /// Remove agent with specified ID from the crowd.
    void removeAgent(I32 idx);
    /**
      * Set global destination or target for all agents in the crowd.
      * Setting adjust to true will try to adjust the current calculated path
      * of the agents slightly to end at the new destination, avoiding the need
      * to calculate a completely new path. This only works if the destination is
      * close to the previously set one, for example when chasing a moving entity.
      **/
    void setMoveTarget(const float3& position, bool adjust);
    /**
      * Set target or destination for an individual agent.
      * Setting adjust to true will try to adjust the current calculated path
      * of the agent slightly to end at the new destination, avoiding the need
      * to calculate a completely new path. This only works if the destination is
      * close to the previously set one, for example when chasing a moving entity.
      **/
    void setMoveTarget(I32 agentID, const float3& position, bool adjust);
    /**
      * Request a specified velocity for the agent with specified index.
      * Requesting a velocity means manually controlling an agent.
      * Returns true if the request was successful.
      **/
    [[nodiscard]] bool requestVelocity(I32 agentID, const float3& velocity) const;
    /// Cancels any request for the specified agent, making it stop.
    /// Returns true if the request was successul.
    [[nodiscard]] bool stopAgent(I32 agentID) const;
    /**
      * Helper that calculates the needed velocity to steer an agent to a target destination.
      * Parameters:
      *     position    is the current position of the agent
      *     target      is the target destination to reach
      *     speed       is the (max) speed the agent can travel at
      * Returns the calculated velocity.
      *
      * This function can be used together with requestMoveVelocity to achieve the functionality
      * of adjustMoveTarget function.
      **/
    [[nodiscard]] static float3 calcVel(const float3& position, const float3& target, D64 speed);
    [[nodiscard]] static F32 getDistanceToGoal(const dtCrowdAgent* agent, F32 maxRange);
    [[nodiscard]] static bool destinationReached(const dtCrowdAgent* agent, F32 maxDistanceFromTarget);
    /**
      * Update method for the crowd manager. Will calculate new positions for moving agents.
      * Call this method in your frameloop every frame to make your agents move.
      *
      * DetourCrowd uses sampling based local steering to calculate a velocity vector for each
      * agent. The calculation time of this is limited to the number of agents in the crowd and
      * the sampling amount (which is a constant).
      *
      * Additionally pathfinding tasks are queued and the number of computations is limited, to
      * limit the maximum amount of time spent for preparing a frame. This can have as consequence
      * that some agents will only start to move after a few frames, when their paths are calculated.
      **/
    void update(U64 deltaTimeUS);
    /// The height of agents in this crowd. All agents in a crowd have the same
    /// height, and height is
    /// determined by the agent height parameter with which the navmesh is
    /// build.
    [[nodiscard]] F32 getAgentHeight() const noexcept { return Attorney::NavigationMeshCrowd::getConfigParams(*_recast).getAgentHeight(); }
    /// The radius of agents in this crowd. All agents in a crowd have the same
    /// radius, and radius
    /// determined by the agent radius parameter with which the navmesh is
    /// build.
    [[nodiscard]] F32 getAgentRadius() const  noexcept { return Attorney::NavigationMeshCrowd::getConfigParams(*_recast).getAgentRadius(); }
    /// The number of (active) agents in this crowd.
    [[nodiscard]] I32 getNbAgents() const noexcept { return _activeAgents; }
    /// Get the navigation mesh associated with this crowd
    [[nodiscard]] const NavigationMesh& getNavMesh() const noexcept { return *_recast; }
    /// Check if the navMesh is valid
    [[nodiscard]] bool isValidNavMesh() const;
    /// Change the navigation mesh for this crowd
    void setNavMesh(NavigationMesh* navMesh) noexcept { _recast = navMesh; }
    /// The maximum number of agents that are allowed in this crowd.
    [[nodiscard]] I32 getMaxNbAgents() const { return _crowd->getAgentCount(); }
    /// Get all (active) agents in this crowd.
                  void getActiveAgents( vector<dtCrowdAgent*>& agentsOut ) const;
    /// Get the IDs of all (active) agents in this crowd.
    [[nodiscard]] vector<I32> getActiveAgentIDs() const;
    /// The last set destination for the crowd.
    /// This is the destination that will be assigned to newly added agents.
    [[nodiscard]] float3 getLastDestination() const noexcept { return float3(_targetPos); }
    /// Reference to the DetourCrowd object that is wrapped.
    dtCrowd* _crowd = nullptr;
    /// Reference to the Recast/Detour wrapper object for Divide.
    NavigationMesh* _recast = nullptr;
    /// The latest set target or destination section in the recast navmesh.
    dtPolyRef _targetRef = 0u;
    /// The latest set target or destination position.
    F32 _targetPos[3]{0.f, 0.f, 0.f};
    /// Max pathlength for calculated paths.
    static constexpr I32 AGENT_MAX_TRAIL = 64;
    /// Max number of agents allowed in this crowd.
    static constexpr I32 MAX_AGENTS = 128;
    /// Stores the calculated paths for each agent in the crowd.
    struct AgentTrail {
        F32 trail[AGENT_MAX_TRAIL * 3];
        I32 htrail;
    };
    AgentTrail _trails[MAX_AGENTS];
    /// Debug info object used in the original recast/detour demo, not used in
    /// this application.
    dtCrowdAgentDebugInfo _agentDebug;
    /// Parameters for obstacle avoidance of DetourCrowd steering.
    dtObstacleAvoidanceDebugData* _vod = nullptr;
    /// Agent configuration parameters
    bool _anticipateTurns = true;
    bool _optimizeVis = true;
    bool _optimizeTopo = true;
    bool _obstacleAvoidance = true;
    bool _separation = false;
    F32 _obstacleAvoidanceType = 3.0f;
    F32 _separationWeight = 2.0f;

   protected:
    /**
      * Helper to calculate the needed velocity to steer an agent to a target destination.
      * Parameters:
      *     velocity    is the return parameter, the calculated velocity
      *     position    is the current position of the agent
      *     target      is the target destination to reach
      *     speed       is the (max) speed the agent can travel at
      *
      * This function can be used together with requestMoveVelocity to achieve the functionality
      * of the old adjustMoveTarget function.
      **/
    static void calcVel(F32* velocity, const F32* position, const F32* target, F32 speed);

   private:
    /// Number of (active) agents in the crowd.
    I32 _activeAgents = 0;
};  // DivideDtCrowd

}  // Navigation
}  // namespace AI
}  // namespace Divide

#endif //DVD_CROWD_H_
