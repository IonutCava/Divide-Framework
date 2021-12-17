#include "stdafx.h"

#include "Headers/BoundingBox.h"
#include "Headers/OBB.h"
#include "Headers/BoundingSphere.h"

namespace Divide {

BoundingBox::BoundingBox() noexcept
    : _min(std::numeric_limits<F32>::max()),
      _max(std::numeric_limits<F32>::lowest())
{
}

BoundingBox::BoundingBox(vec3<F32> min, vec3<F32> max) noexcept
    : _min(MOV(min)),
      _max(MOV(max))
{
}

BoundingBox::BoundingBox(const F32 minX, const F32 minY, const F32 minZ, const F32 maxX, const F32 maxY, const F32 maxZ) noexcept
    : _min(minX, minY, minZ),
      _max(maxX, maxY, maxZ)
{
}

BoundingBox::BoundingBox(const vector<vec3<F32>>& points) noexcept
    : BoundingBox()
{
    createFromPoints(points);
}

BoundingBox::BoundingBox(const std::array<vec3<F32>, 8>& points) noexcept
    : BoundingBox()
{
    createFromPoints(points);
}
BoundingBox::BoundingBox(const OBB& obb) noexcept
    : BoundingBox()
{
    createFromOBB(obb);
}

BoundingBox::BoundingBox(const BoundingSphere& bSphere) noexcept
    : BoundingBox()
{
    createFromSphere(bSphere);
}

BoundingBox::BoundingBox(const BoundingBox& b) noexcept {
    this->_min.set(b._min);
    this->_max.set(b._max);
}

BoundingBox& BoundingBox::operator=(const BoundingBox& b) noexcept {
    this->_min.set(b._min);
    this->_max.set(b._max);
    return *this;
}

void BoundingBox::createFromSphere(const BoundingSphere& bSphere) noexcept {
    createFromSphere(bSphere.getCenter(), bSphere.getRadius());
}

void BoundingBox::createFromCenterAndSize(const vec3<F32>& center, const vec3<F32>& size) noexcept {
    const vec3<F32> halfSize = 0.5f * size;
    setMin(center - halfSize);
    setMax(center + halfSize);
}

void BoundingBox::createFromOBB(const OBB& obb) noexcept
{
    const vec3<F32> halfSize = Abs(obb.axis()[0] * obb.halfExtents()[0]) +
                               Abs(obb.axis()[1] * obb.halfExtents()[1]) +
                               Abs(obb.axis()[2] * obb.halfExtents()[2]);

    createFromCenterAndSize(obb.position(), halfSize * 2);
}

bool BoundingBox::containsBox(const BoundingBox& AABB2) const noexcept {
    return AABB2._min >= _min && AABB2._max <= _max;
}

bool BoundingBox::containsSphere(const BoundingSphere& bSphere) const noexcept {
    const vec3<F32>& center = bSphere.getCenter();
    const F32 radius = bSphere.getRadius();

    return center.x - _min.x > radius &&
           center.y - _min.y > radius &&
           center.z - _min.z > radius &&
           _max.x - center.x > radius &&
           _max.y - center.y > radius &&
           _max.z - center.z > radius;
}

bool BoundingBox::collision(const BoundingBox& AABB2) const noexcept {
    const vec3<F32>& center = this->getCenter();
    const vec3<F32>& halfWidth = this->getHalfExtent();
    const vec3<F32>& otherCenter = AABB2.getCenter();
    const vec3<F32>& otherHalfWidth = AABB2.getHalfExtent();

    return std::abs(center.x - otherCenter.x) <= halfWidth.x + otherHalfWidth.x &&
           std::abs(center.y - otherCenter.y) <= halfWidth.y + otherHalfWidth.y &&
           std::abs(center.z - otherCenter.z) <= halfWidth.z + otherHalfWidth.z;
}

bool BoundingBox::collision(const BoundingSphere& bSphere) const noexcept {
    const vec3<F32>& center = bSphere.getCenter();
    const vec3<F32>& min(getMin());
    const vec3<F32>& max(getMax());

    F32 dmin = 0;
    for (U8 i = 0; i < 3; ++i) {
        if (center[i] < min[i]) {
            dmin += SQUARED(center[i] - min[i]);
        } else if (center[i] > max[i]) {
            dmin += SQUARED(center[i] - max[i]);
        }
    }

    return dmin <= SQUARED(bSphere.getRadius());
}

/// Optimized method: http://www.cs.utah.edu/~awilliam/box/box.pdf
RayResult BoundingBox::intersect(const Ray& r, F32 t0, F32 t1) const noexcept {
    const vec3<F32> bounds[] = {_min, _max};

    Ray::CollisionHelpers colHelpers = r.getCollisionHelpers();
    const vec3<F32> origin = r._origin;

    F32 t_min = (bounds[colHelpers._sign[0]].x - origin.x) * colHelpers._invDirection.x;
    F32 t_max = (bounds[1 - colHelpers._sign[0]].x - origin.x) * colHelpers._invDirection.x;
    const F32 ty_min = (bounds[colHelpers._sign[1]].y - origin.y) * colHelpers._invDirection.y;
    const F32 ty_max = (bounds[1 - colHelpers._sign[1]].y - origin.y) * colHelpers._invDirection.y;

    if (t_min > ty_max || ty_min > t_max) {
        return { false, (t_min >= 0.0f ? t_min : t_max) };
    }

    if (ty_min > t_min) {
         t_min = ty_min;
    }

    if (ty_max < t_max) {
        t_max = ty_max;
    }

    const F32 tz_min = (bounds[colHelpers._sign[2]].z - origin.z) * colHelpers._invDirection.z;
    const F32 tz_max = (bounds[1 - colHelpers._sign[2]].z - origin.z) * colHelpers._invDirection.z;

    if (t_min > tz_max || tz_min > t_max) {
        return { false, (t_min >= 0.0f ? t_min : t_max) };
    }

    if (tz_min > t_min) {
        t_min = tz_min;
    }

    if (tz_max < t_max) {
        t_max = tz_max;
    }

    const F32 t = t_min < 0.f ? t_max : t_min;

    RayResult ret;
    ret.dist = t;
    // Ray started inside the box
    if (ret.dist < 0.0f) {
        ret.hit = true;
        ret.dist = 0.0f;
    } else {
        ret.hit = IS_IN_RANGE_INCLUSIVE(t, t0, t1);
    }
    return ret;
}

void BoundingBox::transform(const mat4<F32>& mat) noexcept {
    transform(getMin(), getMax(), mat);
}

void BoundingBox::transform(const BoundingBox& initialBoundingBox, const mat4<F32>& mat) noexcept {
    transform(initialBoundingBox.getMin(), initialBoundingBox.getMax(), mat);
}

void BoundingBox::transform(vec3<F32> initialMin, vec3<F32> initialMax, const mat4<F32>& mat) noexcept {
    _min = _max = mat.getTranslation<F32>();

    for (U8 i = 0u; i < 3u; ++i) {
        F32& min = _min[i];
        F32& max = _max[i];

        for (U8 j = 0; j < 3; ++j) {
            const F32 a = mat.m[j][i] * initialMin[j];
            const F32 b = mat.m[j][i] * initialMax[j];  // Transforms are usually row major

            if (a < b) {
                min += a;
                max += b;
            } else {
                min += b;
                max += a;
            }
        }
    }
}

}  // namespace Divide