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
#ifndef DVD_STL_STRING_H_
#define DVD_STL_STRING_H_

#include "TemplateAllocator.h"
#include <string>
#include <memory.h>

namespace Divide
{
    /// dvd_allocator uses xmalloc/xfree for memory management whereas std::allocator uses the classic new/delete pair.
    template<bool Fast, class Elem>
    using string_allocator = typename std::conditional<Fast, dvd_allocator<Elem>, std::allocator<Elem>>::type;

    template<bool Fast>
    using string_impl = std::basic_string<char, std::char_traits<char>, string_allocator<Fast, char>>;
    template<bool Fast>
    using wstring_impl = std::basic_string<wchar_t, std::char_traits<wchar_t>, string_allocator<Fast, wchar_t>>;
    template<bool Fast>
    using stringstream_impl = std::basic_stringstream<char, std::char_traits<char>, string_allocator<Fast, char>>;
    template<bool Fast>
    using ostringstream_imp = std::basic_ostringstream<char, std::char_traits<char>, string_allocator<Fast, char>>;
    template<bool Fast>
    using wstringstream_imp = std::basic_stringstream<wchar_t, std::char_traits<wchar_t>, string_allocator<Fast, wchar_t>>;
    template<bool Fast>
    using wostringstream_imp = std::basic_ostringstream<wchar_t, std::char_traits<wchar_t>, string_allocator<Fast, wchar_t>>;
    template<bool Fast>
    using istringstream_imp = std::basic_istringstream<char, std::char_traits<char>, string_allocator<Fast, char>>;
    template<bool Fast>
    using wistringstream_imp = std::basic_istringstream<wchar_t, std::char_traits<wchar_t>, string_allocator<Fast, wchar_t>>;
    template<bool Fast>
    using stringbuf_imp = std::basic_stringbuf<char, std::char_traits<char>, string_allocator<Fast, char>>;
    template<bool Fast>
    using wstringbuf_imp = std::basic_stringbuf<wchar_t, std::char_traits<wchar_t>, string_allocator<Fast, wchar_t>>;

    using string = string_impl<false>;
    using string_fast = string_impl<true>;
    using wstring = wstring_impl<false>;
    using wstring_fast = wstring_impl<true>;
    using stringstream = stringstream_impl<false>;
    using stringstream_fast = stringstream_impl<true>;
    using ostringstream = ostringstream_imp<false>;
    using ostringstream_fast = ostringstream_imp<true>;
    using wstringstream = wstringstream_imp<false>;
    using wstringstream_fast = wstringstream_imp<true>;
    using wostringstream = wostringstream_imp<false>;
    using wostringstream_fast = wostringstream_imp<true>;
    using istringstream = istringstream_imp<false>;
    using istringstream_fast = istringstream_imp<true>;
    using wistringstream = wistringstream_imp<false>;
    using wistringstream_fast = wistringstream_imp<true>;
    using stringbuf = stringbuf_imp<false>;
    using stringbuf_fast = stringbuf_imp<true>;
    using wstringbuf = wstringbuf_imp<false>;
    using wstringbuf_fast = wstringbuf_imp<true>;

    template<typename T>
    concept is_stl_wide_string = std::is_same_v<T, wstring> ||
                                 std::is_same_v<T, wstring_fast>;

    template<typename T>
    concept is_stl_non_wide_string = std::is_same_v<T, string> ||
                                     std::is_same_v<T, string_fast>;

    template<typename T>
    concept is_stl_string = is_stl_wide_string<T> ||
                            is_stl_non_wide_string<T>;

    template <typename T>
    concept is_fixed_string = requires(T c) { []<typename X, int N, bool overflow>(eastl::fixed_string<X, N, overflow>&) {}(c); };

    template<typename T>
    concept is_eastl_string = std::is_same_v<T, eastl::string> ||
                              is_fixed_string<T>;

    template<typename T>
    concept is_non_wide_string = is_stl_non_wide_string<T> ||
                                 is_eastl_string<T>;
}; //namespace Divide

#endif //DVD_STL_STRING_H_
