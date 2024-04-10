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
    };
}

template <CommandType EnumVal>
inline void Command<EnumVal>::DeleteCmd( CommandBase*& cmd ) const
{
    CmdAllocator<CType>::GetPool().deleteElement(cmd->As<MapToDataType<EnumVal>>());
    cmd = nullptr;
}

template <CommandType EnumVal>
void Command<EnumVal>::addToBuffer(CommandBuffer* buffer) const
{
    buffer->add( static_cast<const MapToDataType<EnumVal>&>(*this) );
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::allocateCommand()
{
    _batched = false;

    return get<T>(_commandOrder.emplace_back( T::EType, _commandCount[to_U8( T::EType )]++));
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add()
{
    T* mem = allocateCommand<T>();

    if (mem != nullptr)
    {
        *mem = {};
    }
    else
    {
        mem = CmdAllocator<T>::GetPool().newElement();
        _collection[to_base( T::EType )].emplace_back( mem );
    }

    return mem;
}

template<typename T>  requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(const T& command)
{
    T* mem = allocateCommand<T>();

    if (mem != nullptr)
    {
        *mem = command;
    }
    else
    {
        mem = CmdAllocator<T>::GetPool().newElement(command);
        _collection[to_base( T::EType )].emplace_back( mem );
    }

    return mem;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(T&& command)
{
    T* mem = allocateCommand<T>();

    if (mem != nullptr)
    {
        *mem = MOV(command);
    }
    else 
    {
        mem = CmdAllocator<T>::GetPool().newElement(MOV(command));
        _collection[to_base( T::EType )].emplace_back( mem );
    }

    return mem;
}

template<typename T>  requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const CommandEntry& commandEntry)
{
    const CommandList& collection = _collection[commandEntry._idx._type];

    if ( commandEntry._idx._element < collection.size() )
    {
        return static_cast<T*>(collection[commandEntry._idx._element]);
    }

    return nullptr;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const CommandEntry& commandEntry) const
{
    const CommandList& collection = _collection[commandEntry._idx._type];
    if ( commandEntry._idx._element < collection.size() )
    {
        return static_cast<T*>(collection[commandEntry._idx._element]);
    }

    return nullptr;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
const CommandBuffer::CommandList& CommandBuffer::get() const
{
    return _collection[ to_base(T::EType) ];
}

inline bool CommandBuffer::exists(const CommandEntry& commandEntry) const noexcept
{
    return commandEntry._idx._type < to_base(CommandType::COUNT) && 
           commandEntry._idx._element < _collection[commandEntry._idx._type].size();
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
bool CommandBuffer::exists(const U32 index) const noexcept
{
    return exists(T::EType, index);
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const U32 index)
{
    return get<T>( CommandEntry{T::EType, index});
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const U32 index) const
{
    return get<T>(CommandEntry{T::EType, index });
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
size_t CommandBuffer::count() const noexcept
{
    return _collection[to_base(T::EType)].size();
}

inline CommandBuffer::CommandOrderContainer& CommandBuffer::operator()() noexcept
{
    return _commandOrder;
}

inline const CommandBuffer::CommandOrderContainer& CommandBuffer::operator()() const noexcept
{
    return _commandOrder;
}

inline void CommandBuffer::clear(const bool clearMemory)
{
    _commandCount.fill( 0u );

    _commandOrder.clear();

    if (clearMemory)
    {
        for ( U8 i = 0u; i < to_base(CommandType::COUNT); ++i )
        {
            CommandList& col = _collection[i];

            for ( CommandBase*& cmd : col )
            {
                cmd->DeleteCmd( cmd );
            }
            col.clear();
        }
    }

    _batched = true;
}

inline bool CommandBuffer::empty() const noexcept
{
    return _commandOrder.empty();
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
bool CommandBuffer::tryMergeCommands(const CommandType type, T* prevCommand, T* crtCommand) const
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
