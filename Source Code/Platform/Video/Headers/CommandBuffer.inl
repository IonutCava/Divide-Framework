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

#ifndef _COMMAND_BUFFER_INL_
#define _COMMAND_BUFFER_INL_


namespace Divide {
namespace GFX {

template <typename T, CommandType EnumVal>
void Command<T, EnumVal>::addToBuffer(CommandBuffer* buffer) const
{
    buffer->add(static_cast<const T&>(*this));
}

FORCE_INLINE void DELETE_CMD(CommandBase*& cmd)
{
    cmd->getDeleter().del(cmd);
}

FORCE_INLINE size_t RESERVE_CMD(const U8 typeIndex) noexcept
{
    switch (static_cast<CommandType>(typeIndex)) {
        case CommandType::BIND_SHADER_RESOURCES: return 2;
        case CommandType::SEND_PUSH_CONSTANTS  : return 3;
        case CommandType::DRAW_COMMANDS        : return 4;
        default: break;
    }

    return 1;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::allocateCommand()
{
    const CommandEntry& newEntry = _commandOrder.emplace_back(to_U8(T::EType), _commandCount[to_U8(T::EType)]++);

    return get<T>(newEntry);
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add()
{
    T* mem = allocateCommand<T>();

    if (mem != nullptr) {
        *mem = {};
    } else {
        mem = CmdAllocator<T>::s_Pool.newElement();
        _commands.insert<T>(to_base(mem->Type()), mem);
    }

    _batched = false;
    return mem;
}

template<typename T>  requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(const T& command) {
    T* mem = allocateCommand<T>();

    if (mem != nullptr) {
        *mem = command;
    } else {
        mem = CmdAllocator<T>::s_Pool.newElement(command);
        _commands.insert<T>(to_base(mem->Type()), mem);
    }

    _batched = false;
    return mem;
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::add(const T&& command) {
    T* mem = allocateCommand<T>();

    if (mem != nullptr) {
        *mem = MOV(command);
    } else {
        mem = CmdAllocator<T>::s_Pool.newElement(MOV(command));
        _commands.insert<T>(to_base(mem->Type()), mem);
    }

    _batched = false;
    return mem;
}

template<typename T>  requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const CommandEntry& commandEntry) {
    return static_cast<T*>(_commands.get(commandEntry));
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const CommandEntry& commandEntry) const {
    return static_cast<T*>(_commands.get(commandEntry));
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
const CommandBuffer::Container::EntryList& CommandBuffer::get() const {
    return _commands.get(to_base(T::EType));
}

inline bool CommandBuffer::exists(const CommandEntry& commandEntry) const noexcept {
    return _commands.exists(commandEntry);
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
bool CommandBuffer::exists(const U24 index) const noexcept {
    return exists(to_base(T::EType), index);
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const U24 index) {
    return get<T>({to_base(T::EType), index});
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
T* CommandBuffer::get(const U24 index) const {
    return get<T>({to_base(T::EType), index });
}

inline bool CommandBuffer::exists(const U8 typeIndex, const U24 index) const noexcept {
    return _commands.exists({ typeIndex, index });
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
size_t CommandBuffer::count() const noexcept {
    return _commands.get(to_base(T::EType)).size();
}

inline CommandBuffer::CommandOrderContainer& CommandBuffer::operator()() noexcept {
    return _commandOrder;
}

inline const CommandBuffer::CommandOrderContainer& CommandBuffer::operator()() const noexcept {
    return _commandOrder;
}

inline void CommandBuffer::clear(const bool clearMemory) {
    std::memset(_commandCount.data(), 0, sizeof(U24) * _commandCount.size());

    //_commandCount.fill( 0u );

    _commandOrder.clear();
    if (clearMemory) {
        _commands.clear(true);
    }

    _batched = true;
}

inline bool CommandBuffer::empty() const noexcept {
    return _commandOrder.empty();
}

template<typename T> requires std::is_base_of_v<CommandBase, T>
bool CommandBuffer::tryMergeCommands(const CommandType type, T* prevCommand, T* crtCommand) const {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    bool ret = false;
    assert(prevCommand != nullptr && crtCommand != nullptr);
    switch (type) {
        case CommandType::DRAW_COMMANDS:        {
            ret = Merge(static_cast<DrawCommand*>(prevCommand), static_cast<DrawCommand*>(crtCommand));
        } break;
        case CommandType::MEMORY_BARRIER: {
            ret = Merge(reinterpret_cast<MemoryBarrierCommand*>(prevCommand), reinterpret_cast<MemoryBarrierCommand*>(crtCommand));
        } break;
        case CommandType::SEND_PUSH_CONSTANTS:  {
            bool partial = false;
            ret = Merge(reinterpret_cast<SendPushConstantsCommand*>(prevCommand)->_constants, reinterpret_cast<SendPushConstantsCommand*>(crtCommand)->_constants, partial);
        } break;
        default: {
            ret = false;
        } break;
    }

    return ret;
}

}; //namespace GFX
}; //namespace Divide


#endif //_COMMAND_BUFFER_INL_