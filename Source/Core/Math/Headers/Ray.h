/*
 * Ray class, for use with the optimized ray-box
 * intersection test described in:
 *
 *      Amy Williams, Steve Barrus, R. Keith Morley, and Peter Shirley
 *      "An Efficient and Robust Ray-Box Intersection Algorithm"
 *      Journal of graphics tools, 10(1):49-54, 2005
 *
 */

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
#ifndef DVD_RAY_H_
#define DVD_RAY_H_

namespace Divide
{

struct RayResult
{
    bool hit = false;
    bool inside = false;
    F32 dist = F32_INFINITY;
};

struct Ray
{

    float4 _origin = VECTOR4_ZERO;
    float4 _direction = WORLD_Y_AXIS;
};


struct IntersectionRay : Ray
{
    float4 _invDirection = WORLD_Y_NEG_AXIS;
};

inline void Identity(Ray& rayInOut) noexcept
{
    rayInOut._origin = VECTOR4_ZERO;
    rayInOut._direction = WORLD_Y_AXIS;
}

[[nodiscard]] inline IntersectionRay GetIntersectionRay(const Ray& ray) noexcept
{
    IntersectionRay ret{};

    for ( U8 d = 0u; d < 3u; ++d)
    {
        if (!IS_ZERO(ray._direction._v[d]))
        {
            ret._invDirection._v[d] = 1.f / ray._direction._v[d];
        }
        else
        {
            ret._invDirection._v[d] = F32_INFINITY;
        }
    }

    return ret;
}

}  // namespace Divide

#endif //DVD_RAY_H_
