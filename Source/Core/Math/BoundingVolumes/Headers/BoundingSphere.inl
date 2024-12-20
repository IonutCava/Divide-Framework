/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_INL_
#define DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_INL_

namespace Divide
{

inline void BoundingSphere::fromBoundingBox(const BoundingBox& bBox) noexcept
{
    _sphere.center = bBox.getCenter();
    _sphere.radius = (bBox._max - _sphere.center).length();
}

inline void BoundingSphere::fromBoundingSphere(const BoundingSphere& bSphere) noexcept
{
    _sphere = bSphere._sphere;
}

// https://code.google.com/p/qe3e/source/browse/trunk/src/BoundingSphere.h?r=28
inline void BoundingSphere::add(const BoundingSphere& bSphere) noexcept
{
    const F32 dist = (bSphere._sphere.center - _sphere.center).length();

    if (_sphere.radius >= dist + bSphere._sphere.radius)
    {
        return;
    }

    if (bSphere._sphere.radius >= dist + _sphere.radius)
    {
        _sphere = bSphere._sphere;
    }

    if (dist > EPSILON_F32)
    {
        const F32 nRadius = (_sphere.radius + dist + bSphere._sphere.radius) * 0.5f;
        const F32 ratio = (nRadius - _sphere.radius) / dist;
        _sphere.center += (bSphere._sphere.center - _sphere.center) * ratio;

        _sphere.radius = nRadius;
    }
}

inline void BoundingSphere::addRadius(const BoundingSphere& bSphere) noexcept
{
    const F32 dist = (bSphere._sphere.center - _sphere.center).length() + bSphere._sphere.radius;
    if (_sphere.radius < dist)
    {
        _sphere.radius = dist;
    }
}

inline void BoundingSphere::add(const float3& point) noexcept
{
    const float3 diff(point - _sphere.center);
    const F32 dist = diff.length();
    if (_sphere.radius < dist)
    {
        const F32 nRadius = (dist - _sphere.radius) * 0.5f;
        _sphere.center += diff * (nRadius / dist);
        _sphere.radius += nRadius;
    }
}

inline void BoundingSphere::addRadius(const float3& point) noexcept
{
    const F32 dist = (point - _sphere.center).length();
    if (_sphere.radius < dist)
    {
        _sphere.radius = dist;
    }
}

inline void BoundingSphere::createFromPoints(const vector<float3>& points) noexcept
{
    _sphere.radius = 0.f;
    const F32 numPoints = to_F32(points.size());

    for (const float3& p : points)
    {
        _sphere.center += p / numPoints;
    }

    for (const float3& p : points)
    {
        const F32 distance = (p - _sphere.center).length();

        if (distance > _sphere.radius)
        {
            _sphere.radius = distance;
        }
    }
}

inline void BoundingSphere::createFromPoints(const std::array<float3, 8>& points) noexcept
{
    _sphere.radius = 0.f;

    for (const float3& p : points)
    {
        _sphere.center += p / 8;
    }

    for (const float3& p : points)
    {
        const F32 distance = (p - _sphere.center).length();

        if (distance > _sphere.radius)
        {
            _sphere.radius = distance;
        }
    }
}

inline void BoundingSphere::reset() noexcept
{
    _sphere = VECTOR4_ZERO;
}

inline void BoundingSphere::setRadius(const F32 radius) noexcept
{
    _sphere.radius = radius;
}

inline void BoundingSphere::setCenter(const float3& center) noexcept
{
    _sphere.center = center;
}

inline F32 BoundingSphere::getDistanceFromPoint(const float3& point) const noexcept
{
    return _sphere.center.distance(point) - _sphere.radius;
}

inline F32 BoundingSphere::getDistanceSQFromPoint(const float3& point) const noexcept
{
    // If this is negative, than the sphere contains the point, so we clamp min distance
    return std::max(_sphere.center.distanceSquared(point) - SQUARED(_sphere.radius), 0.f);
}

}  // namespace Divide

#endif  //DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_INL_
