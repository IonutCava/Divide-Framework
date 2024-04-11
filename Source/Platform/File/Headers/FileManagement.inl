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

#ifndef DVD_PLATFORM_FILE_FILE_MANAGEMENT_INL_
#define DVD_PLATFORM_FILE_FILE_MANAGEMENT_INL_

#include "Core/Headers/StringHelper.h"

namespace Divide {

inline FileError writeFile( const ResourcePath& filePath, const std::string_view fileName, const bufferPtr content, const size_t length, const FileType fileType )
{
    return writeFile( filePath, fileName, static_cast<const char*>(content), length, fileType );
}

inline bool fileExists( const ResourcePath& filePath, const std::string_view fileName )
{
    return fileExists( filePath / fileName );
}

inline bool fileIsEmpty( const ResourcePath& filePath, const std::string_view fileName )
{
    return fileIsEmpty( filePath / fileName );
}

inline FileError fileLastWriteTime( const ResourcePath& filePath, const std::string_view fileName, U64& timeOutSec )
{
    return fileLastWriteTime( filePath / fileName, timeOutSec );
}

inline FileError openFile( const ResourcePath& filePath, const std::string_view fileName )
{
    return openFile( "", filePath, fileName );
}

template<typename T> requires has_assign<T> || is_vector<T>
FileError readFile( const ResourcePath& filePath, const std::string_view fileName, T& contentOut, const FileType fileType )
{
    if (!filePath.empty() && !fileName.empty() && pathExists(filePath))
    {
        std::ifstream streamIn((filePath / fileName).string(),
                               fileType == FileType::BINARY
                                         ? std::ios::in | std::ios::binary
                                         : std::ios::in);

        if (!streamIn.eof() && !streamIn.fail()) {
            streamIn.seekg(0, std::ios::end);
            const auto fileSize = streamIn.tellg();

            if (fileSize > 0) {
                streamIn.seekg(0, std::ios::beg);
                optional_reserve(contentOut, to_size(fileSize));

                static_assert(sizeof(char) == sizeof(Byte), "readFile: Platform error!");
                contentOut.assign(string
                    {
                        std::istreambuf_iterator<char>(streamIn),
                        std::istreambuf_iterator<char>()
                    }.c_str());

                return FileError::NONE;
            }

            streamIn.close();
            return FileError::FILE_EMPTY;
        }

        streamIn.close();
        return FileError::FILE_READ_ERROR;
    }

    return FileError::FILE_NOT_FOUND;
}


//Optimized variant for vectors
template<>
inline FileError readFile(const ResourcePath& filePath, const std::string_view fileName, vector<Byte>& contentOut, const FileType fileType)
{
    if ( !filePath.empty() && !fileName.empty() && pathExists(filePath))
    {
        std::ifstream streamIn((filePath / fileName).string(),
                               fileType == FileType::BINARY
                                         ? std::ios::in | std::ios::binary
                                         : std::ios::in);

        if (!streamIn.eof() && !streamIn.fail())
        {
            streamIn.seekg(0, std::ios::end);
            const auto fileSize = streamIn.tellg();
            if (fileSize > 0)
            {
                streamIn.seekg(0, std::ios::beg);

                contentOut.resize(to_size(fileSize));

                static_assert(sizeof(char) == sizeof(Byte), "readFile: Platform error!");
                Byte* outBuffer = contentOut.data();
                streamIn.read(reinterpret_cast<char*>(outBuffer), fileSize);
                return FileError::NONE;
            }

            streamIn.close();
            return FileError::FILE_EMPTY;
        }

        streamIn.close();
        return FileError::FILE_READ_ERROR;
    }

    return FileError::FILE_NOT_FOUND;
}

}; //namespace Divide

#endif //DVD_PLATFORM_FILE_FILE_MANAGEMENT_INL_
