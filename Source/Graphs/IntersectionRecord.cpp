

#include "Headers/IntersectionRecord.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "ECS/Components/Headers/BoundsComponent.h"

namespace Divide {

IntersectionRecord::IntersectionRecord() noexcept
  : _intersectedObject1(nullptr),
    _intersectedObject2(nullptr),
    _distance(D64_MAX),
    _hasHit(false)
{
}

IntersectionRecord::IntersectionRecord(float3 hitPos,
                                       float3 hitNormal,
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
IntersectionRecord::IntersectionRecord(BoundsComponent* hitObject)  noexcept :
    _intersectedObject1(hitObject),
    _intersectedObject2(nullptr),
    _distance( D64_MAX ),
    _hasHit(hitObject != nullptr)
{
}

void IntersectionRecord::reset() noexcept
{
    _ray.identity();
    _hasHit = false;
    _distance = D64_MAX;
    _intersectedObject1 = nullptr;
    _intersectedObject2 = nullptr;
}

bool IntersectionRecord::operator==(const IntersectionRecord& otherRecord) const noexcept
{
    const BoundsComponent* node11 = _intersectedObject1;
    const BoundsComponent* node12 = _intersectedObject2;
    const BoundsComponent* node21 = otherRecord._intersectedObject1;
    const BoundsComponent* node22 = otherRecord._intersectedObject2;

    if (node11 && node12 && node21 && node22) {
        if (node21->parentSGN()->getGUID() == node11->parentSGN()->getGUID() && node22->parentSGN()->getGUID() == node12->parentSGN()->getGUID()) {
            return true;
        }
        if (node21->parentSGN()->getGUID() == node12->parentSGN()->getGUID() && node22->parentSGN()->getGUID() == node11->parentSGN()->getGUID()) {
            return true;
        }
    }

    return false;

}
} //namespace Divide
