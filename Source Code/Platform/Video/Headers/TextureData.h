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
#ifndef _TEXTURE_DATA_H_
#define _TEXTURE_DATA_H_

#include "RenderAPIEnums.h"

namespace Divide {

static constexpr U8 INVALID_TEXTURE_BINDING = U8_MAX;

struct CopyTexParams {
    U8 _sourceMipLevel{ 0u };
    U8 _targetMipLevel{ 0u };
    vec3<U32> _sourceCoords;
    vec3<U32> _targetCoords;
    vec3<U16> _dimensions; //width, height, numlayers
};

enum class TextureUpdateState : U8 {
    ADDED = 0,
    REPLACED,
    NOTHING,
    COUNT
};

struct ImageReadbackData
{
    eastl::unique_ptr<Byte[]> _data;
    size_t _size{ 0u };
};

}; //namespace Divide

#endif //_TEXTURE_DATA_H_
