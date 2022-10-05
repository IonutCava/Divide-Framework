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
#ifndef _CORE_MATH_DIMENSION_H
#define _CORE_MATH_DIMENSION_H

#if !defined(_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING)
#define _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING
#endif //_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING
#include <CEGUI/Vector.h>
#include <CEGUI/Size.h>
#undef _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING

namespace Divide {
    // ToDo: We need our own ... -Ionut
    using RelativeValue = CEGUI::UDim;
    using RelativePosition2D = CEGUI::UVector2;
    using RelativeScale2D = CEGUI::USize;


    inline RelativePosition2D pixelPosition(const I32 x, const I32 y) {
        // ToDo: Remove these and use proper offsets from the start -Ionut"
        return RelativePosition2D(RelativeValue(0.0f, to_F32(x)), RelativeValue(0.0f, to_F32(y)));
    }

    inline RelativePosition2D pixelPosition(const vec2<I32> offset) {
        return pixelPosition(offset.x, offset.y);
    }

    inline RelativeScale2D pixelScale(const I32 x, const I32 y) {
        return RelativeScale2D(RelativeValue(0.0f, to_F32(x)), RelativeValue(0.0f, to_F32(y)));
    }

    inline RelativeScale2D pixelScale(const vec2<I32> scale) {
        return pixelScale(scale.x, scale.y);
    }

} //namespace Divide 

#endif //_CORE_MATH_DIMENSION_H