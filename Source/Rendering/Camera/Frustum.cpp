

#include "Headers/Frustum.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"

namespace Divide {

FrustumCollision PlanePointIntersect(const Plane<F32>& plane, const float3& point) noexcept
{
    switch (plane.classifyPoint(point))
    {
        case Plane<F32>::Side::NO_SIDE      : return FrustumCollision::FRUSTUM_INTERSECT;
        case Plane<F32>::Side::NEGATIVE_SIDE: return FrustumCollision::FRUSTUM_OUT;
        case Plane<F32>::Side::POSITIVE_SIDE: break;
        default: break;
    }

    return FrustumCollision::FRUSTUM_IN;
}

FrustumCollision Frustum::ContainsPoint(const float3& point, I8& lastPlaneCache) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;
    I8 planeToSkip = -1;
    if (lastPlaneCache != -1)
    {
        res = Divide::PlanePointIntersect(_frustumPlanes[lastPlaneCache], point);
        if (res == FrustumCollision::FRUSTUM_IN)
        {
            // reset cache if it's no longer valid
            planeToSkip = lastPlaneCache;
            lastPlaneCache = -1;
        }
    }

    // Cache miss: check all planes
    if (lastPlaneCache == -1)
    {
        for (I8 i = 0; i < to_I8(FrustumPlane::COUNT); ++i)
        {
            if (i != planeToSkip)
            {
                res = Divide::PlanePointIntersect(_frustumPlanes[i], point);
                if (res != FrustumCollision::FRUSTUM_IN)
                {
                    lastPlaneCache = i;
                    break;
                }
            }
        }
    }

    return res;
}

FrustumCollision PlaneBoundingSphereIntersect(const Plane<F32>& plane, const BoundingSphere& bsphere) noexcept
{
    return PlaneSphereIntersect(plane, bsphere._sphere.center, bsphere._sphere.radius);
}

FrustumCollision PlaneSphereIntersect(const Plane<F32>& plane, const float3& center, const F32 radius) noexcept
{
    const F32 distance = plane.signedDistanceToPoint(center);
    if (distance < -radius)
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if (std::abs(distance) < radius)
    {
        return FrustumCollision::FRUSTUM_INTERSECT;
    }

    return  FrustumCollision::FRUSTUM_IN;
}

FrustumCollision Frustum::ContainsSphere(const float3& center, const F32 radius, I8& lastPlaneCache) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;

    I8 planeToSkip = -1;
    if (lastPlaneCache != -1)
    {
        res = Divide::PlaneSphereIntersect(_frustumPlanes[lastPlaneCache], center, radius);
        if (res == FrustumCollision::FRUSTUM_IN)
        {
            // reset cache if it's no longer valid
            planeToSkip = lastPlaneCache;
            lastPlaneCache = -1;
        }
    }

    // Cache miss: check all planes
    if (lastPlaneCache == -1)
    {
        for (I8 i = 0; i < to_I8(FrustumPlane::COUNT); ++i) 
        {
            if (i != planeToSkip)
            {
                res = Divide::PlaneSphereIntersect(_frustumPlanes[i], center, radius);
                if (res != FrustumCollision::FRUSTUM_IN)
                {
                    lastPlaneCache = i;
                    break;
                }
            }
        }
    }

    return res;
}

FrustumCollision Frustum::PlaneBoundingBoxIntersect(const FrustumPlane frustumPlane, const BoundingBox& bbox) const noexcept
{
    return Divide::PlaneBoundingBoxIntersect(_frustumPlanes[to_base(frustumPlane)], bbox);
}

FrustumCollision Frustum::PlaneBoundingSphereIntersect(FrustumPlane frustumPlane, const BoundingSphere& bsphere) const noexcept
{
    return Divide::PlaneBoundingSphereIntersect(_frustumPlanes[to_base(frustumPlane)], bsphere);
}

FrustumCollision Frustum::PlanePointIntersect(const FrustumPlane frustumPlane, const float3& point) const noexcept
{
    return Divide::PlanePointIntersect(_frustumPlanes[to_base(frustumPlane)], point);
}

