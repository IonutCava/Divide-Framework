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
#ifndef DVD_GENERIC_DRAW_COMMAND_H_
#define DVD_GENERIC_DRAW_COMMAND_H_

#include "Core/Headers/PoolHandle.h"

namespace Divide {

struct IndirectIndexedDrawCommand
{
    union
    {
        U32 indexCount{0u};
        U32 vertexCount;
    };

    U32 instanceCount {1u};
    U32 firstIndex    {0u};
    U32 baseVertex    {0u};

    union
    {
      U32 baseInstance{0u};
      U32 firstInstance;
    };
};

static_assert(sizeof( IndirectIndexedDrawCommand ) == 20, "Wrong indexed indirect command size!");

struct IndirectNonIndexedDrawCommand
{
    U32  vertexCount{0u};
    U32  instanceCount{1u};
    U32  firstIndex{0u};
    union
    {
        U32 baseInstance{ 0u };
        U32 firstInstance;
    };
};

static_assert(sizeof( IndirectNonIndexedDrawCommand ) == 16, "Wrong non-indexed indirect command size!");

enum class CmdRenderOptions : U8 {
    RENDER_GEOMETRY           = toBit(1),
    RENDER_WIREFRAME          = toBit(2),
    COUNT = 2
};

#pragma pack(push, 1)
struct GenericDrawCommand {
    IndirectIndexedDrawCommand _cmd{};                                     // 32 bytes
    PoolHandle _sourceBuffer{};                                            // 12 bytes
    U32 _commandOffset{ 0u };                                              // 8  bytes
    U16 _drawCount{ 1u };                                                  // 4  bytes
    U8  _renderOptions{ to_base(CmdRenderOptions::RENDER_GEOMETRY) };      // 2  bytes
    U8  _bufferFlag{ 0u };
};
#pragma pack(pop)

static_assert(sizeof(GenericDrawCommand) == 32, "Wrong command size! May cause performance issues. Disable assert to continue anyway.");

using GenericDrawCommandContainer = eastl::fixed_vector<GenericDrawCommand, 1, true, eastl::dvd_allocator>;

bool isEnabledOption(const GenericDrawCommand& cmd, CmdRenderOptions option) noexcept;
void toggleOption(GenericDrawCommand& cmd, CmdRenderOptions option) noexcept;

void enableOption(GenericDrawCommand& cmd, CmdRenderOptions option) noexcept;
void disableOption(GenericDrawCommand& cmd, CmdRenderOptions option) noexcept;
void setOption(GenericDrawCommand& cmd, CmdRenderOptions option, bool state) noexcept;

void enableOptions(GenericDrawCommand& cmd, BaseType<CmdRenderOptions> optionsMask) noexcept;
void disableOptions(GenericDrawCommand& cmd, BaseType<CmdRenderOptions> optionsMask) noexcept;
void setOptions(GenericDrawCommand& cmd, BaseType<CmdRenderOptions> optionsMask, bool state) noexcept;

void resetOptions(GenericDrawCommand& cmd) noexcept;

bool Compatible(const GenericDrawCommand& lhs, const GenericDrawCommand& rhs) noexcept;

}; //namespace Divide

#endif //DVD_GENERIC_DRAW_COMMAND_H_

#include "GenericDrawCommand.inl"
