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
#ifndef DVD_OCTREE_INTERSECTION_RECORD_H_
#define DVD_OCTREE_INTERSECTION_RECORD_H_

#include "Core/Math/Headers/Ray.h"

namespace Divide {

class BoundsComponent;

class IntersectionRecord
{
  public:
    IntersectionRecord() noexcept;
    IntersectionRecord(float3 hitPos,
                       float3 hitNormal,
                       Ray ray,
                       D64 distance) noexcept;
    /// Creates a new intersection record indicating whether there was a hit or not and the object which was hit.
    IntersectionRecord(BoundsComponent* hitObject) noexcept;

    /// Reset all information contained by this record
    void reset() noexcept;

    /// This is the exact point in 3D space which has an intersection.
    float3 _position;
    /// This is the normal of the surface at the point of intersection
    float3 _normal;
    /// This is the ray which caused the intersection
    Ray _ray;
    /// This is the object which is being intersected
    BoundsComponent* _intersectedObject1;
    /// This is the other object being intersected (may be null, as in the case of a ray-object intersection)
    BoundsComponent* _intersectedObject2;

    /// This is the distance from the ray to the intersection point. 
    /// You'll usually want to use the nearest collision point if you get multiple intersections.
    D64 _distance = 0.;

    bool _hasHit = false;

    /// check the object identities between the two intersection records. If they match in either order, we have a duplicate.
    bool operator==(const IntersectionRecord& otherRecord) const noexcept;

    [[nodiscard]] bool isEmpty() const noexcept {
        return _intersectedObject1 == nullptr && _intersectedObject2 == nullptr;
    }
};

using IntersectionContainer = eastl::fixed_vector<IntersectionRecord, 32u, true>;

}; //namespace Divide

#endif //DVD_OCTREE_INTERSECTION_RECORD_H_
