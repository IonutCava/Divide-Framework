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
#ifndef _WAYPOINT_H_
#define _WAYPOINT_H_

#include "Core/Math/Headers/Ray.h"

namespace Divide {
namespace Navigation {

/// A point in space that AI units can navigate to
struct Waypoint {
    PROPERTY_RW(Quaternion<F32>, orientation);
    PROPERTY_RW(vec3<F32>, position);
    PROPERTY_R(U32, time, 0u);
    PROPERTY_R(U32, ID, 0xFFFFFFFF);
};

/// A straight line between 2 waypoints
struct WaypointPath {
    POINTER_RW(Waypoint, first, nullptr);
    POINTER_RW(Waypoint, second, nullptr);
    /// If the path intersects an object in the scene, is the path still valid?
    PROPERTY_RW(bool, throughObjects, false);
    /// ray used for collision detection
    PROPERTY_RW(Ray, collisionRay);
};

}  // namespace Navigation
}  // namespace Divide

#endif