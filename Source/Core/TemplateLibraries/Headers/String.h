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
#ifndef DVD_STRING_H_
#define DVD_STRING_H_

#include "STLString.h"

#include <fmt/format.h>

namespace Divide
{
    template<size_t N>
    class Str : public eastl::fixed_string<char, N, true>
    {
       public:
        using Base = eastl::fixed_string<char, N, true>;
        using Base::Base;

        Str(const char* str, const size_t length, [[maybe_unused]] dvd_allocator<char>& allocator) : Base(str, length) {}

        Str(const string& str) : Base(str.c_str()) {}
        Str(const std::string_view str) : Base(str.data(), str.size()) {}

        operator std::string_view() const noexcept
        {
            return std::string_view( Base::c_str(), Base::size() );
        }
    };

    template<size_t N>
    Str<N> operator+( const char* other, const Str<N>& base )
    {
        Str<N> ret( other );
        ret.append( base );
        return ret;

    }

    template<size_t N>
    Str<N> operator+( const std::string_view other, const Str<N>& base )
    {
        Str<N> ret( other );
        ret.append( base );
        return ret;
    }

    template<size_t N>
    Str<N> operator+( const Str<N>& base, const char* other )
    {
        Str<N> ret = base;
        ret.append( other );
        return ret;
    }

    template<size_t N>
    Str<N> operator+( const Str<N>& base, const std::string_view other )
    {
        Str<N> ret = base;
        ret.append( other.data(), other.size() );
        return ret;
    }

    template<typename T>
    concept is_string = is_stl_string<T> || is_eastl_string<T>;
}//namespace Divide

template<size_t N>
struct fmt::formatter<Divide::Str<N>>
{
    constexpr auto parse( format_parse_context& ctx ) { return ctx.begin(); }

    template <typename FormatContext>
    auto format( const Divide::Str<N>& str, FormatContext& ctx ) -> decltype(ctx.out())
    {
        return fmt::format_to( ctx.out(), "{}", str.c_str() );
    }
};

namespace std
{
    template<size_t N>
    struct hash<Divide::Str<N> >
    {
        size_t operator()( const Divide::Str<N>& str ) const
        {
            const std::string_view view = str;
            return std::hash<std::string_view>{}(view);
        }
    };
}

#endif //DVD_STRING_H_
