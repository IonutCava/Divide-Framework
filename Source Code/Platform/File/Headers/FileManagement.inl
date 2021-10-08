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

#ifndef _PLATFORM_FILE_FILE_MANAGEMENT_INL_
#define _PLATFORM_FILE_FILE_MANAGEMENT_INL_

#include "Core/Headers/StringHelper.h"

namespace Divide {

template<typename T,
typename std::enable_if<std::is_same<decltype(has_assign<T>(nullptr)), std::true_type>::value, bool>::type*>
FileError readFile(const char* filePath, const char* fileName, T& contentOut, const FileType fileType) {
    if (!Util::IsEmptyOrNull(filePath) && !Util::IsEmptyOrNull(fileName) && pathExists(filePath)) {
        std::ifstream streamIn(string{ filePath } +fileName,
                               fileType == FileType::BINARY
                                         ? std::ios::in | std::ios::binary
                                         : std::ios::in);

        if (!streamIn.eof() && !streamIn.fail()) {
            streamIn.seekg(0, std::ios::end);
            const auto fileSize = streamIn.tellg();

            if (fileSize > 0) {
                streamIn.seekg(0, std::ios::beg);
                optional_reserve(contentOut, fileSize);

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

template<typename T,
typename std::enable_if<std::is_same<decltype(has_assign<T>(nullptr)), std::true_type>::value, bool>::type*>
FileError readFile(const ResourcePath& filePath, const ResourcePath& fileName, T& contentOut, FileType fileType) {
    return readFile(filePath.c_str(), fileName.c_str(), contentOut, fileType);
}

//Optimized variant for vectors
template<>
inline FileError readFile(const char* filePath, const char* fileName, vector<Byte>& contentOut, const FileType fileType) {
    if (!Util::IsEmptyOrNull(filePath) && !Util::IsEmptyOrNull(fileName) && pathExists(filePath)) {
        std::ifstream streamIn(string{ filePath } +fileName,
                               fileType == FileType::BINARY
                                         ? std::ios::in | std::ios::binary
                                         : std::ios::in);

        if (!streamIn.eof() && !streamIn.fail()) {
            streamIn.seekg(0, std::ios::end);
            const auto fileSize = streamIn.tellg();
            if (fileSize > 0) {
                streamIn.seekg(0, std::ios::beg);

                contentOut.resize(fileSize);

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

#endif //_PLATFORM_FILE_FILE_MANAGEMENT_INL_
