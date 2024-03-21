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
#ifndef DVD_CORE_MATH_DIMENSION_H
#define DVD_CORE_MATH_DIMENSION_H

namespace Divide {
    struct RelativeValue
    {
        F32 _scale {0.f};
        F32 _offset{0.f};
    };

    struct RelativeSize
    {
        F32 _width{0.f};
        F32 _height{0.f};
    };
    struct RelativePosition2D
    {
        RelativeValue _x;
        RelativeValue _y;
    };

    struct RelativeScale2D
    {
        RelativeSize _x;
        RelativeSize _y;
    };


    inline RelativePosition2D pixelPosition(const I32 x, const I32 y)
    {
        // ToDo: Remove these and use proper offsets from the start -Ionut"
        return RelativePosition2D
        {
            ._x = RelativeValue{ ._offset = to_F32(x) },
            ._y = RelativeValue{ ._offset = to_F32(y) }
        };
    }

    inline RelativePosition2D pixelPosition(const vec2<I32> offset)
    {
        return pixelPosition(offset.x, offset.y);
    }

    inline RelativeScale2D pixelScale(const I32 x, const I32 y)
    {
        return RelativeScale2D
        {
            ._x = RelativeSize{._width = to_F32( x ) },
            ._y = RelativeSize{._height = to_F32( y ) }
        };
    }

    inline RelativeScale2D pixelScale(const vec2<I32> scale)
    {
        return pixelScale(scale.x, scale.y);
    }

} //namespace Divide 

#endif //DVD_CORE_MATH_DIMENSION_H
