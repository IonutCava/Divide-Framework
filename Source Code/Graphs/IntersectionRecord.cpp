#include "stdafx.h"

#include "Headers/IntersectionRecord.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

IntersectionRecord::IntersectionRecord() noexcept
  : _intersectedObject1(nullptr),
    _intersectedObject2(nullptr),
    _distance(std::numeric_limits<D64>::max()),
    _hasHit(false)
{
}

IntersectionRecord::IntersectionRecord(vec3<F32> hitPos,
                                       vec3<F32> hitNormal,
                                       Ray ray,
                                       const D64 distance) noexcept
    : _position(MOV(hitPos)),
      _normal(MOV(hitNormal)),
      _ray(MOV(ray)),
      _intersectedObject1(nullptr),
      _intersectedObject2(nullptr),
      _distance(distance),
      _hasHit(true)
{
}

/// Creates a new intersection record indicating whether there was a hit or not and the object which was hit.
IntersectionRecord::IntersectionRecord(SceneGraphNode* hitObject)  noexcept :
    _intersectedObject1(hitObject),
    _intersectedObject2(nullptr),
    _distance(std::numeric_limits<D64>::max()),
    _hasHit(hitObject != nullptr)
{
}

void IntersectionRecord::reset() noexcept
{
    _ray.identity();
    _hasHit = false;
    _distance = std::numeric_limits<D64>::max();
    _intersectedObject1 = nullptr;
    _intersectedObject2 = nullptr;
}

bool IntersectionRecord::operator==(const IntersectionRecord& otherRecord) const noexcept
{
    const SceneGraphNode* node11 = _intersectedObject1;
    const SceneGraphNode* node12 = _intersectedObject2;
    const SceneGraphNode* node21 = otherRecord._intersectedObject1;
    const SceneGraphNode* node22 = otherRecord._intersectedObject2;

    if (node11 && node12 && node21 && node22) {
        if (node21->getGUID() == node11->getGUID() && node22->getGUID() == node12->getGUID()) {
            return true;
        }
        if (node21->getGUID() == node12->getGUID() && node22->getGUID() == node11->getGUID()) {
            return true;
        }
    }

    return false;

}
};