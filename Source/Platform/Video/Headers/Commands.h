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
#ifndef DVD_GFX_COMMAND_H_
#define DVD_GFX_COMMAND_H_


#ifndef TO_STR
#define TO_STR(arg) #arg
#endif


namespace Divide {

namespace GFX {

enum class CommandType : U8;
class CommandBuffer;

struct CommandBase
{
    explicit CommandBase(const CommandType type) noexcept
        : EType( type )
    {
    }

    virtual ~CommandBase() = default;

    CommandBase( const CommandBase& ) = default;
    CommandBase( CommandBase&& ) = default;
    CommandBase& operator=( const CommandBase& ) = default;
    CommandBase& operator=( CommandBase&& ) = default;

    virtual void addToBuffer(CommandBuffer* buffer) const = 0;

    [[nodiscard]] CommandType Type() const noexcept { return EType; }

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] FORCE_INLINE T* As() { return static_cast<T*>(this); }

protected:
    friend void DELETE_CMD( CommandBase*& );
    virtual void DeleteCmd( CommandBase*& cmd ) const = 0;

protected:
    CommandType EType;
};

template<typename T, CommandType EnumVal>
struct Command : CommandBase {
    static constexpr CommandType EType = EnumVal;
    using CType = T;

    Command() noexcept : CommandBase(EnumVal) {}

    void addToBuffer(CommandBuffer* buffer) const final;

protected:
    void DeleteCmd(CommandBase*& cmd) const final;

};

string ToString(const CommandBase& cmd, U16 indent);

#define DEFINE_COMMAND_BEGIN(Name, Enum) struct Name final : public Command<Name, Enum> { \
using Base = Command<Name, Enum>; \
PROPERTY_RW(bool, flag, false) \

#define DEFINE_COMMAND_END(Name) }

#define DEFINE_COMMAND(Name, Enum) \
DEFINE_COMMAND_BEGIN(Name, Enum);\
DEFINE_COMMAND_END(Name)

}; //namespace GFX
}; //namespace Divide

#endif //DVD_GFX_COMMAND_H_
