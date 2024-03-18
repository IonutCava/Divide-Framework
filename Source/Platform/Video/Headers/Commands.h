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
#ifndef _GFX_COMMAND_H_
#define _GFX_COMMAND_H_


#ifndef TO_STR
#define TO_STR(arg) #arg
#endif


namespace Divide {

namespace GFX {

struct DrawCommand;
struct BindShaderResourcesCommand;

template<typename T>
constexpr size_t MemoryPoolSize()
{
    constexpr size_t g_commandPoolSizeFactor = prevPOW2( sizeof( T ) ) * (1u << 17);

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

template<typename T>
struct CmdAllocator
{
    static inline thread_local MemoryPool<T, MemoryPoolSize<T>()> s_Pool;
};

enum class CommandType : U8;

struct CommandBase;

class CommandBuffer;
struct CommandBase
{
    explicit CommandBase(const CommandType type) noexcept : EType(type) {}

    virtual void addToBuffer(CommandBuffer* buffer) const = 0;

    [[nodiscard]] CommandType Type() const noexcept { return EType; }

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] FORCE_INLINE T* As() { return static_cast<T*>(this); }

protected:
    friend void DELETE_CMD(CommandBase*& cmd);
    virtual void DeleteCmd( CommandBase*& cmd ) const noexcept = 0;

protected:
    CommandType EType;
};

template<typename T, CommandType EnumVal>
struct Command : CommandBase {
    static constexpr CommandType EType = EnumVal;

    Command() noexcept : CommandBase(EnumVal) {}
    virtual ~Command() = default;

    void addToBuffer(CommandBuffer* buffer) const final;

protected:
    void DeleteCmd(CommandBase*& cmd) const noexcept final
    {
        CmdAllocator<T>::s_Pool.deleteElement( (T*&)cmd );
        cmd = nullptr;
    }
};

string ToString(const CommandBase& cmd, U16 indent);

#define IMPLEMENT_COMMAND(Command) \
template<> \
thread_local decltype(CmdAllocator<Command>::s_Pool) CmdAllocator<Command>::s_Pool;

#define DEFINE_COMMAND_BEGIN(Name, Enum) struct Name final : public Command<Name, Enum> { \
using Base = Command<Name, Enum>; \
PROPERTY_RW(bool, flag, false); \

#define DEFINE_COMMAND_END(Name) }

#define DEFINE_COMMAND(Name, Enum) \
DEFINE_COMMAND_BEGIN(Name, Enum)\
DEFINE_COMMAND_END(Name)

}; //namespace GFX
}; //namespace Divide

#endif //_GFX_COMMAND_H_
