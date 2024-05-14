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

#include <string>
#include <memory.h>

namespace Divide
{
    using string = std::basic_string<char, std::char_traits<char>, dvd_allocator<char>>;
    using wstring = std::basic_string<wchar_t, std::char_traits<wchar_t>, dvd_allocator<wchar_t>>;
    using stringstream = std::basic_stringstream<char, std::char_traits<char>, dvd_allocator<char>>;
    using ostringstream = std::basic_ostringstream<char, std::char_traits<char>, dvd_allocator<char>>;
    using wstringstream = std::basic_stringstream<wchar_t, std::char_traits<wchar_t>, dvd_allocator<wchar_t>>;
    using wostringstream = std::basic_ostringstream<wchar_t, std::char_traits<wchar_t>, dvd_allocator<wchar_t>>;
    using istringstream = std::basic_istringstream<char, std::char_traits<char>, dvd_allocator<char>>;
    using wistringstream = std::basic_istringstream<wchar_t, std::char_traits<wchar_t>, dvd_allocator<wchar_t>>;
    using stringbuf = std::basic_stringbuf<char, std::char_traits<char>, dvd_allocator<char>>;
    using wstringbuf = std::basic_stringbuf<wchar_t, std::char_traits<wchar_t>, dvd_allocator<wchar_t>>;

    template<typename T>
    concept is_stl_wide_string = std::is_same_v<T, wstring> || std::is_same_v<T, std::wstring>;

    template<typename T>
    concept is_stl_non_wide_string = std::is_same_v<T, string> || std::is_same_v<T, std::string>;

    template<typename T>
    concept is_stl_string = is_stl_wide_string<T> || is_stl_non_wide_string<T>;

    template <typename T>
    concept is_fixed_string = requires(T c) { []<typename X, int N, bool overflow>(eastl::fixed_string<X, N, overflow>&) {}(c); };

    template<typename T>
    concept is_eastl_string = std::is_same_v<T, eastl::string> || is_fixed_string<T>;

    template<typename T>
    concept is_non_wide_string = is_stl_non_wide_string<T> || is_eastl_string<T>;
}; //namespace Divide

#endif //DVD_STL_STRING_H_
