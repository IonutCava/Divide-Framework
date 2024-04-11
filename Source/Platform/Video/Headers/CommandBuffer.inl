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

namespace Divide {
namespace GFX {

struct DrawCommand;
struct BindShaderResourcesCommand;

namespace
{
    template<typename T>
    constexpr size_t MemoryPoolSize()
    {
        constexpr size_t g_commandPoolSizeFactor = prevPOW2( sizeof( T ) ) * (1u << 4);

        if constexpr ( std::is_same<T, GFX::BindShaderResourcesCommand>::value )
        {
            return g_commandPoolSizeFactor * 3;
        }
        else if constexpr ( std::is_same<T, GFX::DrawCommand>::value )
        {
            return g_commandPoolSizeFactor * 2;
        }

        return g_commandPoolSizeFactor;
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

        void DeleteCmd( CommandBase*& cmd ) const
        {
            GetPool().deleteElement( cmd->As<T>() );
            cmd = nullptr;
        }
    };
}

template <CommandType EnumVal>
inline void Command<EnumVal>::DeleteCmd( CommandBase*& cmd ) const
{
    using CType = MapToDataType<EnumVal>;

    CmdAllocator<CType>::GetPool().deleteElement(cmd->As<CType>());
    cmd = nullptr;
}

template <CommandType EnumVal>
void Command<EnumVal>::addToBuffer(CommandBuffer* buffer) const
{
    using CType = MapToDataType<EnumVal>;

    buffer->add( static_cast<const CType&>(*this) );
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
            ret = Merge(reinterpret_cast<MemoryBarrierCommand*>(prevCommand), reinterpret_cast<MemoryBarrierCommand*>(crtCommand));
        } break;
        case CommandType::SEND_PUSH_CONSTANTS:
        {
            bool partial = false;
            ret = Merge(reinterpret_cast<SendPushConstantsCommand*>(prevCommand)->_constants, reinterpret_cast<SendPushConstantsCommand*>(crtCommand)->_constants, partial);
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
