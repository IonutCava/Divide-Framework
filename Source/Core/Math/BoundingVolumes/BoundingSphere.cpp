

#include "Headers/BoundingSphere.h"
#include "Headers/OBB.h"

namespace Divide
{

BoundingSphere::BoundingSphere(const float3& center, const F32 radius) noexcept
    : _sphere(center, radius)
{
}

BoundingSphere::BoundingSphere(const vector<float3>& points) noexcept
{
    createFromPoints(points);
}

BoundingSphere::BoundingSphere(const std::array<float3, 8>& points) noexcept
{
    createFromPoints(points);
}

bool BoundingSphere::containsBoundingBox(const BoundingBox& AABB) const noexcept
{
    bool inside = true;
    std::array<float3, 8> points = AABB.getPoints();

    for (U8 i = 0; i < 8u; ++i)
    {
        if (containsPoint(points[i]))
        {
            inside = false;
            break;
        }
    }

    return inside;
}

bool BoundingSphere::containsPoint(const float3& point) const noexcept
{
    const F32 distanceSQ = _sphere.center.distanceSquared(point);
    return distanceSQ <= _sphere.radius * _sphere.radius;
}

bool BoundingSphere::collision(const BoundingSphere& sphere2) const noexcept
{
    F32 radiusSq = _sphere.radius + sphere2._sphere.radius;
    radiusSq *= radiusSq;
    return _sphere.center.distanceSquared(sphere2._sphere.center) <= radiusSq;
}

RayResult BoundingSphere::intersect(const IntersectionRay& r, const F32 tMin, const F32 tMax) const noexcept
{
    const float3& p = r._origin;
    const float3& d = r._direction;

    const float3 m = p - _sphere.center;
    const F32 b = Dot(m, d);
    const F32 c = Dot(m, m) - SQUARED(_sphere.radius);

    // Exit if r's origin outside s (c > 0) and r pointing away from s (b > 0) 
    if (c > 0.f && b > 0.f)
    {
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
    ret.dist = -b - Sqrt<F32>(discr);
    
    if (ret.dist < 0.f)
    {
        ret.hit = true;
        ret.dist = 0.f;
    }
    else
    {
        ret.hit = IS_IN_RANGE_INCLUSIVE(ret.dist, tMin, tMax);
    }

    return ret;
}

void BoundingSphere::fromOBB(const OBB& box) noexcept
{
    *this = box.toEnclosingSphere();
}

}  // namespace Divide
