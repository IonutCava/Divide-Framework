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
#ifndef _CORE_MATH_LINE_H_
#define _CORE_MATH_LINE_H_

#include "Utility/Headers/Colours.h"

namespace Divide {

struct Line {
    Line() = default;

    Line(vec3<F32> positionStart, vec3<F32> positionEnd, FColour3 colourStart, FColour3 colourEnd, const F32 widthStart, const F32 widthEnd) noexcept
        : _positionStart(MOV(positionStart)),
          _positionEnd(MOV(positionEnd)),
          _colourStart(MOV(colourStart)),
          _colourEnd(MOV(colourEnd)),
          _widthStart(widthStart),
          _widthEnd(widthEnd)
    {
    }

    PROPERTY_RW(vec3<F32>, positionStart, VECTOR3_ZERO);
    PROPERTY_RW(vec3<F32>, positionEnd, VECTOR3_UNIT);
    PROPERTY_RW(UColour4, colourStart, DefaultColours::BLACK_U8);
    PROPERTY_RW(UColour4, colourEnd, DefaultColours::DIVIDE_BLUE_U8);
    PROPERTY_RW(F32, widthStart, 1.0f);
    PROPERTY_RW(F32, widthEnd, 1.0f);
};

} //namespace Divide

#endif //_CORE_MATH_LINE_H_

