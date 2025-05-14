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

#ifndef DVD_CORE_CONSOLE_INL_
#define DVD_CORE_CONSOLE_INL_

#include "StringHelper.h"

struct sink 
{ 
    template<typename ...Args> 
    explicit sink(Args const& ...) noexcept {}
};

namespace Divide {
template <typename... Args>
NO_INLINE void Console::d_printfn(const std::string_view format, Args&&... args)
{
    if constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        printfn(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_printf(const std::string_view format, Args&&... args)
{
    if constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        printf(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_warnfn(const std::string_view format, Args&&... args)
{
    if constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        warnfn(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_warnf(const std::string_view format, Args&&... args)
{
    if constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        warnf(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_errorfn(const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        errorfn(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_errorf(const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        errorf(format, FWD(args)...);
    }
    else
    {
        sink{ format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::printfn(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>(format, FWD(args)...), true, EntryType::INFO);
}

template <typename... Args>
NO_INLINE void Console::printf(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>( format, FWD(args)...), false, EntryType::INFO);
}

template <typename... Args>
NO_INLINE void Console::warnfn(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>( format, FWD(args)...), true, EntryType::WARNING);
}

template <typename... Args>
NO_INLINE void Console::warnf(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>( format, FWD(args)...), false, EntryType::WARNING);
}

template <typename... Args>
NO_INLINE void Console::errorfn(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>( format, FWD( args )...), true, EntryType::ERR);
}

template <typename... Args>
NO_INLINE void Console::errorf(const std::string_view format, Args&&... args)
{
    Output( Util::StringFormat<string>( format, FWD(args)...), false, EntryType::ERR);
}

template <typename... Args>
NO_INLINE void Console::printfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), true, EntryType::INFO);
}

template <typename... Args>
NO_INLINE void Console::printf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), false, EntryType::INFO);
}

template <typename... Args>
NO_INLINE void Console::warnfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), true, EntryType::WARNING);
}

template <typename... Args>
NO_INLINE void Console::warnf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), false, EntryType::WARNING);
}

template <typename... Args>
NO_INLINE void Console::errorfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), true, EntryType::ERR);
}

template <typename... Args>
NO_INLINE void Console::errorf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    Output(outStream, Util::StringFormat<string>( format, FWD(args)...), false, EntryType::ERR);
}

template <typename... Args>
NO_INLINE void Console::d_printfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        printfn(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_printf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        printf(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_warnfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        warnfn(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_warnf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        warnf(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_errorfn(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        errorfn(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}

template <typename... Args>
NO_INLINE void Console::d_errorf(std::ofstream& outStream, const std::string_view format, Args&&... args)
{
    if constexpr (Config::Build::IS_DEBUG_BUILD)
    {
        errorf(outStream, format, FWD(args)...);
    }
    else
    {
        sink{ outStream, format, args ... };
    }
}
}

#endif  //DVD_CORE_CONSOLE_INL_