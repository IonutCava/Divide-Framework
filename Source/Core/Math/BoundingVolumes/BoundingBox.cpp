

#include "Headers/BoundingBox.h"
#include "Headers/OBB.h"
#include "Headers/BoundingSphere.h"

namespace Divide {

BoundingBox::BoundingBox(const float3& min, const float3& max) noexcept
    : _min(min),
      _max(max)
{
}

BoundingBox::BoundingBox(const F32 minX, const F32 minY, const F32 minZ, const F32 maxX, const F32 maxY, const F32 maxZ) noexcept
    : _min(minX, minY, minZ),
      _max(maxX, maxY, maxZ)
{
}

BoundingBox::BoundingBox(std::span<const float3> points) noexcept
{
    createFromPoints(points);
}

BoundingBox::BoundingBox(const OBB& obb) noexcept
{
    createFromOBB(obb);
}

BoundingBox::BoundingBox(const BoundingSphere& bSphere) noexcept
{
    createFromSphere(bSphere);
}

BoundingBox::BoundingBox(const BoundingBox& b) noexcept
    : _min(b._min)
    , _max(b._max)
{
}

BoundingBox& BoundingBox::operator=(const BoundingBox& b) noexcept
{
    _min.set(b._min);
    _max.set(b._max);
    return *this;
}

void BoundingBox::createFromSphere(const BoundingSphere& bSphere) noexcept
{
    createFromSphere(bSphere._sphere.center, bSphere._sphere.radius);
}

void BoundingBox::createFromOBB(const OBB& obb) noexcept
{
    const float3 halfSize = Abs(obb.axis()[0] * obb.halfExtents()[0]) +
                            Abs(obb.axis()[1] * obb.halfExtents()[1]) +
                            Abs(obb.axis()[2] * obb.halfExtents()[2]);

    createFromCenterAndSize(obb.position(), halfSize * 2);
}

bool BoundingBox::containsSphere(const BoundingSphere& bSphere) const noexcept
{
    return containsSphere(bSphere._sphere.center, bSphere._sphere.radius);
}

bool BoundingBox::collision(const BoundingBox& AABB2) const noexcept
{
    return collision(AABB2.getCenter(), AABB2.getHalfExtent());
}

bool BoundingBox::collision(const BoundingSphere& bSphere) const noexcept
{
    return collision(bSphere._sphere.center, bSphere._sphere.radius);
}

///https://tavianator.com/cgit/dimension.git/tree/libdimension/bvh/bvh.c#n196
RayResult BoundingBox::intersect(const IntersectionRay& r, F32 t0, F32 t1) const noexcept
{
    F32 tmin = 0.f, tmax = F32_INFINITY;

    for (U8 d = 0u; d < 3u; ++d)
    {
        const bool sign  = r._signbit[d];
        const F32 origin = r._origin[d];
        const F32 invDir = r._invDirection[d];
        const F32 bmin   = _corners[ sign][d];
        const F32 bmax   = _corners[!sign][d];

        const F32 dmin = (bmin - origin) * invDir;
        const F32 dmax = (bmax - origin) * invDir;

        tmin = std::max(dmin, tmin);
        tmax = std::min(dmax, tmax);
    }

    return RayResult
    {
        .hit = tmin < 0.f || IS_IN_RANGE_INCLUSIVE(tmin, t0, t1),
        .inside = tmin < 0.f,
        .dist = tmin < 0.f ? tmax : tmin
    };
}

}  // namespace Divide
