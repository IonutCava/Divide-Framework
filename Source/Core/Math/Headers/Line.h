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
#ifndef DVD_CORE_MATH_LINE_H_
#define DVD_CORE_MATH_LINE_H_

#include "Utility/Headers/Colours.h"

namespace Divide {

struct Line
{
    vec3<F32> _positionStart = VECTOR3_ZERO;
    vec3<F32> _positionEnd = VECTOR3_UNIT;
    UColour4  _colourStart = DefaultColours::BLACK_U8;
    UColour4  _colourEnd = DefaultColours::DIVIDE_BLUE_U8;
    F32       _widthStart = 1.0f;
    F32       _widthEnd = 1.0f;
};

} //namespace Divide

#endif //DVD_CORE_MATH_LINE_H_
