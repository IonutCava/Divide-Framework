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
#ifndef _GRAPHICS_RESOURCE_H_
#define _GRAPHICS_RESOURCE_H_

namespace Divide {

class GFXDevice;
class GraphicsResource : NonCopyable, NonMovable {
public:
    enum class Type : U8 {
        RENDER_TARGET,
        SHADER_BUFFER,
        BUFFER,
        SHADER,
        SHADER_PROGRAM,
        TEXTURE,
        COUNT
    };
    virtual ~GraphicsResource();

protected:
    explicit GraphicsResource(GFXDevice& context, Type type, I64 GUID, U64 nameHash);

public:
    [[nodiscard]] GFXDevice& context() const noexcept { return _context; }
    [[nodiscard]] U64 nameHash() const noexcept { return _nameHash; }

protected:
    GFXDevice& _context;

private:
    I64 _GUID;
    U64 _nameHash;
    Type _type;
};

namespace Names {
    static const char* resourceTypes[] = {
        "RENDER_TARGET", "SHADER_BUFFER", "BUFFER", "SHADER", "SHADER_PROGRAM", "TEXTURE", "UNKNOWN"
    };
};

}; //namespace Divide

#endif //_GRAPHICS_RESOURCE_H_