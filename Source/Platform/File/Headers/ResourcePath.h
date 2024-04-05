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
#ifndef DVD_FILE_WITH_PATH_H_
#define DVD_FILE_WITH_PATH_H_

#include <filesystem>

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
    explicit ResourcePath(const Str<N>& path) : ResourcePath(path.c_str()) {}

    explicit ResourcePath(const char* path) : _fileSystemPath(path) {}

    explicit ResourcePath(std::string_view path) : _fileSystemPath(path) {}

    explicit ResourcePath(const string& path) : _fileSystemPath(path) {}

    explicit ResourcePath(const std::filesystem::path& path) : _fileSystemPath(path) {}

    [[nodiscard]] size_t length() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    ResourcePath& append(std::string_view str);

    ResourcePath& makeRelative(const ResourcePath& base);
    ResourcePath getRelative(const ResourcePath& base) const;

    template<size_t N = 0>
    std::conditional_t<(N > 0), Str<N>, Divide::string> string() const noexcept
    {
        return std::conditional_t<(N > 0), Str<N>, Divide::string>( _fileSystemPath.string().c_str() );
    }

    PROPERTY_R_IW(std::filesystem::path, fileSystemPath);
};

ResourcePath  operator/ (const ResourcePath& lhs, const ResourcePath& rhs);
ResourcePath& operator/=(ResourcePath& lhs, const ResourcePath& rhs);

ResourcePath  operator/ ( const ResourcePath& lhs, std::string_view rhs );
ResourcePath& operator/=( ResourcePath& lhs, std::string_view rhs );

template<size_t N>
ResourcePath  operator/ ( const ResourcePath& lhs, const Str<N>& rhs )
{
    return lhs / ResourcePath( rhs );
}

template<size_t N>
ResourcePath& operator/=( ResourcePath& lhs, const Str<N>& rhs )
{
    return lhs /= ResourcePath( rhs );
}

bool operator== (const ResourcePath& lhs, std::string_view rhs);
bool operator!= (const ResourcePath& lhs, std::string_view rhs);

bool operator== (const ResourcePath& lhs, const ResourcePath& rhs);
bool operator!= (const ResourcePath& lhs, const ResourcePath& rhs);

struct FileNameAndPath
{
    Str<256> _fileName;
    ResourcePath _path;
};

bool operator==(const FileNameAndPath& lhs, const FileNameAndPath& rhs);
bool operator!=(const FileNameAndPath& lhs, const FileNameAndPath& rhs);

}; //namespace Divide

template<>
struct fmt::formatter<Divide::ResourcePath>
{
    constexpr auto parse( format_parse_context& ctx ) { return ctx.begin(); }

    template<typename FormatContext>
    auto format( Divide::ResourcePath const& path, FormatContext& ctx ) -> decltype(ctx.out())
    {
        return fmt::format_to( ctx.out(), "{}", path.string() );
    }
};

#endif //DVD_FILE_WITH_PATH_H_
