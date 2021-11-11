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
#ifndef _RAY_H_
#define _RAY_H_

namespace Divide {
struct RayResult
{
    bool hit = false;
    F32 dist = std::numeric_limits<F32>::infinity();
};

struct Ray {
    struct CollisionHelpers {
        vec3<F32> _invDirection;
        vec3<I32> _sign;
    };

    vec3<F32> _origin;
    vec3<F32> _direction;

    void identity() noexcept {
        _origin = VECTOR3_ZERO;
        _direction = WORLD_Y_AXIS;
    }

    [[nodiscard]] CollisionHelpers getCollisionHelpers() const noexcept {
        CollisionHelpers ret = {};
        if (!IS_ZERO(_direction.x)) {
            ret._invDirection.x = 1.0f / _direction.x;
        } else {
            ret._invDirection.x = std::numeric_limits<F32>::infinity();
        }
        if (!IS_ZERO(_direction.y)) {
            ret._invDirection.y = 1.0f / _direction.y;
        } else {
            ret._invDirection.y = std::numeric_limits<F32>::infinity();
        }
        if (!IS_ZERO(_direction.z)) {
            ret._invDirection.z = 1.0f / _direction.z;
        } else {
            ret._invDirection.z = std::numeric_limits<F32>::infinity();
        }
        ret._sign.x = ret._invDirection.x < 0.0f;
        ret._sign.y = ret._invDirection.y < 0.0f;
        ret._sign.z = ret._invDirection.z < 0.0f;
        return ret;
    }
};

}  // namespace Divide

#endif