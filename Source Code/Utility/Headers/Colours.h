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
#ifndef _COLOURS_H_
#define _COLOURS_H_

namespace Divide {
namespace DefaultColours {

/// Random stuff added for convenience
static FColour4 WHITE = { 1.0f, 1.0f, 1.0f, 1.0f };
static FColour4 BLACK = { 0.0f, 0.0f, 0.0f, 1.0f };
static FColour4 RED   = { 1.0f, 0.0f, 0.0f, 1.0f };
static FColour4 GREEN = { 0.0f, 1.0f, 0.0f, 1.0f };
static FColour4 BLUE  = { 0.0f, 0.0f, 1.0f, 1.0f };

static UColour4 WHITE_U8 = { 255, 255, 255, 255 };
static UColour4 BLACK_U8 = { 0,   0,   0,   255 };
static UColour4 RED_U8   = { 255, 0,   0,   255 };
static UColour4 GREEN_U8 = { 0,   255, 0,   255 };
static UColour4 BLUE_U8  = { 0,   0,   255, 255 };

static FColour4 DIVIDE_BLUE    = { 0.1f, 0.1f, 0.8f, 1.0f};
static UColour4 DIVIDE_BLUE_U8 = { 26,   26,   204,  255 };

inline vec4<U8> RANDOM() {
    return vec4<U8>(Random<U8>(255),
                    Random<U8>(255),
                    Random<U8>(255),
                    to_U8(255));
}

inline vec4<F32> RANDOM_NORMALIZED() {
    return Util::ToFloatColour(RANDOM());
}

}  // namespace DefaultColours
}  // namespace Divide

#endif