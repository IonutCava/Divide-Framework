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
#ifndef _RENDER_TARGET_DRAW_DESCRIPTOR_INL_
#define _RENDER_TARGET_DRAW_DESCRIPTOR_INL_

namespace Divide {

inline bool operator==(const RTDrawMask& lhs, const RTDrawMask& rhs) noexcept {
    return lhs._disabledDepth   == rhs._disabledDepth &&
           lhs._disabledStencil == rhs._disabledStencil &&
           lhs._disabledColours == rhs._disabledColours;
}

inline bool operator!=(const RTDrawMask& lhs, const RTDrawMask& rhs) noexcept {
    return lhs._disabledDepth   != rhs._disabledDepth ||
           lhs._disabledStencil != rhs._disabledStencil ||
           lhs._disabledColours != rhs._disabledColours;
}

inline bool operator==(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs) noexcept {
    return lhs._drawMask == rhs._drawMask &&
           lhs._setViewport == rhs._setViewport &&
           lhs._setDefaultState == rhs._setDefaultState;
}

inline bool operator!=(const RTDrawDescriptor& lhs, const RTDrawDescriptor& rhs) noexcept {
    return lhs._drawMask != rhs._drawMask ||
           lhs._setViewport != rhs._setViewport ||
           lhs._setDefaultState != rhs._setDefaultState;
}
}; //namespace Divide
#endif// _RENDER_TARGET_DRAW_DESCRIPTOR_INL_