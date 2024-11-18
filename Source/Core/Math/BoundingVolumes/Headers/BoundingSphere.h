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
#ifndef DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_H_
#define DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_H_

#include "BoundingBox.h"

namespace Divide {
namespace Attorney {
    class BoundingSphereEditor;
}

class OBB;
class PropertyWindow;
class BoundingSphere {
    friend class Attorney::BoundingSphereEditor;

   public:
    BoundingSphere() noexcept;
    explicit BoundingSphere(float3 center, F32 radius) noexcept;
    explicit BoundingSphere(const vector<float3>& points) noexcept;
    explicit BoundingSphere(const std::array<float3, 8>& points) noexcept;

    void fromBoundingBox(const BoundingBox& bBox) noexcept;
    void fromOBB(const OBB& box) noexcept;
    void fromBoundingSphere(const BoundingSphere& bSphere) noexcept;
    [[nodiscard]] bool containsPoint(const float3& point) const noexcept;
    [[nodiscard]] bool containsBoundingBox(const BoundingBox& AABB) const noexcept;

    // https://code.google.com/p/qe3e/source/browse/trunk/src/BoundingSphere.h?r=28
    void add(const BoundingSphere& bSphere) noexcept;
    void add(const float3& point) noexcept;
    void addRadius(const BoundingSphere& bSphere) noexcept;
    void addRadius(const float3& point) noexcept;

    void createFromPoints(const vector<float3>& points) noexcept;
    void createFromPoints(const std::array<float3, 8>& points) noexcept;

    void setRadius(F32 radius) noexcept;
    void setCenter(const float3& center) noexcept;

    [[nodiscard]] const float3& getCenter() const noexcept;
    [[nodiscard]] F32 getRadius() const noexcept;
    [[nodiscard]] F32 getDiameter() const noexcept;

    [[nodiscard]] F32 getDistanceFromPoint(const float3& point) const noexcept;
    [[nodiscard]] F32 getDistanceSQFromPoint(const float3& point) const noexcept;

    void reset() noexcept;
    [[nodiscard]] float4 asVec4() const noexcept;

    [[nodiscard]] bool collision(const BoundingSphere& sphere2) const noexcept;

    [[nodiscard]] RayResult intersect(const Ray& r, F32 tMin, F32 tMax) const noexcept;


   private:
    float3 _center;
    F32 _radius;
};

namespace Attorney {
    class BoundingSphereEditor {
        static F32* center(BoundingSphere& bs) noexcept {
            return bs._center._v;
        }
        static F32& radius(BoundingSphere& bs) noexcept {
            return bs._radius;
        }
        friend class Divide::PropertyWindow;
    };
} //namespace Attorney

}  // namespace Divide

#endif  //_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGSPHERE_H_

#include "BoundingSphere.inl"
