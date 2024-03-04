

#include "Headers/DivideCrowd.h"

#include "Utility/Headers/Localization.h"

#include <recastnavigation/DetourCommon.h>

namespace Divide::AI::Navigation {

DivideDtCrowd::DivideDtCrowd(NavigationMesh* navMesh)
    : _recast(navMesh)
{
    assert(_recast);
    _crowd = dtAllocCrowd();
    if (!_crowd) {
        Console::errorfn(LOCALE_STR("ERROR_DETOUR_CROWD_INSTANCE"));
        assert(_crowd != nullptr);
    }

    memset(_trails, 0, sizeof _trails);

    _vod = dtAllocObstacleAvoidanceDebugData();
    assert(_vod != nullptr);
    _vod->init(2048);

    memset(&_agentDebug, 0, sizeof _agentDebug);
    _agentDebug.idx = -1;
    _agentDebug.vod = _vod;

    dtNavMesh* nav = Attorney::NavigationMeshCrowd::getNavigationMesh(*_recast);
    assert(nav);

    dtCrowd* crowd = _crowd;

    if (nav && crowd && crowd->getAgentCount() == 0) {
        crowd->init(MAX_AGENTS, Attorney::NavigationMeshCrowd::getConfigParams(
                                    *navMesh).getAgentRadius(),
                    nav);
        // Make polygons with 'disabled' flag invalid.
        crowd->getEditableFilter(0)->setExcludeFlags(to_U16(SamplePolyFlags::SAMPLE_POLYFLAGS_DISABLED));
        // Create different avoidance settings presets. The crowd object can
        // store multiple,
        // identified by an index number.
        // Setup local avoidance params to different qualities.
        dtObstacleAvoidanceParams params;
        // Use mostly default settings, copy from dtCrowd.
        memcpy(&params, crowd->getObstacleAvoidanceParams(0), sizeof(dtObstacleAvoidanceParams));
        // Low (11)
        params.velBias = 0.5f;
        params.adaptiveDivs = 5;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 1;
        crowd->setObstacleAvoidanceParams(0, &params);
        // Medium (22)
        params.velBias = 0.5f;
        params.adaptiveDivs = 5;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 2;
        crowd->setObstacleAvoidanceParams(1, &params);
        // Good (45)
        params.velBias = 0.5f;
        params.adaptiveDivs = 7;
        params.adaptiveRings = 2;
        params.adaptiveDepth = 3;
        crowd->setObstacleAvoidanceParams(2, &params);
        // High (66)
        params.velBias = 0.5f;
        params.adaptiveDivs = 7;
        params.adaptiveRings = 3;
        params.adaptiveDepth = 3;
        crowd->setObstacleAvoidanceParams(3, &params);
    }
}

DivideDtCrowd::~DivideDtCrowd() {
    dtFreeCrowd(_crowd);
    dtFreeObstacleAvoidanceDebugData(_vod);
}

void DivideDtCrowd::update(const U64 deltaTimeUS) {
    const dtNavMesh* nav = Attorney::NavigationMeshCrowd::getNavigationMesh(*_recast);

    if (!nav || !_crowd) return;
    // TimeVal startTime = getPerfTime();
    _crowd->update(Time::MicrosecondsToSeconds<F32>(deltaTimeUS), &_agentDebug);
    // TimeVal endTime = getPerfTime();
    // Update agent trails
    for (I32 i = 0; i < _crowd->getAgentCount(); ++i) {
        const dtCrowdAgent* ag = _crowd->getAgent(i);

        if (!ag->active) continue;

        AgentTrail* trail = &_trails[i];
        // Update agent movement trail.
        trail->htrail = (trail->htrail + 1) % AGENT_MAX_TRAIL;
        dtVcopy(&trail->trail[trail->htrail * 3], ag->npos);
    }
    _agentDebug.vod->normalizeSamples();
    // m_crowdSampleCount.addSample((float)_crowd->getVelocitySampleCount());
    // m_crowdTotalTime.addSample(getPerfDeltaTimeUsec(startTime, endTime) /
    // 1000.0f);
}

I32 DivideDtCrowd::addAgent(const vec3<F32>& position, const F32 maxSpeed, const F32 acceleration) {
    // Define parameters for agent in crowd
    dtCrowdAgentParams ap;
    memset(&ap, 0, sizeof ap);
    ap.radius = to_F32(getAgentRadius());
    ap.height = to_F32(getAgentHeight());
    ap.maxAcceleration = acceleration;
    ap.maxSpeed = maxSpeed;
    ap.collisionQueryRange = ap.radius * 12.0f;
    ap.pathOptimizationRange = ap.radius * 30.0f;
    // Set update flags according to config
    ap.updateFlags = 0;
    if (_anticipateTurns) ap.updateFlags |= DT_CROWD_ANTICIPATE_TURNS;
    if (_optimizeVis) ap.updateFlags |= DT_CROWD_OPTIMIZE_VIS;
    if (_optimizeTopo) ap.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;
    if (_obstacleAvoidance) ap.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
    if (_separation) ap.updateFlags |= DT_CROWD_SEPARATION;
    ap.obstacleAvoidanceType = (U8)_obstacleAvoidanceType;
    ap.separationWeight = _separationWeight;
    const F32* p = &position.x;
    const I32 idx = _crowd->addAgent(p, &ap);
    if (idx != -1) {
        // If a move target is defined: move agent towards it
        // TODO: do we want to set newly added agent's destination to previously
        // set target?
        // or remove this behaviour?
        if (_targetRef) _crowd->requestMoveTarget(idx, _targetRef, _targetPos);

        // Init trail
        AgentTrail* trail = &_trails[idx];
        for (U32 i = 0; i < AGENT_MAX_TRAIL; ++i)
            dtVcopy(&trail->trail[i * 3], p);
        trail->htrail = 0;
    }

    _activeAgents++;
    return idx;
}

vector<dtCrowdAgent*> DivideDtCrowd::getActiveAgents() const
{
    dtCrowdAgent** resultEntries = MemoryManager_NEW dtCrowdAgent * [getMaxNbAgents()];
    const I32 size = _crowd->getActiveAgents(resultEntries, getMaxNbAgents());

    vector<dtCrowdAgent*> result(resultEntries, resultEntries + size);
    MemoryManager::DELETE_ARRAY(resultEntries);
    return result;
}

vector<I32> DivideDtCrowd::getActiveAgentIDs() const
{
    vector<I32> result;
    result.reserve(getMaxNbAgents());

    for (I32 i = 0; i < getMaxNbAgents(); i++) {
        const dtCrowdAgent* agent = _crowd->getAgent(i);
        if (agent->active) {
            result.push_back(i);
        }
    }

    return result;
}

void DivideDtCrowd::removeAgent(const I32 idx) {
    _crowd->removeAgent(idx);
    _activeAgents--;
}

vec3<F32> DivideDtCrowd::calcVel(const vec3<F32>& position,
                                 const vec3<F32>& target,
                                 const D64 speed) {
    vec3<F32> res;
    calcVel(res._v, &position.x, &target.x, to_F32(speed));
    return res;
}

void DivideDtCrowd::calcVel(F32* velocity, const F32* position, const F32* target, const F32 speed) {
    dtVsub(velocity, target, position);
    velocity[1] = 0.0;
    dtVnormalize(velocity);
    dtVscale(velocity, velocity, speed);
}

void DivideDtCrowd::setMoveTarget(const vec3<F32>& position, const bool adjust) {
    // Find nearest point on navmesh and set move request to that location.
    const dtNavMeshQuery& navquery = _recast->getNavQuery();

    const dtQueryFilter* filter = _crowd->getFilter(0);
    const F32* ext = _crowd->getQueryExtents();
    const F32* p = &position.x;
    navquery.findNearestPoly(p, ext, filter, &_targetRef, _targetPos);
    // Adjust target using tiny local search. (instead of recalculating full
    // path)
    if (adjust) {
        for (I32 i = 0; i < _crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* ag = _crowd->getAgent(i);
            if (!ag->active) continue;
            F32 vel[3];
            calcVel(vel, ag->npos, p, ag->params.maxSpeed);
            _crowd->requestMoveVelocity(i, vel);
        }
    } else {
        // Move target using path finder (recalculate a full new path)
        for (I32 i = 0; i < _crowd->getAgentCount(); ++i) {
            const dtCrowdAgent* ag = _crowd->getAgent(i);
            if (!ag->active) continue;
            _crowd->requestMoveTarget(i, _targetRef, _targetPos);
        }
    }
}

void DivideDtCrowd::setMoveTarget(const I32 agentID, const vec3<F32>& position, const bool adjust) {
    // TODO extract common method
    // Find nearest point on navmesh and set move request to that location.
    const dtNavMeshQuery& navquery = _recast->getNavQuery();
    dtCrowd* crowd = _crowd;
    const dtQueryFilter* filter = crowd->getFilter(0);
    const F32* ext = crowd->getQueryExtents();
    const F32* p = &position.x;
    navquery.findNearestPoly(p, ext, filter, &_targetRef, _targetPos);
    // ----
    if (adjust) {
        const dtCrowdAgent* ag = getAgent(agentID);
        F32 vel[3];
        calcVel(vel, ag->npos, p, ag->params.maxSpeed);
        crowd->requestMoveVelocity(agentID, vel);
    } else {
        _crowd->requestMoveTarget(agentID, _targetRef, _targetPos);
    }
}

bool DivideDtCrowd::requestVelocity(const I32 agentID, const vec3<F32>& velocity) const {
    if (!getAgent(agentID)->active) {
        return false;
    }

    return _crowd->requestMoveVelocity(agentID, &velocity.x);
}

bool DivideDtCrowd::stopAgent(const I32 agentID) const {
    constexpr F32 zeroVel[] = {0, 0, 0};
    return _crowd->resetMoveTarget(agentID) &&
           _crowd->requestMoveVelocity(agentID, zeroVel);
}

F32 DivideDtCrowd::getDistanceToGoal(const dtCrowdAgent* agent, const F32 maxRange) {
    if (!agent || !agent->ncorners || agent->ncorners < 0) return maxRange;

    if (agent->cornerFlags[agent->ncorners - 1] & DT_STRAIGHTPATH_END)
        return dtMin(dtVdist2D(agent->npos,
                               &agent->cornerVerts[(agent->ncorners - 1) * 3]),
                     maxRange);

    return maxRange;
}

bool DivideDtCrowd::destinationReached(const dtCrowdAgent* agent, const F32 maxDistanceFromTarget) {
    return getDistanceToGoal(agent, maxDistanceFromTarget) < maxDistanceFromTarget;
}

bool DivideDtCrowd::isValidNavMesh() const {
    return _recast->getNavQuery().getAttachedNavMesh() != nullptr;
}

}  // namespace Divide::AI::Navigation
