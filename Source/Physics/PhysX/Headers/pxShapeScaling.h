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

// Source:
/*****************************************************/
/* breeze Engine Physics Module (c) Tobias Zirr 2011 */
/*****************************************************/

#pragma once
#ifndef _PHYSX_SCALING_H_
#define _PHYSX_SCALING_H_

#include "Physics/PhysX/Headers/PhysX.h"

namespace Divide {

/// Scales the given shape as precisely as possible.
void Scale(physx::PxShape &shape, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxBoxGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxSphereGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxPlaneGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxCapsuleGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxConvexMeshGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);
/// Scales the given shape as precisely as possible.
void Scale(physx::PxTriangleMeshGeometry &geometry, physx::PxTransform &pose, const physx::PxVec3 &scaling);

};  // namespace Divide

#endif