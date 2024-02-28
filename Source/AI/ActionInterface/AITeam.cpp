

#include "Headers/AITeam.h"

#include "AI/Headers/AIEntity.h"
#include "AI/Headers/AIManager.h"
#include "AI/PathFinding/Headers/DivideCrowd.h"

namespace Divide::AI {

namespace {
    constexpr U16 g_entityThreadedThreashold = 16u;
}

AITeam::AITeam(const U32 id, AIManager& parentManager)
     : GUIDWrapper(),
       _teamID(id),
       _parentManager(parentManager)
{
    _team.clear();
    _parentManager.registerTeam(this);
}

AITeam::~AITeam()
{
    _parentManager.unregisterTeam(this);
    {
        LockGuard<SharedMutex> w_lock(_crowdMutex);
        MemoryManager::DELETE_HASHMAP(_aiTeamCrowd);
    }
    {
        LockGuard<SharedMutex> w_lock(_updateMutex);
        for (TeamMap::value_type& entity : _team) {
            Attorney::AIEntityAITeam::setTeamPtr(*entity.second, nullptr);
        }
        _team.clear();
    }
}

void AITeam::addCrowd(const AIEntity::PresetAgentRadius radius, Navigation::NavigationMesh* navMesh) {
    DIVIDE_ASSERT(_aiTeamCrowd.find(radius) == std::end(_aiTeamCrowd),
                  "AITeam error: DtCrowd already existed for new navmesh!");
    emplace(_aiTeamCrowd, radius,
            MemoryManager_NEW Navigation::DivideDtCrowd(navMesh));
}

void AITeam::removeCrowd(const AIEntity::PresetAgentRadius radius) {
    const AITeamCrowd::iterator it = _aiTeamCrowd.find(radius);
    DIVIDE_ASSERT(
        it != std::end(_aiTeamCrowd),
        "AITeam error: DtCrowd does not exist for specified navmesh!");
    MemoryManager::DELETE(it->second);
    _aiTeamCrowd.erase(it);
}

vector<AIEntity*> AITeam::getEntityList() const {
    //ToDo: Cache this? -Ionut
    SharedLock<SharedMutex> r2_lock(_updateMutex);

    U32 i = 0;
    vector<AIEntity*> entities(_team.size(), nullptr);
    for (const TeamMap::value_type& entity : _team) {
        entities[i++] = entity.second;
    }

    return entities;
}

bool AITeam::update(TaskPool& parentPool, const U64 deltaTimeUS) {
    // Crowds
    {
        SharedLock<SharedMutex> r1_lock(_crowdMutex);
        for (AITeamCrowd::value_type& it : _aiTeamCrowd) {
            it.second->update(deltaTimeUS);
        }
    }

    vector<AIEntity*> entities = getEntityList();
    for (AIEntity* entity : entities) {
        if (!Attorney::AIEntityAITeam::update(*entity, deltaTimeUS)) {
            return false;
        }
    }
    const U16 entityCount = to_U16(entities.size());
    if (entityCount <= g_entityThreadedThreashold) {
        for (AIEntity* entity : entities) {
            if (!Attorney::AIEntityAITeam::update(*entity, deltaTimeUS)) {
                //print error;
            }
        }
    } else {
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = entityCount;
        descriptor._partitionSize = g_entityThreadedThreashold;
        descriptor._cbk = [this, deltaTimeUS, &entities](const Task*, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                if (!Attorney::AIEntityAITeam::update(*entities[i], deltaTimeUS)) {
                    //print error;
                }
            }
        };

        parallel_for(parentPool, descriptor);
    }
    return true;
}

bool AITeam::processInput(TaskPool& parentPool, const U64 deltaTimeUS) {
    vector<AIEntity*> entities = getEntityList();

    const U16 entityCount = to_U16(entities.size());
    if (entityCount <= g_entityThreadedThreashold) {
        for (AIEntity* entity : entities) {
            if (!Attorney::AIEntityAITeam::processInput(*entity, deltaTimeUS)) {
                //print error;
            }
        }
    } else {
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = entityCount;
        descriptor._partitionSize = g_entityThreadedThreashold;
        descriptor._cbk = [this, deltaTimeUS, &entities](const Task*, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                if (!Attorney::AIEntityAITeam::processInput(*entities[i], deltaTimeUS)) {
                    //print error;
                }
            }
        };

        parallel_for(parentPool, descriptor);
    }

    return true;
}

bool AITeam::processData(TaskPool& parentPool, const U64 deltaTimeUS) {
    vector<AIEntity*> entities = getEntityList();

    const U16 entityCount = to_U16(entities.size());
    if (entityCount <= g_entityThreadedThreashold) {
        for (AIEntity* entity : entities) {
            if (!Attorney::AIEntityAITeam::processData(*entity, deltaTimeUS)) {
                //print error;
            }
        }
    } else {
        ParallelForDescriptor descriptor = {};
        descriptor._iterCount = entityCount;
        descriptor._partitionSize = g_entityThreadedThreashold;
        descriptor._cbk = [this, deltaTimeUS, &entities](const Task*, const U32 start, const U32 end) {
            for (U32 i = start; i < end; ++i) {
                if (!Attorney::AIEntityAITeam::processData(*entities[i], deltaTimeUS)) {
                    //print error;
                }
            }
        };

        parallel_for(parentPool, descriptor);
    }

    return true;
}

void AITeam::resetCrowd() {
    vector<AIEntity*> entities = getEntityList();
    for (AIEntity* entity : entities) {
        entity->resetCrowd();
    }
}

bool AITeam::addTeamMember(AIEntity* entity) {
    if (!entity) {
        return false;
    }
    /// If entity already belongs to this team, no need to do anything
    LockGuard<SharedMutex> w_lock(_updateMutex);
    if (_team.find(entity->getGUID()) != std::end(_team)) {
        return true;
    }
    insert(_team, entity->getGUID(), entity);
    Attorney::AIEntityAITeam::setTeamPtr(*entity, this);

    return true;
}

// Removes an entity from this list
bool AITeam::removeTeamMember(AIEntity* entity) {
    if (!entity) {
        return false;
    }

    LockGuard<SharedMutex> w_lock(_updateMutex);
    if (_team.find(entity->getGUID()) != std::end(_team)) {
        _team.erase(entity->getGUID());
    }
    return true;
}

bool AITeam::addEnemyTeam(const U32 enemyTeamID) {
    if (findEnemyTeamEntry(enemyTeamID) == std::end(_enemyTeams)) {
        LockGuard<SharedMutex> w_lock(_updateMutex);
        _enemyTeams.push_back(enemyTeamID);
        return true;
    }
    return false;
}

bool AITeam::removeEnemyTeam(const U32 enemyTeamID) {
    const vector<U32>::iterator it = findEnemyTeamEntry(enemyTeamID);
    if (it != end(_enemyTeams)) {
        LockGuard<SharedMutex> w_lock(_updateMutex);
        _enemyTeams.erase(it);
        return true;
    }
    return false;
}

} // namespace Divide::AI
