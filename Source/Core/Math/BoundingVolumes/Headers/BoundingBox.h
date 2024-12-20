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
#ifndef DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_
#define DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_

#include "Core/Math/Headers/Ray.h"

namespace Divide
{

class OBB;
class BoundingSphere;
class BoundingBox
{
   public:
    BoundingBox() = default;

    explicit BoundingBox(const OBB& obb) noexcept;
    explicit BoundingBox(const BoundingSphere& bSphere) noexcept;
    explicit BoundingBox(const float3& min, const float3& max) noexcept;
    explicit BoundingBox(std::span<const float3> points) noexcept;
    explicit BoundingBox(F32 minX, F32 minY, F32 minZ, F32 maxX, F32 maxY, F32 maxZ) noexcept;

    BoundingBox(const BoundingBox& b) noexcept;
    BoundingBox& operator=(const BoundingBox& b) noexcept;

    [[nodiscard]] bool containsPoint(const float3& point) const noexcept;
    [[nodiscard]] bool containsAABB(const BoundingBox& AABB2) const noexcept;
    [[nodiscard]] bool containsAABB(const float3& min, const float3& max) const noexcept;
    [[nodiscard]] bool containsSphere(const BoundingSphere& bSphere) const noexcept;
    [[nodiscard]] bool containsSphere(const float3& center, F32 radius) const noexcept;

    [[nodiscard]] bool collision(const BoundingBox& AABB2) const  noexcept;
    [[nodiscard]] bool collision(const float3& min, const float3& halfExtent) const noexcept;
    [[nodiscard]] bool collision(const BoundingSphere& bSphere) const noexcept;
    [[nodiscard]] bool collision(const float3& center, F32 radius) const noexcept;

    [[nodiscard]] bool compare(const BoundingBox& bb) const noexcept;
    [[nodiscard]] bool operator==(const BoundingBox& B) const noexcept;
    [[nodiscard]] bool operator!=(const BoundingBox& B) const noexcept;

    /// Optimized method
    [[nodiscard]] RayResult intersect(const IntersectionRay& r, F32 t0, F32 t1) const noexcept;

    void createFromPoints(std::span<const float3> points) noexcept;
    void createFromSphere(const BoundingSphere& bSphere) noexcept;
    void createFromSphere(const float3& center, F32 radius) noexcept;
    void createFromCenterAndSize(const float3& center, const float3& size) noexcept;
    void createFromOBB(const OBB& obb) noexcept;

    void add(const float3& v) noexcept;
    void add(const BoundingBox& bb) noexcept;

    void translate(const float3& v) noexcept;

    void multiply(F32 factor) noexcept;
    void multiply(const float3& v) noexcept;
    void multiplyMax(const float3& v) noexcept;
    void multiplyMin(const float3& v) noexcept;

    void transform(const float3& initialMin, const float3& initialMax, const mat4<F32>& mat) noexcept;
    void transform(const BoundingBox& initialBoundingBox, const mat4<F32>& mat) noexcept;
    void transform(const mat3<F32>& rotationMatrix) noexcept;
    void transform(const mat4<F32>& transformMatrix) noexcept;

    [[nodiscard]] float3 getCenter() const noexcept;
    [[nodiscard]] float3 getExtent() const noexcept;
    [[nodiscard]] float3 getHalfExtent() const noexcept;

    [[nodiscard]] F32 getWidth() const noexcept;
    [[nodiscard]] F32 getHeight() const noexcept;
    [[nodiscard]] F32 getDepth() const noexcept;

    void set(const BoundingBox& bb) noexcept;
    void set(const float3& min, const float3& max) noexcept;
    void setMin(const float3& min) noexcept;
    void setMax(const float3& max) noexcept;

    void set(F32 min, F32 max) noexcept;
    void set(F32 minX, F32 minY, F32 minZ, F32 maxX, F32 maxY, F32 maxZ) noexcept;
    void setMin(F32 min) noexcept;
    void setMin(F32 minX, F32 minY, F32 minZ) noexcept;
    void setMax(F32 max) noexcept;
    void setMax(F32 maxX, F32 maxY, F32 maxZ) noexcept;

    void reset() noexcept;

    [[nodiscard]] float3 cornerPoint(U8 cornerIndex) const noexcept;
    [[nodiscard]] std::array<float3, 8> getPoints() const noexcept;

    // Returns the closest point inside this AABB to the given point
    [[nodiscard]] float3 nearestPoint(const float3& pos) const noexcept;

    [[nodiscard]] inline float3 getPVertex(const float3& normal) const noexcept;
    [[nodiscard]] inline float3 getNVertex(const float3& normal) const noexcept;

   private:
    template<typename T> requires std::is_same_v<T, mat3<F32>> || std::is_same_v<T, mat4<F32>>
    void transformInternal(const float3& initialMin, const float3& initialMax, const float3 translation, const T& rotation) noexcept;

   public:
    union
    {
        struct
        {
            float3 _min;
            float3 _max;
        };

        float3 _corners[2]{ {-EPSILON_F32 * 2}, { EPSILON_F32 * 2}};
    };
};

}  // namespace Divide

#endif  //DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_

#include "BoundingBox.inl"
