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

#ifndef DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_INL_
#define DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_INL_

namespace Divide {

inline bool BoundingBox::containsPoint(const float3& point) const noexcept {
    return IS_GEQUAL(point.x, _min.x) && IS_GEQUAL(point.y, _min.y) && IS_GEQUAL(point.z, _min.z) &&
        IS_LEQUAL(point.x, _max.x) && IS_LEQUAL(point.y, _max.y) && IS_LEQUAL(point.z, _max.z);
}

inline bool BoundingBox::compare(const BoundingBox& bb) const  noexcept {
    return _min == bb._min &&
           _max == bb._max;
}

inline bool BoundingBox::operator==(const BoundingBox& B) const noexcept {
    return compare(B);
}

inline bool BoundingBox::operator!=(const BoundingBox& B) const noexcept {
    return !compare(B);
}

inline void BoundingBox::createFromPoints(const vector<float3>& points) noexcept {
    for (const float3& p : points) {
        add(p);
    }
}

inline void BoundingBox::createFromPoints(const std::array<float3, 8>& points) noexcept {
    for (const float3& p : points) {
        add(p);
    }
}

inline void BoundingBox::createFromSphere(const float3& center, const F32 radius) noexcept {
    _max.set(center + radius);
    _min.set(center - radius);
}

inline void BoundingBox::add(const float3& v) noexcept {
    //Max
    if (v.x > _max.x) {
        _max.x = v.x;
    }
    if (v.y > _max.y) {
        _max.y = v.y;
    }
    if (v.z > _max.z) {
        _max.z = v.z;
    }

    //Min
    if (v.x < _min.x) {
        _min.x = v.x;
    }
    if (v.y < _min.y) {
        _min.y = v.y;
    }
    if (v.z < _min.z) {
        _min.z = v.z;
    }
}

inline void BoundingBox::add(const BoundingBox& bb) noexcept {
    _max.set(std::max(bb._max.x, _max.x),
             std::max(bb._max.y, _max.y),
             std::max(bb._max.z, _max.z));

    _min.set(std::min(bb._min.x, _min.x),
             std::min(bb._min.y, _min.y),
             std::min(bb._min.z, _min.z));
}

inline void BoundingBox::translate(const float3& v) noexcept {
    _min += v;
    _max += v;
}

inline void BoundingBox::multiply(const F32 factor) noexcept {
    _min *= factor;
    _max *= factor;
}

inline void BoundingBox::multiply(const float3& v) noexcept {
    _min.x *= v.x;
    _min.y *= v.y;
    _min.z *= v.z;
    _max.x *= v.x;
    _max.y *= v.y;
    _max.z *= v.z;
}

inline void BoundingBox::multiplyMax(const float3& v) noexcept {
    _max.x *= v.x;
    _max.y *= v.y;
    _max.z *= v.z;
}

inline void BoundingBox::multiplyMin(const float3& v) noexcept {
    _min.x *= v.x;
    _min.y *= v.y;
    _min.z *= v.z;
}

inline const float3& BoundingBox::getMin() const noexcept {
    return _min;
}

inline const float3& BoundingBox::getMax() const noexcept {
    return _max;
}

inline float3 BoundingBox::getCenter() const noexcept {
    // Doesn't seem to inline all that great in Debug builds
    return {
        (_max.x + _min.x) * 0.5f,
        (_max.y + _min.y) * 0.5f,
        (_max.z + _min.z) * 0.5f
    };
}

inline float3 BoundingBox::getExtent() const noexcept {
    return _max - _min;
}

inline float3 BoundingBox::getHalfExtent() const noexcept {
    // Doesn't seem to inline all that great in Debug builds
    return {
        (_max.x - _min.x) * 0.5f,
        (_max.y - _min.y) * 0.5f,
        (_max.z - _min.z) * 0.5f
    };
}

inline F32 BoundingBox::getWidth() const noexcept {
    return _max.x - _min.x;
}

inline F32 BoundingBox::getHeight() const noexcept {
    return _max.y - _min.y;
}

inline F32 BoundingBox::getDepth() const noexcept {
    return _max.z - _min.z;
}

inline void BoundingBox::setMin(const float3& min) noexcept {
    setMin(min.x, min.y, min.z);
}

inline void BoundingBox::setMax(const float3& max) noexcept {
    setMax(max.x, max.y, max.z);
}

inline void BoundingBox::set(const BoundingBox& bb) noexcept {
    set(bb._min, bb._max); 
}

inline void BoundingBox::set(const F32 min, const F32 max) noexcept {
    _min.set(min);
    _max.set(max);
}

inline void BoundingBox::set(const F32 minX, const F32 minY, const F32 minZ, const F32 maxX, const F32 maxY, const F32 maxZ) noexcept {
    _min.set(minX, minY, minZ);
    _max.set(maxX, maxY, maxZ);
}

inline void BoundingBox::setMin(const F32 min) noexcept {
    _min.set(min);
}

inline void BoundingBox::setMin(const F32 minX, const F32 minY, const F32 minZ) noexcept {
    _min.set(minX, minY, minZ);
}

inline void BoundingBox::setMax(const F32 max) noexcept {
    _max.set(max);
}

inline void BoundingBox::setMax(const F32 maxX, const F32 maxY, const F32 maxZ) noexcept {
    _max.set(maxX, maxY, maxZ);
}

inline void BoundingBox::set(const float3& min, const float3& max) noexcept {
    _min = min;
    _max = max;
}

inline void BoundingBox::reset() noexcept {
    _min.set( F32_MAX);
    _max.set(-F32_MAX);
}

inline float3 BoundingBox::cornerPoint(const U8 cornerIndex) const noexcept {
    assert(0 <= cornerIndex && cornerIndex <= 7);
    switch (cornerIndex) {
        default:
        case 0: return float3{ _min.x, _min.y, _min.z};
        case 1: return float3{ _min.x, _min.y, _max.z };
        case 2: return float3{ _min.x, _max.y, _min.z };
        case 3: return float3{ _min.x, _max.y, _max.z };
        case 4: return float3{ _max.x, _min.y, _min.z };
        case 5: return float3{ _max.x, _min.y, _max.z };
        case 6: return float3{ _max.x, _max.y, _min.z };
        case 7: return float3{ _max.x, _max.y, _max.z };
    }
}
inline std::array<float3, 8> BoundingBox::getPoints() const noexcept {
    return std::array<float3, 8>
    {
        float3{_min.x, _min.y, _min.z},
        float3{_min.x, _min.y, _max.z},
        float3{_min.x, _max.y, _min.z},
        float3{_min.x, _max.y, _max.z},
        float3{_max.x, _min.y, _min.z},
        float3{_max.x, _min.y, _max.z},
        float3{_max.x, _max.y, _min.z},
        float3{_max.x, _max.y, _max.z}
    };
}

inline float3 BoundingBox::nearestPoint(const float3& pos) const noexcept {
    return Clamped(pos, getMin(), getMax());
}

inline float3 BoundingBox::getPVertex(const float3& normal) const noexcept {
    return float3(normal.x >= 0.0f ? _max.x : _min.x,
                     normal.y >= 0.0f ? _max.y : _min.y,
                     normal.z >= 0.0f ? _max.z : _min.z);
}

inline float3 BoundingBox::getNVertex(const float3& normal) const noexcept {
    return float3(normal.x >= 0.0f ? _min.x : _max.x,
                     normal.y >= 0.0f ? _min.y : _max.y,
                     normal.z >= 0.0f ? _min.z : _max.z);
}

}  // namespace Divide

#endif  //DVD_CORE_MATH_BOUNDINGVOLUMES_BOUNDINGBOX_INL_
