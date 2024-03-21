

#include "Headers/VisualSensor.h"

#include "AI/Headers/AIEntity.h"
#include "Utility/Headers/Localization.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Dynamics/Entities/Units/Headers/NPC.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {
using namespace AI;

VisualSensor::VisualSensor(AIEntity* const parentEntity)
    : Sensor(parentEntity, SensorType::VISUAL_SENSOR)
{
}

VisualSensor::~VisualSensor()
{
    for (NodeContainerMap::value_type& container : _nodeContainerMap) {
        container.second.clear();
    }
    _nodeContainerMap.clear();
    _nodePositionsMap.clear();
}

void VisualSensor::followSceneGraphNode(const U32 containerID, SceneGraphNode* node) {

    NodeContainerMap::iterator container = _nodeContainerMap.find(containerID);

    assert(node);

    if (container != std::end(_nodeContainerMap)) {
        const auto result = insert(container->second, node->getGUID(), node);
        if (!result.second) {
            Console::errorfn(LOCALE_STR("AI_DUPLICATE_SENSOR_INPUT_ERROR"));
        }
    } else {
        insert(_nodeContainerMap[containerID],
               node->getGUID(),
               node);
       
    }

    NodePositions& positions = _nodePositionsMap[containerID];
    insert(positions,
           node->getGUID(),
           node->get<TransformComponent>()->getWorldPosition());
}

void VisualSensor::unfollowSceneGraphNode(const U32 containerID, const U64 nodeGUID) {
    DIVIDE_ASSERT(nodeGUID != 0,
                  "VisualSensor error: Invalid node GUID specified for "
                  "unfollow function");
    NodeContainerMap::iterator container = _nodeContainerMap.find(containerID);
    if (container != std::end(_nodeContainerMap)) {
        const NodeContainer::iterator nodeEntry = container->second.find(nodeGUID);
        if (nodeEntry != std::end(container->second)) {
         
            container->second.erase(nodeEntry);
            NodePositions& positions = _nodePositionsMap[containerID];
            const NodePositions::iterator it = positions.find(nodeGUID);
            if (it != std::end(positions)) {
                positions.erase(it);
            }
        }
    }
}

void VisualSensor::update( [[maybe_unused]] const U64 deltaTimeUS) {
    for (const NodeContainerMap::value_type& container : _nodeContainerMap) {
        NodePositions& positions = _nodePositionsMap[container.first];
        for (const NodeContainer::value_type& entry : container.second) {
            SceneGraphNode* sgn(entry.second);
            if (sgn) {
                positions[entry.first] = sgn->get<TransformComponent>()->getWorldPosition();
            }
        }
    }
}

SceneGraphNode* VisualSensor::findClosestNode(const U32 containerID) {
    NodeContainerMap::iterator container = _nodeContainerMap.find(containerID);
    if (container != std::end(_nodeContainerMap)) {
        NodePositions& positions = _nodePositionsMap[container->first];
        NPC* const unit = _parentEntity->getUnitRef();
        if (unit) {
            const vec3<F32>& currentPosition = unit->getCurrentPosition();
            U64 currentNearest = positions.begin()->first;
            F32 currentDistanceSq = F32_MAX;
            for (const NodePositions::value_type& entry : positions) {
                const F32 temp = currentPosition.distanceSquared(entry.second);
                if (temp < currentDistanceSq) {
                    currentDistanceSq = temp;
                    currentNearest = entry.first;
                }
            }
            if (currentNearest != 0) {
                const NodeContainer::const_iterator nodeEntry =
                    container->second.find(currentNearest);
                if (nodeEntry != std::end(container->second)) {
                    return nodeEntry->second;
                }
            }
        }
    }

    return nullptr;
}

F32 VisualSensor::getDistanceToNodeSq(const U32 containerID, const U64 nodeGUID) {
    DIVIDE_ASSERT(
        nodeGUID != 0,
        "VisualSensor error: Invalid node GUID specified for distance request");
    NPC* const unit = _parentEntity->getUnitRef();
    if (unit) {
        return unit->getCurrentPosition().distanceSquared(
            getNodePosition(containerID, nodeGUID));
    }

    return F32_MAX;
}

vec3<F32> VisualSensor::getNodePosition(const U32 containerID, const U64 nodeGUID) {
    DIVIDE_ASSERT(
        nodeGUID != 0,
        "VisualSensor error: Invalid node GUID specified for position request");
    const NodeContainerMap::iterator container = _nodeContainerMap.find(containerID);
    if (container != std::end(_nodeContainerMap)) {
        NodePositions& positions = _nodePositionsMap[container->first];
        const NodePositions::iterator it = positions.find(nodeGUID);
        if (it != std::end(positions)) {
            return it->second;
        }
    }

    return vec3<F32>( F32_MAX );
}

}  // namespace Divide
