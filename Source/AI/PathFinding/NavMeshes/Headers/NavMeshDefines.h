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
#ifndef _NAVIGATION_MESH_DEFINES_H_
#define _NAVIGATION_MESH_DEFINES_H_

#ifndef RECAST_UTIL_PROPERTIES
#define RECAST_UTIL_PROPERTIES
#endif
#include <recastnavigation/Recast.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/DetourNavMeshBuilder.h>

constexpr auto MAX_PATHSLOT = 128;  ///< how many paths we can store;
constexpr auto MAX_PATHPOLY = 256;  ///< max number of polygons in a path
constexpr auto MAX_PATHVERT = 512;  ///< most verts in a path

namespace Divide {
namespace AI {
// Extra padding added to the border size of tiles (together with agent radius)
constexpr F32 BORDER_PADDING = -3;
namespace Navigation {
/// These are just sample areas to use consistent values across the samples.
/// The use should specify these base on his needs.
enum class SamplePolyAreas : U8 {
    SAMPLE_POLYAREA_GROUND,
    SAMPLE_POLYAREA_WATER,
    SAMPLE_POLYAREA_ROAD,
    SAMPLE_POLYAREA_DOOR,
    SAMPLE_POLYAREA_GRASS,
    SAMPLE_POLYAREA_JUMP,
    SAMPLE_AREA_OBSTACLE
};

enum class SamplePolyFlags : U16 {
    SAMPLE_POLYFLAGS_WALK = 0x01,      // Ability to walk (ground, grass, road)
    SAMPLE_POLYFLAGS_SWIM = 0x02,      // Ability to swim (water).
    SAMPLE_POLYFLAGS_DOOR = 0x04,      // Ability to move through doors.
    SAMPLE_POLYFLAGS_JUMP = 0x08,      // Ability to jump.
    SAMPLE_POLYFLAGS_DISABLED = 0x10,  // Disabled polygon
    SAMPLE_POLYFLAGS_ALL = 0xffff      // All abilities.
};

// structure for storing output straight line paths
struct PATHDATA {
    F32 PosX[MAX_PATHVERT];
    F32 PosY[MAX_PATHVERT];
    F32 PosZ[MAX_PATHVERT];
    I32 MaxVertex;
    I32 Target;
};

enum class PathErrorCode : I8 {
    PATH_ERROR_NONE = 0,
    PATH_ERROR_NO_NEAREST_POLY_START = -1,
    PATH_ERROR_NO_NEAREST_POLY_END = -2,
    PATH_ERROR_COULD_NOT_CREATE_PATH = -3,
    PATH_ERROR_COULD_NOT_FIND_PATH = -4,
    PATH_ERROR_NO_STRAIGHT_PATH_CREATE = -5,
    PATH_ERROR_NO_STRAIGHT_PATH_FIND = -6
};
}  // namespace Navigation
}  // namespace AI
}  // namespace Divide

#endif
