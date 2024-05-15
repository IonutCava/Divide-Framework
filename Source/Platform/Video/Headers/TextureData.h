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
#ifndef DVD_TEXTURE_DATA_H_
#define DVD_TEXTURE_DATA_H_

namespace Divide {

struct CopyTexParams 
{
    vec2<U16> _layerRange{0u, 1u};
    vec2<U32> _sourceCoords;
    vec2<U32> _targetCoords;
    vec2<U16> _dimensions; //width, height
    U8 _sourceMipLevel{ 0u };
    U8 _targetMipLevel{ 0u };
};

enum class TextureUpdateState : U8 {
    ADDED = 0,
    REPLACED,
    NOTHING,
    COUNT
};

struct ImageReadbackData
{
    vector<Byte> _data;
    U16 _width{0u};
    U16 _height{0u};
    U8  _bpp{0u};
    U8  _numComponents{0u};
    bool _sourceIsBGR{false};
};

}; //namespace Divide

#endif //DVD_TEXTURE_DATA_H_
