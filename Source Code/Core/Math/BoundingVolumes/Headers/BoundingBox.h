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
#ifndef _CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_
#define _CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_

#include "Core/Math/Headers/Ray.h"

namespace Divide {

namespace Attorney {
    class BoundingBoxEditor;
}

class OBB;
class PropertyWindow;
class BoundingSphere;
class BoundingBox {
    friend class Attorney::BoundingBoxEditor;

   public:
    BoundingBox() noexcept;
    explicit BoundingBox(const OBB& obb) noexcept;
    explicit BoundingBox(const BoundingSphere& bSphere) noexcept;
    explicit BoundingBox(vec3<F32> min, vec3<F32> max) noexcept;
    explicit BoundingBox(const vector<vec3<F32>>& points) noexcept;
    explicit BoundingBox(const std::array<vec3<F32>, 8>& points) noexcept;
    explicit BoundingBox(F32 minX, F32 minY, F32 minZ, F32 maxX, F32 maxY, F32 maxZ) noexcept;

    BoundingBox(const BoundingBox& b) noexcept;
    BoundingBox& operator=(const BoundingBox& b) noexcept;

    [[nodiscard]] bool containsPoint(const vec3<F32>& point) const noexcept;
    [[nodiscard]] bool containsBox(const BoundingBox& AABB2) const noexcept;
    [[nodiscard]] bool containsSphere(const BoundingSphere& bSphere) const noexcept;

    [[nodiscard]] bool collision(const BoundingBox& AABB2) const  noexcept;
    [[nodiscard]] bool collision(const BoundingSphere& bSphere) const noexcept;

    [[nodiscard]] bool compare(const BoundingBox& bb) const noexcept;
    [[nodiscard]] bool operator==(const BoundingBox& B) const noexcept;
    [[nodiscard]] bool operator!=(const BoundingBox& B) const noexcept;

    /// Optimized method
    [[nodiscard]] RayResult intersect(const Ray& r, F32 t0, F32 t1) const noexcept;

    void createFromPoints(const vector<vec3<F32>>& points) noexcept;
    void createFromPoints(const std::array<vec3<F32>, 8>& points) noexcept;
    void createFromSphere(const BoundingSphere& bSphere) noexcept;
    void createFromSphere(const vec3<F32>& center, F32 radius) noexcept;
    void createFromCenterAndSize(const vec3<F32>& center, const vec3<F32>& size) noexcept;
    void createFromOBB(const OBB& obb) noexcept;

    void add(const vec3<F32>& v) noexcept;
    void add(const BoundingBox& bb) noexcept;

    void translate(const vec3<F32>& v) noexcept;

    void multiply(F32 factor) noexcept;
    void multiply(const vec3<F32>& v) noexcept;
    void multiplyMax(const vec3<F32>& v) noexcept;
    void multiplyMin(const vec3<F32>& v) noexcept;

    void transform(vec3<F32> initialMin, vec3<F32> initialMax, const mat4<F32>& mat) noexcept;
    void transform(const BoundingBox& initialBoundingBox, const mat4<F32>& mat);
    void transform(const mat4<F32>& mat);

    [[nodiscard]] const vec3<F32>& getMin() const noexcept;
    [[nodiscard]] const vec3<F32>& getMax() const noexcept;

    [[nodiscard]] vec3<F32> getCenter() const noexcept;
    [[nodiscard]] vec3<F32> getExtent() const noexcept;
    [[nodiscard]] vec3<F32> getHalfExtent() const noexcept;

    [[nodiscard]] F32 getWidth() const noexcept;
    [[nodiscard]] F32 getHeight() const noexcept;
    [[nodiscard]] F32 getDepth() const noexcept;

    void set(const BoundingBox& bb) noexcept;
    void set(const vec3<F32>& min, const vec3<F32>& max) noexcept;
    void setMin(const vec3<F32>& min) noexcept;
    void setMax(const vec3<F32>& max) noexcept;

    void set(F32 min, F32 max) noexcept;
    void set(F32 minX, F32 minY, F32 minZ, F32 maxX, F32 maxY, F32 maxZ) noexcept;
    void setMin(F32 min) noexcept;
    void setMin(F32 minX, F32 minY, F32 minZ) noexcept;
    void setMax(F32 max) noexcept;
    void setMax(F32 maxX, F32 maxY, F32 maxZ) noexcept;

    void reset() noexcept;

    [[nodiscard]] vec3<F32> cornerPoint(U8 cornerIndex) const noexcept;
    [[nodiscard]] std::array<vec3<F32>, 8> getPoints() const noexcept;

    // Returns the closest point inside this AABB to the given point
    [[nodiscard]] vec3<F32> nearestPoint(const vec3<F32>& pos) const noexcept;

    [[nodiscard]] inline vec3<F32> getPVertex(const vec3<F32>& normal) const noexcept;
    [[nodiscard]] inline vec3<F32> getNVertex(const vec3<F32>& normal) const noexcept;

   private:
    vec3<F32> _min, _max;
};

namespace Attorney {
    class BoundingBoxEditor {
        static F32* min(BoundingBox& bb) noexcept {
            return bb._min._v;
        }
        static F32* max(BoundingBox& bb) noexcept {
            return bb._max._v;
        }
        friend class Divide::PropertyWindow;
    };
} //namespace Attorney

}  // namespace Divide

#endif  //_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_H_

#include "BoundingBox.inl"