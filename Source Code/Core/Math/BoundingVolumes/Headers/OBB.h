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

#pragma once
#ifndef _CORE_MATH_BOUNDINGVOLUMES_OOBB_H_
#define _CORE_MATH_BOUNDINGVOLUMES_OOBB_H_

#include "Core/Math/Headers/Ray.h"

//ref: https://github.com/juj/MathGeoLib
namespace Divide {

struct LineSegment
{
    vec3<F32> _start;
    vec3<F32> _end;
};

class BoundingSphere;
class BoundingBox;
class OBB {
public:
    using OBBAxis = std::array<vec3<F32>, 3>;
    using OOBBEdgeList = std::array<LineSegment, 12>;

    OBB() = default;
    explicit OBB(vec3<F32> pos, vec3<F32> hExtents, OBBAxis axis)  noexcept;
    explicit OBB(const BoundingBox &aabb)  noexcept;
    explicit OBB(const BoundingSphere &bSphere)  noexcept;

    void fromBoundingBox(const BoundingBox& aabb) noexcept;
    void fromBoundingBox(const BoundingBox& aabb, const mat4<F32>& worldMatrix);
    void fromBoundingBox(const BoundingBox& aabb, const Quaternion<F32>& orientation);
    void fromBoundingBox(const BoundingBox& aabb, const vec3<F32>& position, const Quaternion<F32>& rotation, const vec3<F32>& scale);
    void fromBoundingSphere(const BoundingSphere &sphere)  noexcept;

    void translate(const vec3<F32>& offset);
    /// Uniform scaling
    void scale(const vec3<F32>& centerPoint, F32 scaleFactor);
    /// Non-uniform scaling
    void scale(const vec3<F32>& centerPoint, const vec3<F32>& scaleFactor);
    void transform(const mat3<F32>& transform);
    void transform(const mat4<F32>& transform);
    void transform(const Quaternion<F32>& rotation);

    [[nodiscard]] BoundingBox toBoundingBox() const noexcept;

    [[nodiscard]] BoundingSphere toEnclosingSphere() const noexcept;
    [[nodiscard]] BoundingSphere toEnclosedSphere() const noexcept;

    [[nodiscard]] F32 distance(const vec3<F32>& point) const noexcept;
    [[nodiscard]] vec3<F32> closestPoint(const vec3<F32>& point) const noexcept;
    [[nodiscard]] vec3<F32> cornerPoint(U8 cornerIndex) const noexcept;
    [[nodiscard]] vec3<F32> size() const noexcept;
    [[nodiscard]] vec3<F32> diagonal() const noexcept;
    [[nodiscard]] vec3<F32> halfDiagonal() const noexcept;
    [[nodiscard]] LineSegment edge(U8 edgeIndex) const noexcept;
    [[nodiscard]] OOBBEdgeList edgeList() const noexcept;

    [[nodiscard]] bool containsPoint(const vec3<F32>& point) const noexcept;
    [[nodiscard]] bool containsBox(const OBB& OBB) const noexcept;
    [[nodiscard]] bool containsBox(const BoundingBox& AABB) const noexcept;
    [[nodiscard]] bool containsSphere(const BoundingSphere& bSphere) const noexcept;

    [[nodiscard]] RayResult intersect(const Ray& ray, F32 t0In, F32 t1In) const noexcept;

    PROPERTY_RW(vec3<F32>, position, VECTOR3_ZERO);
    PROPERTY_RW(vec3<F32>, halfExtents, VECTOR3_UNIT);
    PROPERTY_RW(OBBAxis, axis, {});
};

}  // namespace Divide

#endif  //_CORE_MATH_BOUNDINGVOLUMES_OOBB_H_