FrustumCollision Frustum::PlaneSphereIntersect(const FrustumPlane frustumPlane, const float3& center, const F32 radius) const noexcept
{
    return Divide::PlaneSphereIntersect(_frustumPlanes[to_base(frustumPlane)], center, radius);
}

FrustumCollision Frustum::PlaneBoundingBoxIntersect(const FrustumPlane* frustumPlanes, const U8 count, const BoundingBox& bbox) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;

    for (U8 i = 0u; i < count; ++i)
    {
        res = Divide::PlaneBoundingBoxIntersect(_frustumPlanes[to_base(frustumPlanes[i])], bbox);
        if (res != FrustumCollision::FRUSTUM_IN)
        {
            break;
        }
    }

    return res;
}

FrustumCollision Frustum::PlaneBoundingSphereIntersect(const FrustumPlane* frustumPlanes, U8 count, const BoundingSphere& bsphere) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;

    for (U8 i = 0u; i < count; ++i)
    {
        res = Divide::PlaneBoundingSphereIntersect(_frustumPlanes[to_base(frustumPlanes[i])], bsphere);
        if (res != FrustumCollision::FRUSTUM_IN)
        {
            break;
        }
    }

    return res;
}

FrustumCollision Frustum::PlanePointIntersect(const FrustumPlane* frustumPlanes, const U8 count, const float3& point) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;

    for (U8 i = 0u; i < count; ++i)
    {
        res = Divide::PlanePointIntersect(_frustumPlanes[to_base(frustumPlanes[i])], point);
        if (res != FrustumCollision::FRUSTUM_IN)
        {
            break;
        }
    }

    return res;
}

FrustumCollision Frustum::PlaneSphereIntersect(const FrustumPlane* frustumPlanes, const U8 count, const float3& center, const F32 radius) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;

    for (U8 i = 0u; i < count; ++i)
    {
        res = Divide::PlaneSphereIntersect(_frustumPlanes[to_base(frustumPlanes[i])], center, radius);
        if (res != FrustumCollision::FRUSTUM_IN)
        {
            break;
        }
    }

    return res;
}

FrustumCollision PlaneBoundingBoxIntersect(const Plane<F32>& plane, const BoundingBox& bbox) noexcept
{
    if (plane.signedDistanceToPoint(bbox.getPVertex(plane._normal)) < 0)
    {
        return FrustumCollision::FRUSTUM_OUT;
    }

    if (plane.signedDistanceToPoint(bbox.getNVertex(plane._normal)) < 0)
    {
        return FrustumCollision::FRUSTUM_INTERSECT;
    }

    return FrustumCollision::FRUSTUM_IN;
}

FrustumCollision Frustum::ContainsSphere(const BoundingSphere& bSphere, I8& lastPlaneCache) const noexcept
{
    return ContainsSphere(bSphere._sphere.center, bSphere._sphere.radius, lastPlaneCache);
}

FrustumCollision Frustum::ContainsBoundingBox(const BoundingBox& bbox, I8& lastPlaneCache) const noexcept
{
    FrustumCollision res = FrustumCollision::FRUSTUM_IN;
    I8 planeToSkip = -1;
    if (lastPlaneCache != -1)
    {
        res = Divide::PlaneBoundingBoxIntersect(_frustumPlanes[lastPlaneCache], bbox);
        if (res == FrustumCollision::FRUSTUM_IN)
        {
            // reset cache if it's no longer valid
            planeToSkip = lastPlaneCache;
            lastPlaneCache = -1;
        }
    }

    // Cache miss: check all planes
    if (lastPlaneCache == -1)
    {
        for (I8 i = 0; i < to_I8(FrustumPlane::COUNT); ++i)
        {
            if (i != planeToSkip)
            {
                res = Divide::PlaneBoundingBoxIntersect(_frustumPlanes[i], bbox);
                if (res != FrustumCollision::FRUSTUM_IN)
                {
                    lastPlaneCache = i;
                    break;
                }
            }
        }
    }

    return res;
}

