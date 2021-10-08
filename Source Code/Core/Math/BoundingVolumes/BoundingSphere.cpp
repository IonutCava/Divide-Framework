#include "stdafx.h"

#include "Headers/BoundingSphere.h"

namespace Divide {
BoundingSphere::BoundingSphere() noexcept
    : _radius(0.0f)
{
    _center.reset();
}

BoundingSphere::BoundingSphere(vec3<F32> center, const F32 radius) noexcept
    : _center(MOV(center)),
      _radius(radius)
{
}

BoundingSphere::BoundingSphere(const vector<vec3<F32>>& points) noexcept
    : BoundingSphere()
{
    createFromPoints(points);
}

BoundingSphere::BoundingSphere(const std::array<vec3<F32>, 8>& points) noexcept
    : BoundingSphere()
{
    createFromPoints(points);
}

bool BoundingSphere::containsBoundingBox(const BoundingBox& AABB) const noexcept {
    bool inside = true;
    std::array<vec3<F32>, 8> points = AABB.getPoints();

    for (U8 i = 0; i < 8; ++i) {
        if (containsPoint(points[i]))
        {
            inside = false;
            break;
        }
    }

    return inside;
    
}

bool BoundingSphere::containsPoint(const vec3<F32>& point) const noexcept {
    const F32 distanceSQ = _center.distanceSquared(point);
    return distanceSQ <= _radius * _radius;
}

bool BoundingSphere::collision(const BoundingSphere& sphere2) const noexcept {
    F32 radiusSq = _radius + sphere2._radius;
    radiusSq *= radiusSq;
    return _center.distanceSquared(sphere2._center) <= radiusSq;
}

RayResult BoundingSphere::intersect(const Ray& r, const F32 tMin, const F32 tMax) const noexcept {
    const vec3<F32>& p = r._origin;
    const vec3<F32>& d = r._direction;

    const vec3<F32> m = p - _center;
    const F32 b = Dot(m, d);
    const F32 c = Dot(m, m) - SQUARED(_radius);

    // Exit if r’s origin outside s (c > 0) and r pointing away from s (b > 0) 
    if (c > 0.0f && b > 0.0f) {
        return {};
    }
    const F32 discr = b * b - c;

    // A negative discriminant corresponds to ray missing sphere 
    if (discr < 0.0f) {
        return {};
    }

    // Ray now found to intersect sphere, compute smallest t value of intersection
    // If t is negative, ray started inside sphere so clamp t to zero
    RayResult ret;
    ret.dist = -b - Sqrt(discr);
    
    if (ret.dist < 0.0f) {
        ret.hit = true;
        ret.dist = 0.0f;
    } else {
        ret.hit = IS_IN_RANGE_INCLUSIVE(ret.dist, tMin, tMax);
    }
    return ret;
}

}  // namespace Divide