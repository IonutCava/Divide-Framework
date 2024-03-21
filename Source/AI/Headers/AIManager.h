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
#ifndef DVD_AI_MANAGER_H_
#define DVD_AI_MANAGER_H_

#include "AI/Headers/AIEntity.h"
#include "Scenes/Headers/SceneComponent.h"

namespace Divide {

class Scene;
struct Task;
class TaskPool;

namespace GFX {
    class CommandBuffer;

    struct MemoryBarrierCommand;
} //namespace GFX

namespace AI {

namespace Navigation {
    class NavigationMesh;
} //namespace Navigation

class AIManager final : public SceneComponent
{
  public:
    using AITeamMap = hashMap<U32, AITeam*>;
    using NavMeshMap = hashMap<AIEntity::PresetAgentRadius, Navigation::NavigationMesh*>;

    explicit AIManager(Scene& parentScene, TaskPool& pool);
    ~AIManager();

    /// Clear all AI related data (teams, entities, NavMeshes, etc);
    void destroy();
    /// Called at a fixed interval (preferably in a separate thread);
    void update(U64 deltaTimeUS);
    /// Add an AI Entity to a specific team.
    /// Entities can be added to multiple teams. Caller is responsible for the
    /// lifetime of entity
    bool registerEntity(U32 teamID, AIEntity* entity);
    /// Remove an AI Entity from a specific teams. Entities can be added to multiple teams.
    /// Caller is responsible for the lifetime of entity
    void unregisterEntity(U32 teamID, AIEntity* entity);
    /// Remove an AI Entity from all teams. Entities can be added to multiple teams.
    /// Caller is responsible for the lifetime of entity
    void unregisterEntity(AIEntity* entity);
    AITeam* getTeamByID(const I32 AITeamID) {
        if (AITeamID != -1) {
            const AITeamMap::const_iterator it = _aiTeams.find(AITeamID);
            if (it != std::end(_aiTeams)) {
                return it->second;
            }
        }
        return nullptr;
    }
    /// Add a NavMesh
    bool addNavMesh(AIEntity::PresetAgentRadius radius, Navigation::NavigationMesh* navMesh);

    Navigation::NavigationMesh* getNavMesh(const AIEntity::PresetAgentRadius radius) const {
        const NavMeshMap::const_iterator it = _navMeshes.find(radius);
        if (it != std::end(_navMeshes)) {
            return it->second;
        }
        return nullptr;
    }
    /// Remove a NavMesh
    void destroyNavMesh(AIEntity::PresetAgentRadius radius);

    void setSceneCallback(const DELEGATE<void>& callback) {
        LockGuard<Mutex> w_lock(_updateMutex);
        _sceneCallback = callback;
    }
    void pauseUpdate(const bool state) noexcept { _pauseUpdate = state; }
    bool updatePaused() const noexcept { return _pauseUpdate; }
    bool updating() const noexcept { return _updating; }
    /// Handle any debug information rendering (nav meshes, AI paths, etc).
    /// Called by Scene::postRender after depth map preview call
    void debugDraw(GFX::CommandBuffer& bufferInOut,
                   GFX::MemoryBarrierCommand& memCmdInOut,
                   bool forceAll = true);
    bool isDebugDraw() const noexcept { return _navMeshDebugDraw; }
    /// Toggle debug draw for all NavMeshes
    void toggleNavMeshDebugDraw(bool state);
    bool navMeshDebugDraw() const noexcept { return _navMeshDebugDraw; }

    void stop() noexcept { _shouldStop = true; }

    bool running() const noexcept { return _running; }

  protected:
    friend class AITeam;
    /// Register an AI Team
    void registerTeam(AITeam* team);
    /// Unregister an AI Team
    void unregisterTeam(AITeam* team);

    bool shouldStop() const noexcept;

  private:
    bool processInput(U64 deltaTimeUS);    ///< sensors
    bool processData(U64 deltaTimeUS);     ///< think
    bool updateEntities(U64 deltaTimeUS);  ///< react

  private:
    TaskPool& _parentPool;
    U64 _deltaTimeUS, _currentTimeUS, _previousTimeUS;
    std::atomic_bool _navMeshDebugDraw;
    std::atomic_bool _pauseUpdate;
    std::atomic_bool _updating;
    std::atomic_bool _shouldStop;
    std::atomic_bool _running;
    NavMeshMap _navMeshes;
    AITeamMap _aiTeams;
    mutable Mutex _updateMutex;
    mutable SharedMutex _navMeshMutex;
    DELEGATE<void> _sceneCallback;

};

FWD_DECLARE_MANAGED_CLASS(AIManager);

}  // namespace AI
}  // namespace Divide

#endif //DVD_AI_MANAGER_H_
