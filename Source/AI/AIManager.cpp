

#include "config.h"

#include "Headers/AIManager.h"

#include "AI/ActionInterface/Headers/AITeam.h"
#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"

namespace Divide {

using namespace AI;

AIManager::AIManager(Scene& parentScene, TaskPool& pool)
    : SceneComponent(parentScene),
      _parentPool(pool),
      _deltaTimeUS(0ULL),
      _currentTimeUS(0ULL),
      _previousTimeUS(0ULL),
      _navMeshDebugDraw(false),
      _pauseUpdate(true),
      _updating(false)
{
    _shouldStop = false;
    _running = false;
}

AIManager::~AIManager()
{
    destroy();
}

/// Clear up any remaining AIEntities
void AIManager::destroy()
{
    {
        LockGuard<Mutex> w_lock(_updateMutex);
        _aiTeams.clear();
    }
    {
        LockGuard<SharedMutex> w_lock(_navMeshMutex);
        _navMeshes.clear();
    }
}

void AIManager::update(const U64 deltaTimeUS) {
     constexpr U64 updateFreqUS = Time::SecondsToMicroseconds(1) / (Config::TARGET_FRAME_RATE / Config::TICK_DIVISOR);

    _currentTimeUS += deltaTimeUS;
    if (_currentTimeUS >= _previousTimeUS + updateFreqUS && !shouldStop()) {
        _running = true;
        /// use internal delta time calculations
        _deltaTimeUS = _currentTimeUS - _previousTimeUS;
        {
            /// Lock the entities during update() adding or deleting entities is
            /// suspended until this returns
            LockGuard<Mutex> r_lock(_updateMutex);
            if (!_aiTeams.empty() && !_pauseUpdate) {
                _updating = true;
                if (_sceneCallback) {
                    _sceneCallback();
                }

                if (processInput(deltaTimeUS)) {  // sensors
                    if (processData(deltaTimeUS)) {  // think
                        updateEntities(deltaTimeUS);  // react
                    }
                }

                _updating = false;
            }
        }
        _previousTimeUS = _currentTimeUS;
        _running = false;
    }
}

bool AIManager::processInput(const U64 deltaTimeUS) {  // sensors
    for (AITeamMap::value_type& team : _aiTeams) {
        if (!team.second->processInput(_parentPool, deltaTimeUS)) {
            return false;
        }
    }
    return true;
}

bool AIManager::processData(const U64 deltaTimeUS) {  // think
    for (AITeamMap::value_type& team : _aiTeams) {
        if (!team.second->processData(_parentPool, deltaTimeUS)) {
            return false;
        }
    }
    return true;
}

bool AIManager::updateEntities(const U64 deltaTimeUS) {  // react
    for (AITeamMap::value_type& team : _aiTeams) {
        if (!team.second->update(_parentPool, deltaTimeUS)) {
            return false;
        }
    }
    return true;
}

bool AIManager::registerEntity(const U32 teamID, AIEntity* entity) {
    LockGuard<Mutex> w_lock(_updateMutex);
    const AITeamMap::const_iterator it = _aiTeams.find(teamID);
    DIVIDE_ASSERT(it != std::end(_aiTeams),
                  "AIManager error: attempt to register an AI Entity to a "
                  "non-existent team!");
    it->second->addTeamMember(entity);
    return true;
}

void AIManager::unregisterEntity(AIEntity* entity) {
    for (AITeamMap::value_type& team : _aiTeams) {
        unregisterEntity(team.second->teamID(), entity);
    }
}

void AIManager::unregisterEntity(const U32 teamID, AIEntity* entity) {
    LockGuard<Mutex> w_lock(_updateMutex);
    const AITeamMap::const_iterator it = _aiTeams.find(teamID);
    DIVIDE_ASSERT(it != std::end(_aiTeams),
                  "AIManager error: attempt to remove an AI Entity from a "
                  "non-existent team!");
    it->second->removeTeamMember(entity);
}

Navigation::NavigationMesh* AIManager::addNavMesh( PlatformContext& context, Navigation::DivideRecast& recastInterface, Scene& parentScene, const AIEntity::PresetAgentRadius radius )
{
    _navMeshMutex.lock();
    std::unique_ptr<Navigation::NavigationMesh>& navMesh = _navMeshes[radius];
    if (navMesh == nullptr)
    {
        navMesh = std::make_unique<Navigation::NavigationMesh>(context, recastInterface, parentScene);
        navMesh->debugDraw(_navMeshDebugDraw);
    }
    _navMeshMutex.unlock();

    {
        LockGuard<Mutex> w_lock(_updateMutex);
        for (AITeamMap::value_type& team : _aiTeams)
        {
            team.second->addCrowd(radius, navMesh.get());
        }
    }

    return navMesh.get();
}

bool AIManager::destroyNavMesh(const AIEntity::PresetAgentRadius radius)
{
    {
        LockGuard<SharedMutex> w_lock(_navMeshMutex);
        const NavMeshMap::iterator it = _navMeshes.find(radius);
        if ( it == std::end(_navMeshes))
        {
            return false;
        }

        _navMeshes.erase( it );
    }

    {
        LockGuard<Mutex> w_lock(_updateMutex);
        for (AITeamMap::value_type& team : _aiTeams)
        {
            team.second->removeCrowd(radius);
        }
    }

    return true;
}

AITeam* AIManager::registerTeam( const U32 id )
{
    LockGuard<Mutex> w_lock(_updateMutex);
    auto& team = _aiTeams[id];
    auto it = _aiTeams.find(id);
    if ( team == nullptr )
    {
        team.reset( new AITeam( id, *this ) );
    }

    return team.get();
}

bool AIManager::unregisterTeam(const U32 id )
{
    LockGuard<Mutex> w_lock(_updateMutex);

    const AITeamMap::iterator it = _aiTeams.find( id );
    if (it != std::end(_aiTeams))
    {
        _aiTeams.erase( it );
        return true;
    }

    return false;
}

void AIManager::toggleNavMeshDebugDraw(const bool state) {
    _navMeshDebugDraw = state;

    SharedLock<SharedMutex> r_lock(_navMeshMutex);
    for (NavMeshMap::value_type& it : _navMeshes) {
        it.second->debugDraw(state);
    }
}

void AIManager::debugDraw(GFX::CommandBuffer& bufferInOut,
                          GFX::MemoryBarrierCommand& memCmdInOut,
                          const bool forceAll)
{
    if (forceAll || _navMeshDebugDraw) {
        SharedLock<SharedMutex> r_lock(_navMeshMutex);
        for (NavMeshMap::value_type& it : _navMeshes) {
            if (forceAll || it.second->debugDraw()) {
                it.second->draw(forceAll, bufferInOut, memCmdInOut);
            }
        }
    }
}

bool AIManager::shouldStop() const noexcept {
    return _shouldStop;
}
};  // namespace Divide
