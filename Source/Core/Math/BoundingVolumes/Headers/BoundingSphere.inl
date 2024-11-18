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

namespace Divide {

inline void BoundingSphere::fromBoundingBox(const BoundingBox& bBox) noexcept {
    _center.set(bBox.getCenter());
    _radius = (bBox.getMax() - _center).length();
}

inline void BoundingSphere::fromBoundingSphere(const BoundingSphere& bSphere) noexcept {
    _center.set(bSphere.getCenter());
    _radius = bSphere.getRadius();
}

// https://code.google.com/p/qe3e/source/browse/trunk/src/BoundingSphere.h?r=28
inline void BoundingSphere::add(const BoundingSphere& bSphere) noexcept {
    const F32 dist = (bSphere._center - _center).length();

    if (_radius >= dist + bSphere._radius) {
        return;
    }

    if (bSphere._radius >= dist + _radius) {
        _center = bSphere._center;
        _radius = bSphere._radius;
    }

    if (dist > EPSILON_F32) {
        const F32 nRadius = (_radius + dist + bSphere._radius) * 0.5f;
        const F32 ratio = (nRadius - _radius) / dist;
        _center += (bSphere._center - _center) * ratio;

        _radius = nRadius;
    }
}

inline void BoundingSphere::addRadius(const BoundingSphere& bSphere) noexcept {
    const F32 dist = (bSphere._center - _center).length() + bSphere._radius;
    if (_radius < dist) {
        _radius = dist;
    }
}

inline void BoundingSphere::add(const float3& point) noexcept {
    const float3 diff(point - _center);
    const F32 dist = diff.length();
    if (_radius < dist) {
        const F32 nRadius = (dist - _radius) * 0.5f;
        _center += diff * (nRadius / dist);
        _radius += nRadius;
    }
}

inline void BoundingSphere::addRadius(const float3& point) noexcept {
    const F32 dist = (point - _center).length();
    if (_radius < dist) {
        _radius = dist;
    }
}

inline void BoundingSphere::createFromPoints(const vector<float3>& points) noexcept {
    _radius = 0;
    const F32 numPoints = to_F32(points.size());

    for (const float3& p : points) {
        _center += p / numPoints;
    }

    for (const float3& p : points) {
        const F32 distance = (p - _center).length();

        if (distance > _radius) {
            _radius = distance;
        }
    }
}

inline void BoundingSphere::createFromPoints(const std::array<float3, 8>& points) noexcept {
    _radius = 0;

    for (const float3& p : points) {
        _center += p / 8;
    }

    for (const float3& p : points) {
        const F32 distance = (p - _center).length();

        if (distance > _radius) {
            _radius = distance;
        }
    }
}

inline void BoundingSphere::reset() noexcept {
    _center.reset();
    _radius = 0.0f;
}

inline void BoundingSphere::setRadius(const F32 radius) noexcept {
    _radius = radius;
}

inline void BoundingSphere::setCenter(const float3& center) noexcept {
    _center = center;
}

inline const float3& BoundingSphere::getCenter() const noexcept { return _center; }

inline F32 BoundingSphere::getRadius() const noexcept { return _radius; }

inline F32 BoundingSphere::getDiameter() const noexcept { return _radius * 2; }

inline F32 BoundingSphere::getDistanceFromPoint(const float3& point) const noexcept
{
    return getCenter().distance(point) - getRadius();
}
inline F32 BoundingSphere::getDistanceSQFromPoint(const float3& point) const noexcept
{
    // If this is negative, than the sphere contains the point, so we clamp min distance
    return std::max(getCenter().distanceSquared(point) - SQUARED(getRadius()), 0.f);
}
inline float4 BoundingSphere::asVec4() const noexcept
{
    return float4(getCenter(), getRadius());
}
}  // namespace Divide

#endif  //DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_INL_