void Frustum::set(const Frustum& other) noexcept
{
    _frustumPlanes = other._frustumPlanes;
}

// Get the frustum corners in WorldSpace.
void Frustum::getCornersWorldSpace(std::array<float3, to_base(FrustumPoints::COUNT)>& cornersWS) const noexcept
{
    const Plane<F32>& leftPlane   = _frustumPlanes[to_base(FrustumPlane::PLANE_LEFT)];
    const Plane<F32>& rightPlane  = _frustumPlanes[to_base(FrustumPlane::PLANE_RIGHT)];
    const Plane<F32>& nearPlane   = _frustumPlanes[to_base(FrustumPlane::PLANE_NEAR)];
    const Plane<F32>& farPlane    = _frustumPlanes[to_base(FrustumPlane::PLANE_FAR)];
    const Plane<F32>& topPlane    = _frustumPlanes[to_base(FrustumPlane::PLANE_TOP)];
    const Plane<F32>& bottomPlane = _frustumPlanes[to_base(FrustumPlane::PLANE_BOTTOM)];

    cornersWS[to_base(FrustumPoints::NEAR_LEFT_TOP)]     = GetIntersection(nearPlane, leftPlane,  topPlane);
    cornersWS[to_base(FrustumPoints::NEAR_RIGHT_TOP)]    = GetIntersection(nearPlane, rightPlane, topPlane);
    cornersWS[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]  = GetIntersection(nearPlane, leftPlane,  bottomPlane);
    cornersWS[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)] = GetIntersection(nearPlane, rightPlane, bottomPlane);
    cornersWS[to_base(FrustumPoints::FAR_LEFT_TOP)]      = GetIntersection(farPlane,  leftPlane,  topPlane);
    cornersWS[to_base(FrustumPoints::FAR_RIGHT_TOP)]     = GetIntersection(farPlane,  rightPlane, topPlane);
    cornersWS[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]   = GetIntersection(farPlane,  leftPlane,  bottomPlane);
    cornersWS[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]  = GetIntersection(farPlane,  rightPlane, bottomPlane);
}

const std::array<Plane<F32>, to_base(FrustumPlane::COUNT)>& Frustum::computePlanes(const mat4<F32>& viewProjMatrix)
{
    F32* leftPlane   = _frustumPlanes[to_base(FrustumPlane::PLANE_LEFT)]._equation._v;
    F32* rightPlane  = _frustumPlanes[to_base(FrustumPlane::PLANE_RIGHT)]._equation._v;
    F32* nearPlane   = _frustumPlanes[to_base(FrustumPlane::PLANE_NEAR)]._equation._v;
    F32* farPlane    = _frustumPlanes[to_base(FrustumPlane::PLANE_FAR)]._equation._v;
    F32* topPlane    = _frustumPlanes[to_base(FrustumPlane::PLANE_TOP)]._equation._v;
    F32* bottomPlane = _frustumPlanes[to_base(FrustumPlane::PLANE_BOTTOM)]._equation._v;

    const auto& mat = viewProjMatrix.m;

    for (I8 i = 4; i--; ) { leftPlane[i]   = mat[i][3] + mat[i][0]; }
    for (I8 i = 4; i--; ) { rightPlane[i]  = mat[i][3] - mat[i][0]; }
    for (I8 i = 4; i--; ) { bottomPlane[i] = mat[i][3] + mat[i][1]; }
    for (I8 i = 4; i--; ) { topPlane[i]    = mat[i][3] - mat[i][1]; }
    for (I8 i = 4; i--; ) { nearPlane[i]   = mat[i][3] + mat[i][2]; }
    for (I8 i = 4; i--; ) { farPlane[i]    = mat[i][3] - mat[i][2]; }

    for (Plane<F32>& plane : _frustumPlanes)
    {
        plane.normalize();
    }

    return _frustumPlanes;
}

} //namespace Divide
