

#include "Headers/AIEntity.h"

#include "AI/PathFinding/Headers/DivideCrowd.h"
#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"
#include "AI/ActionInterface/Headers/AITeam.h"
#include "AI/ActionInterface/Headers/AIProcessor.h"

#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "Sensors/Headers/AudioSensor.h"

namespace Divide {
using namespace AI;

constexpr F32 DESTINATION_RADIUS = 2.f;
constexpr F32 DESTINATION_RADIUS_SQ = DESTINATION_RADIUS *
                                      DESTINATION_RADIUS;
constexpr F32 DESTINATION_RADIUS_F = to_F32(DESTINATION_RADIUS);

AIEntity::AIEntity( NPC* parent, const vec3<F32>& currentPosition, std::string_view name)
    : GUIDWrapper(),
      _name(MOV(name)),
      _teamPtr(nullptr),
      _processor(nullptr),
      _unitRef( parent ),
      _agentID(-1),
      _detourCrowd(nullptr),
      _agent(nullptr),
      _distanceToTarget(-1.f),
      _previousDistanceToTarget(-1.f),
      _moveWaitTimer(0ULL),
      _stopped(false)
{
    _currentPosition.set(currentPosition);
    _agentRadiusCategory = PresetAgentRadius::AGENT_RADIUS_SMALL;
}

AIEntity::~AIEntity()
{
    if (_detourCrowd) {
        _detourCrowd->removeAgent(getAgentID());
    }

    if (!setAndSurrenderAIProcessor( nullptr ))
    {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void AIEntity::load( const vec3<F32>& currentPosition )
{
    if (!setPosition( currentPosition ))
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    if (!isAgentLoaded() && _detourCrowd) {
        _agentID = _detourCrowd->addAgent( currentPosition,
                                          _unitRef
                                              ? _unitRef->getMovementSpeed()
                                              : to_F32(_detourCrowd->getAgentHeight() / 2 * 3.5f),
                                           _unitRef
                                              ? _unitRef->getAcceleration()
                                              : 5.0f);

        _agent = _detourCrowd->getAgent(_agentID);
        _destination = currentPosition;
    }
}

void AIEntity::unload() {
    if (!isAgentLoaded()) {
        return;
    }

    if (_detourCrowd) {
        _detourCrowd->removeAgent(getAgentID());
    }

    _agentID = -1;
    _agent = nullptr;
}

void AIEntity::sendMessage(AIEntity& receiver,
                           const AIMsg msg,
                           const std::any& msg_content) {
    
    if (getGUID() != receiver.getGUID()) {
        receiver.receiveMessage(*this, msg, msg_content);
    }
}

void AIEntity::receiveMessage(AIEntity& sender,
                              const AIMsg msg,
                              const std::any& msg_content) {
    processMessage(sender, msg, msg_content);
}

void AIEntity::processMessage(AIEntity& sender,
                              const AIMsg msg,
                              const std::any& msg_content) {
    assert(_processor);
    SharedLock<SharedMutex> r_lock(_updateMutex);
    _processor->processMessage(sender, msg, msg_content);
}

Sensor* AIEntity::getSensor(const SensorType type) {
    SharedLock<SharedMutex> r_lock(_updateMutex);
    const SensorMap::const_iterator it = _sensorList.find(type);
    if (it != std::end(_sensorList)) {
        return it->second.get();
    }
    return nullptr;
}

bool AIEntity::addSensor(const SensorType type)
{
    LockGuard<SharedMutex> w_lock(_updateMutex);
    std::unique_ptr<Sensor>& sensor = _sensorList[type];
    switch (type)
    {
        case SensorType::AUDIO_SENSOR:
        {
            sensor = std::make_unique<AudioSensor>(this);
        } break;
        case SensorType::VISUAL_SENSOR:
        {
            sensor = std::make_unique<VisualSensor>(this);
        } break;
        default:
        case SensorType::NONE:
        {
                
        } break;
    }

    return true;
}

bool AIEntity::setAndSurrenderAIProcessor( AIProcessor* processor )
{
    LockGuard<SharedMutex> w_lock(_updateMutex);
    _processor.reset(processor);
    if (_processor)
    {
        _processor->addEntityRef(this);
        processor = nullptr;
    }

    return true;
}

bool AIEntity::processInput(const U64 deltaTimeUS) {
    if (_processor) {
        _processor->init();
        if (!_processor->processInput(deltaTimeUS)) {
            return false;
        }
    }

    return true;
}

bool AIEntity::processData(const U64 deltaTimeUS) {
    if (_processor) {
        if (!_processor->processData(deltaTimeUS)) {
            return false;
        }
    }

    return true;
}

bool AIEntity::update(const U64 deltaTimeUS) {
    if (_processor) {
        _processor->update(deltaTimeUS, _unitRef);
    }
    if (_unitRef) {
        _unitRef->update(deltaTimeUS);
        if (!_detourCrowd) {
            resetCrowd();
        }
    }
    updatePosition(deltaTimeUS);

    return true;
}

U32 AIEntity::getTeamID() const
{
    if (_teamPtr)
    {
        return _teamPtr->teamID();
    }
    return U32_MAX;
}

void AIEntity::setTeamPtr(AITeam* const teamPtr) {
    if (_teamPtr) {
        _teamPtr->removeTeamMember(this);
    }
    _teamPtr = teamPtr;
}


F32 AIEntity::getAgentHeight() const noexcept {
    return _detourCrowd ? _detourCrowd->getAgentHeight() : 0.f;
}

F32 AIEntity::getAgentRadius() const noexcept {
    return _detourCrowd ? _detourCrowd->getAgentRadius() : 0.f;
}

void AIEntity::resetCrowd()
{
    if (_detourCrowd)
    {
        unload();
    }

    _detourCrowd = _teamPtr->getCrowd(getAgentRadiusCategory());

    if (_detourCrowd) {
        _destination = _detourCrowd->getLastDestination();
        load(_unitRef ? _unitRef->getPosition() : vec3<F32>());
    }
}

bool AIEntity::setPosition(const vec3<F32>& position) {
    if (!isAgentLoaded()) {
        if (_unitRef) {
            _unitRef->setPosition(position);
        }
        return false;
    }

    if (!_detourCrowd || !_detourCrowd->isValidNavMesh()) {
        return false;
    }

    // Find position on NavMesh
    vec3<F32> result;
    const bool isPointOnNavMesh = _detourCrowd->getNavMesh().getClosestPosition(position,
                                                                                vec3<F32>(5),
                                                                                DESTINATION_RADIUS_F,
                                                                                result);
    DIVIDE_ASSERT(isPointOnNavMesh, "AIEntity::setPosition error: Invalid NavMesh position returned!");

    // Remove agent from crowd and re-add at position
    _detourCrowd->removeAgent(_agentID);
    _agentID = _detourCrowd->addAgent(result,
                                      _unitRef
                                          ? _unitRef->getMovementSpeed()
                                          : to_F32(_detourCrowd->getAgentHeight() / 2 * 3.5f),
                                      _unitRef
                                          ? _unitRef->getAcceleration()
                                          : 5.0f);

    _agent = _detourCrowd->getAgent(_agentID);

    if (_unitRef) {
        _unitRef->setPosition(result);
    }

    return true;
}

void AIEntity::updatePosition(const U64 deltaTimeUS)
{
    if (isAgentLoaded() && getAgent()->active)
    {
        _previousDistanceToTarget = _distanceToTarget;
        _distanceToTarget = _currentPosition.distanceSquared(_destination);
        const F32 distanceDelta = std::abs(_previousDistanceToTarget - _distanceToTarget);

        // if we are walking but did not change distance in a while
        if (distanceDelta < DESTINATION_RADIUS_SQ)
        {
            _moveWaitTimer += deltaTimeUS;

            if (Time::MicrosecondsToSeconds<F32>(_moveWaitTimer) > 5.f)
            {
                // DISABLED FOR NOW!
                if constexpr(false)
                {
                    _moveWaitTimer = 0;
                    stop();
                    if (_detourCrowd)
                    {
                        vec3<F32> result;
                        const bool isPointOnNavMesh = _detourCrowd->getNavMesh().getClosestPosition(_currentPosition, vec3<F32>(5), DESTINATION_RADIUS_F, result);

                        DIVIDE_ASSERT(isPointOnNavMesh, "AIEntity::updatePosition error: Invalid NavMesh position returned!");
                        if (!_unitRef->moveTo(result, deltaTimeUS))
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    }
                    return;
                }
            }
        } else {
            _moveWaitTimer = 0;
        }
        _currentPosition.set(getAgent()->npos);
        _currentVelocity.set(getAgent()->nvel);
    }

    if (_unitRef) {
        _unitRef->setPosition(_currentPosition);
        _unitRef->setVelocity(_currentVelocity);
    }
}

bool AIEntity::updateDestination(const vec3<F32>& destination, const bool updatePreviousPath) {
    if (!isAgentLoaded()) {
        return false;
    }

    // Skip very small updates
    if (_destination.distanceSquared(destination) <= DESTINATION_RADIUS_SQ) {
        return true;
    }

    // Find position on navmesh
    vec3<F32> result;
    bool isPointOnNavMesh =
        _detourCrowd->getNavMesh().getRandomPositionInCircle(destination,
                                                             DESTINATION_RADIUS_F,
                                                             vec3<F32>(5),
                                                             result,
                                                             10);
    if (!isPointOnNavMesh) {
        isPointOnNavMesh =
            _detourCrowd->getNavMesh().getClosestPosition(destination,
                                                          vec3<F32>(5),
                                                          DESTINATION_RADIUS_F,
                                                          result);
    }

    if (isPointOnNavMesh) {
        _detourCrowd->setMoveTarget(_agentID, result, updatePreviousPath);
        _destination = result;
        _stopped = false;
    }

    return isPointOnNavMesh;
}

const vec3<F32>& AIEntity::getPosition() const noexcept {
    return _currentPosition;
}

const vec3<F32>& AIEntity::getDestination() const noexcept {
    if (isAgentLoaded()) {
        return _destination;
    }

    // TODO: this is not ideal! -Ionut
    return getPosition();
}

bool AIEntity::destinationReached() const {
    if (!isAgentLoaded()) {
        return false;
    }

    return _currentPosition.distanceSquared(getDestination()) <= DESTINATION_RADIUS_SQ ||
           Navigation::DivideDtCrowd::destinationReached(getAgent(), DESTINATION_RADIUS_F);
}

void AIEntity::setDestination(const vec3<F32>& destination) noexcept {
    if (!isAgentLoaded()) {
        return;
    }

    _destination = destination;
    _stopped = false;
}

void AIEntity::moveForward() {
    const vec3<F32> lookDirection(_unitRef != nullptr
                                      ? Normalized(_unitRef->getLookingDirection())
                                      : WORLD_Z_NEG_AXIS);

    setVelocity(lookDirection * to_F32(getMaxSpeed()));
}

void AIEntity::moveBackwards() {
    const vec3<F32> lookDirection(_unitRef != nullptr
                                      ? Normalized(_unitRef->getLookingDirection())
                                      : WORLD_Z_NEG_AXIS);

    setVelocity(lookDirection * to_F32(getMaxSpeed()) * -1.0f);
}

void AIEntity::setVelocity(const vec3<F32>& velocity) {
    _stopped = false;
    _destination.reset();

    if (!isAgentLoaded() || !_detourCrowd->requestVelocity(getAgentID(), velocity)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void AIEntity::stop() {
    if (!isAgentLoaded()) {
        _stopped = true;
        return;
    }

    if (_detourCrowd->stopAgent(getAgentID())) {
        _destination.reset();
        _stopped = true;
    }
}

vec3<F32> AIEntity::getVelocity() const noexcept {
    return isAgentLoaded() ? vec3<F32>(getAgent()->nvel) : vec3<F32>();
}

F32 AIEntity::getMaxSpeed() const noexcept
{
    return isAgentLoaded() ? getAgent()->params.maxSpeed : 0.f;
}

F32 AIEntity::getMaxAcceleration() const noexcept
{
    return isAgentLoaded() ? getAgent()->params.maxAcceleration : 0.f;
}

string AIEntity::toString() const {
    if (_processor) {
        return _processor->toString();
    }

    return "Error_" + name();
}
}  // namespace Divide
