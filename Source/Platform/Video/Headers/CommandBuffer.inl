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

#ifndef DVD_COMMAND_BUFFER_INL_
#define DVD_COMMAND_BUFFER_INL_

#include "CommandTypes.h"

namespace Divide {
namespace GFX {

struct DrawCommand;
struct BindShaderResourcesCommand;
struct SendPushConstantsCommand;

namespace
{
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    constexpr size_t MemoryPoolSize()
    {
        constexpr size_t g_commandSize = prevPOW2( sizeof( T ) );

        if constexpr ( std::is_same<T, GFX::BindShaderResourcesCommand>::value )
        {
            return g_commandSize * (1u << 5);
        }
        else if constexpr ( std::is_same<T, GFX::DrawCommand>::value )
        {
            return g_commandSize * (1u << 6);
        }

        return g_commandSize * (1u << 4);
    }

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    struct CmdAllocator 
    {
        using Pool = MemoryPool<T, MemoryPoolSize<T>()>;

        static Pool& GetPool()
        {
            NO_DESTROY thread_local Pool pool;
            return pool;
        }
    };
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add()
{
    T* mem = CmdAllocator<T>::GetPool().newElement();
    _commands.emplace_back(mem);
    return mem;
}

template<typename T>  requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(const T& command)
{
    T* mem = CmdAllocator<T>::GetPool().newElement(command);
    _commands.emplace_back( mem );
    return mem;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(T&& command)
{
    T* mem = CmdAllocator<T>::GetPool().newElement( MOV(command) );
    _commands.emplace_back( mem );
    return mem;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
bool TryMergeCommands(const CommandType type, T* prevCommand, T* crtCommand)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    bool ret = false;
    assert(prevCommand != nullptr && crtCommand != nullptr);
    switch (type)
    {
        case CommandType::DRAW_COMMANDS:
        {
            ret = Merge(static_cast<DrawCommand*>(prevCommand), static_cast<DrawCommand*>(crtCommand));
        } break;
        case CommandType::MEMORY_BARRIER:
        {
            ret = Merge( static_cast<MemoryBarrierCommand*>(prevCommand), static_cast<MemoryBarrierCommand*>(crtCommand));
        } break;
        case CommandType::SEND_PUSH_CONSTANTS:
        {
            ret = Merge( static_cast<SendPushConstantsCommand*>(prevCommand), static_cast<SendPushConstantsCommand*>(crtCommand));
        } break;
        default:
        {
            ret = false;
        } break;
    }

    return ret;
}

}; //namespace GFX
}; //namespace Divide

#endif //DVD_COMMAND_BUFFER_INL_
