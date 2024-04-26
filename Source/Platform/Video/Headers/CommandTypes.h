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
#ifndef DVD_GFX_COMMAND_TYPES_H_
#define DVD_GFX_COMMAND_TYPES_H_

namespace Divide {

namespace GFX {

enum class CommandType : U8 {
    BEGIN_RENDER_PASS,
    END_RENDER_PASS,
    BEGIN_GPU_QUERY,
    END_GPU_QUERY,
    SET_VIEWPORT,
    PUSH_VIEWPORT,
    POP_VIEWPORT,
    SET_SCISSOR,
    BLIT_RT,
    COPY_TEXTURE,
    READ_TEXTURE,
    CLEAR_TEXTURE,
    COMPUTE_MIPMAPS,
    SET_CAMERA,
    PUSH_CAMERA,
    POP_CAMERA,
    SET_CLIP_PLANES,
    BIND_PIPELINE,
    BIND_SHADER_RESOURCES,
    SEND_PUSH_CONSTANTS,
    DRAW_COMMANDS,
    DISPATCH_COMPUTE,
    MEMORY_BARRIER,
    READ_BUFFER_DATA,
    CLEAR_BUFFER_DATA,
    BEGIN_DEBUG_SCOPE,
    END_DEBUG_SCOPE,
    ADD_DEBUG_MESSAGE,
    COUNT
};

namespace Names {
    static const char* commandType[] = {
        "BEGIN_RENDER_PASS", "END_RENDER_PASS", "BEGIN_GPU_QUERY", "END_GPU_QUERY", "SET_VIEWPORT", "PUSH_VIEWPORT","POP_VIEWPORT",
        "SET_SCISSOR", "BLIT_RT", "COPY_TEXTURE", "READ_TEXTURE", "CLEAR_TEXTURE", "COMPUTE_MIPMAPS",
        "SET_CAMERA", "PUSH_CAMERA", "POP_CAMERA", "SET_CLIP_PLANES", "BIND_PIPELINE", "BIND_SHADER_RESOURCES", "SEND_PUSH_CONSTANTS",
        "DRAW_COMMANDS", "DISPATCH_COMPUTE", "MEMORY_BARRIER", "READ_BUFFER_DATA", "CLEAR_BUFFER_DATA",
        "BEGIN_DEBUG_SCOPE","END_DEBUG_SCOPE", "ADD_DEBUG_MESSAGE", "UNKNOWN"
    };
};

static_assert(sizeof(Names::commandType) / sizeof(Names::commandType[0]) == to_size(CommandType::COUNT) + 1);

} //namespace GFX
} //namespace Divide

#endif //DVD_GFX_COMMAND_TYPES_H_
