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

constexpr size_t g_commandPoolSizeFactor = 256;

template<typename T>
struct CmdAllocator {
    static Mutex s_PoolMutex;
    static MemoryPool<T, prevPOW2(sizeof(T) * g_commandPoolSizeFactor)> s_Pool;

    template <class... Args>
    static T* allocate(Args&&... args) {
        ScopedLock<Mutex> lock(s_PoolMutex);
        return s_Pool.newElement(FWD(args)...);
    }

    static void deallocate(T*& ptr) {
        ScopedLock<Mutex> lock(s_PoolMutex);
        s_Pool.deleteElement(ptr);
    }
};

enum class CommandType : U8;

struct CommandBase;
struct Deleter {
    virtual void del([[maybe_unused]] CommandBase*& cmd) const = 0;
};

template<typename T>
struct DeleterImpl final : Deleter {
    void del(CommandBase*& cmd) const override {
        CmdAllocator<T>::deallocate((T*&)cmd);
        cmd = nullptr;
    }
};

class CommandBuffer;
struct CommandBase
{
    explicit CommandBase(const CommandType type) noexcept : EType(type) {}

    virtual void addToBuffer(CommandBuffer* buffer) const = 0;

    [[nodiscard]] CommandType Type() const noexcept { return EType; }

    template<typename T>
    FORCE_INLINE [[nodiscard]] 
    typename std::enable_if<std::is_base_of<CommandBase, T>::value, T*>::type
    As() { return static_cast<T*>(this); }

protected:
    friend void DELETE_CMD(CommandBase*& cmd);
    [[nodiscard]] virtual Deleter& getDeleter() const noexcept = 0;

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
    [[nodiscard]] Deleter& getDeleter() const noexcept final {
        static DeleterImpl<T> s_deleter;
        return s_deleter; 
    }
};

string ToString(const CommandBase& cmd, U16 indent);

#define IMPLEMENT_COMMAND(Command) \
decltype(CmdAllocator<Command>::s_PoolMutex) CmdAllocator<Command>::s_PoolMutex; \
decltype(CmdAllocator<Command>::s_Pool) CmdAllocator<Command>::s_Pool;

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
