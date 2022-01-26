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
#ifndef _FILE_WITH_PATH_H_
#define _FILE_WITH_PATH_H_

namespace Divide {

enum class FileType : U8 {
    BINARY = 0,
    TEXT = 1,
    COUNT
};

struct ResourcePath
{
    ResourcePath() = default;

    template<size_t N>
    explicit ResourcePath(const Str<N>& string) : ResourcePath(string.c_str()) {}

    explicit ResourcePath(const std::string_view& path);

    [[nodiscard]] size_t length() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const char* c_str() const noexcept;

    ResourcePath& pop_back() noexcept;
    ResourcePath& append(const std::string_view& str);

    [[nodiscard]] const string& str() const noexcept { return _str; }

    const string& convertToLower() noexcept { 
        std::transform(_str.begin(), _str.end(), _str.begin(), [](unsigned char c) { return std::tolower(c); });
        return str();
    }
private:
    string _str;
};

ResourcePath  operator+  (const ResourcePath& lhs, const ResourcePath& rhs);
ResourcePath  operator+  (const ResourcePath& lhs, const std::string_view& rhs);
template<size_t N>
ResourcePath  operator+  (const ResourcePath& lhs, const Str<N>& rhs) {
    return lhs + rhs.c_str();
}

ResourcePath& operator+= (ResourcePath& lhs, const ResourcePath& rhs);
ResourcePath& operator+= (ResourcePath& lhs, const std::string_view& rhs);
template<size_t N>
ResourcePath& operator+= (ResourcePath& lhs, const Str<N>& rhs) {
    return lhs += rhs.c_str();
}

bool operator== (const ResourcePath& lhs, const std::string_view& rhs);
bool operator!= (const ResourcePath& lhs, const std::string_view& rhs);

bool operator== (const ResourcePath& lhs, const ResourcePath& rhs);
bool operator!= (const ResourcePath& lhs, const ResourcePath& rhs);

template<size_t N>
bool operator== (const ResourcePath& lhs, const Str<N>& rhs) {
    return lhs == rhs.c_str();
}
template<size_t N>
bool operator!= (const ResourcePath& lhs, const Str<N>& rhs) {
    return lhs != rhs.c_str();
}

using FileAndPath = std::pair<ResourcePath, ResourcePath>;

}; //namespace Divide

#endif //_FILE_WITH_PATH_H_