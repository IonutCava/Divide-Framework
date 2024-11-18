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
#ifndef DVD_FRUSTUM_H_
#define DVD_FRUSTUM_H_

namespace Divide {

class Camera;
class BoundingBox;
class BoundingSphere;

class Frustum {
   public:

    void set(const Frustum& other) noexcept;

    [[nodiscard]] FrustumCollision ContainsPoint(const float3& point, I8& lastPlaneCache) const noexcept;
    [[nodiscard]] FrustumCollision ContainsBoundingBox(const BoundingBox& bbox, I8& lastPlaneCache) const noexcept;
    [[nodiscard]] FrustumCollision ContainsSphere(const BoundingSphere& bSphere, I8& lastPlaneCache) const noexcept;
    [[nodiscard]] FrustumCollision ContainsSphere(const float3& center, F32 radius, I8& lastPlaneCache) const noexcept;

    [[nodiscard]] FrustumCollision ContainsPoint(const float3& point) const noexcept {
        I8 lastPlaneCache = -1;
        return ContainsPoint(point, lastPlaneCache);
    }

    [[nodiscard]] FrustumCollision ContainsBoundingBox(const BoundingBox& bbox) const noexcept {
        I8 lastPlaneCache = -1;
        return ContainsBoundingBox(bbox, lastPlaneCache);
    }

    [[nodiscard]] FrustumCollision ContainsSphere(const float3& center, const F32 radius) const noexcept {
        I8 lastPlaneCache = -1;
        return ContainsSphere(center, radius, lastPlaneCache);
    }

    // Get the frustum corners in WorldSpace. cornerWS must be a vector with at least 8 allocated slots
    void getCornersWorldSpace(std::array<float3, to_base(FrustumPoints::COUNT)>& cornersWS) const noexcept;

    const std::array<Plane<F32>, to_base(FrustumPlane::COUNT)>& computePlanes(const mat4<F32>& viewProjMatrix);


    [[nodiscard]] FrustumCollision PlaneBoundingBoxIntersect(FrustumPlane frustumPlane, const BoundingBox& bbox) const noexcept;
    [[nodiscard]] FrustumCollision PlaneBoundingSphereIntersect(FrustumPlane frustumPlane, const BoundingSphere& bsphere) const noexcept;
    [[nodiscard]] FrustumCollision PlanePointIntersect(FrustumPlane frustumPlane, const float3& point) const noexcept;
    [[nodiscard]] FrustumCollision PlaneSphereIntersect(FrustumPlane frustumPlane, const float3& center, F32 radius) const noexcept;
    [[nodiscard]] FrustumCollision PlaneBoundingBoxIntersect(const FrustumPlane* frustumPlanes, U8 count, const BoundingBox& bbox) const noexcept;
    [[nodiscard]] FrustumCollision PlaneBoundingSphereIntersect(const FrustumPlane* frustumPlanes, U8 count, const BoundingSphere& bsphere) const noexcept;
    [[nodiscard]] FrustumCollision PlanePointIntersect(const FrustumPlane* frustumPlanes, U8 count, const float3& point) const noexcept;
    [[nodiscard]] FrustumCollision PlaneSphereIntersect(const FrustumPlane* frustumPlanes, U8 count, const float3& center, F32 radius) const noexcept;

    [[nodiscard]] const std::array<Plane<F32>, to_base(FrustumPlane::COUNT)>& planes() const noexcept { return _frustumPlanes; }

    bool operator==(const Frustum& other) const = default;

   private:
      std::array<Plane<F32>, to_base(FrustumPlane::COUNT)>  _frustumPlanes = create_array<to_base(FrustumPlane::COUNT)>(Plane<F32>{ WORLD_Y_AXIS, 0.0f });
};

[[nodiscard]] FrustumCollision PlaneBoundingBoxIntersect(const Plane<F32>& plane, const BoundingBox& bbox) noexcept;
[[nodiscard]] FrustumCollision PlaneBoundingSphereIntersect(const Plane<F32>& plane, const BoundingSphere& bsphere) noexcept;
[[nodiscard]] FrustumCollision PlanePointIntersect(const Plane<F32>& plane, const float3& point) noexcept;
[[nodiscard]] FrustumCollision PlaneSphereIntersect(const Plane<F32>& plane, const float3& center, F32 radius) noexcept;

};  // namespace Divide

#endif //DVD_FRUSTUM_H_
