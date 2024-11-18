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
#ifndef DVD_IM_EMULATION_DESCRIPTORS_H_
#define DVD_IM_EMULATION_DESCRIPTORS_H_

#include "Core/Math/Headers/Line.h"
#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Rendering/Camera/Headers/Frustum.h"

namespace Divide {
namespace IM {
    struct BaseDescriptor {
        UColour4  colour{ DefaultColours::WHITE };
        mat4<F32> worldMatrix;
        bool noDepth{ false };
        bool noCull{ false };
        bool wireframe{ false };
    };

    struct OBBDescriptor final : public BaseDescriptor {
        OBB box;
    };

    struct FrustumDescriptor final : public BaseDescriptor {
        Frustum frustum;
    };

    struct BoxDescriptor final : public BaseDescriptor {
        float3 min{ VECTOR3_UNIT * -0.5f };
        float3 max{ VECTOR3_UNIT * 0.5f };
    };

    struct LineDescriptor final : public BaseDescriptor {
        vector<Line> _lines;
    };

    struct SphereDescriptor final : public BaseDescriptor {
        float3 center{ VECTOR3_ZERO };
        F32 radius{ 1.f };
        U8 slices{ 8u };
        U8 stacks{ 8u };
    };

    struct ConeDescriptor final : public BaseDescriptor {
        float3 root{ VECTOR3_ZERO };
        float3 direction{ WORLD_Y_AXIS };
        F32 length{ 1.f };
        F32 radius{ 2.f };
        U8 slices{ 16u }; //max 32u
    };
} //namespace IM
} //namespace Divide

#endif //DVD_IM_EMULATION_DESCRIPTORS_H_
