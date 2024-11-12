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
#ifndef DVD_PLATFORM_FILE_FILE_MANAGEMENT_H_
#define DVD_PLATFORM_FILE_FILE_MANAGEMENT_H_

namespace Divide {

enum class FileError : U8 {
    NONE = 0,
    FILE_NOT_FOUND,
    FILE_EMPTY,
    FILE_CREATE_ERROR,
    FILE_READ_ERROR,
    FILE_OPEN_ERROR,
    FILE_WRITE_ERROR,
    FILE_DELETE_ERROR,
    FILE_OVERWRITE_ERROR,
    FILE_COPY_ERROR,
    FILE_MOVE_ERROR,
    FILE_TARGET_BUFFER_ERROR,
    COUNT
};

namespace Names
{
    static const char* fileError[] = {
            "NONE", "FILE_NOT_FOUND", "FILE_EMPTY", "FILE_CREATE_ERROR","FILE_READ_ERROR", "FILE_OPEN_ERROR", "FILE_WRITE_ERROR", "FILE_DELETE_ERROR", "FILE_OVERWRITE_ERROR", "FILE_COPY_ERROR", "FILE_MOVE_ERROR", "FILE_TARGET_BUFFER_ERROR", "UNKNOWN"
    };
}

struct SysInfo;
class PlatformContext;
struct Paths {
    constexpr static const char g_pathSeparator =
#if defined(IS_WINDOWS_BUILD)
        '\\';
#else //IS_WINDOWS_BUILD
        '/';
#endif //IS_WINDOWS_BUILD

    static ResourcePath g_logPath;
    static ResourcePath g_screenshotPath;
    static ResourcePath g_assetsLocation;
    static ResourcePath g_modelsLocation;
    static ResourcePath g_shadersLocation;
    static ResourcePath g_texturesLocation;
    static ResourcePath g_proceduralTexturesLocation;
    static ResourcePath g_heightmapLocation;
    static ResourcePath g_climatesLowResLocation;
    static ResourcePath g_climatesMedResLocation;
    static ResourcePath g_climatesHighResLocation;
    static ResourcePath g_imagesLocation;
    static ResourcePath g_materialsLocation;
    static ResourcePath g_soundsLocation;
    static ResourcePath g_xmlDataLocation;
    static ResourcePath g_navMeshesLocation;
    static ResourcePath g_scenesLocation;
    static ResourcePath g_projectsLocation;
    static ResourcePath g_saveLocation;
    static ResourcePath g_nodesSaveLocation;
    static ResourcePath g_GUILocation;
    static ResourcePath g_fontsPath;
    static ResourcePath g_iconsPath;
    static ResourcePath g_localisationPath;
    static ResourcePath g_cacheLocation;
    static ResourcePath g_buildTypeLocation;
    static ResourcePath g_terrainCacheLocation;
    static ResourcePath g_geometryCacheLocation;
    static ResourcePath g_collisionMeshCacheLocation;

    struct Editor {
        static ResourcePath g_saveLocation;
    };

    struct Scripts {
        static ResourcePath g_scriptsLocation;
        static ResourcePath g_scriptsAtomsLocation;
    };

    struct Textures {
        static ResourcePath g_metadataLocation;
        static Str<8> g_ddsExtension;
    };

    struct Shaders {
        static ResourcePath g_cacheLocation;
        static ResourcePath g_cacheLocationGL;
        static ResourcePath g_cacheLocationVK;
        static ResourcePath g_cacheLocationText;
        static ResourcePath g_cacheLocationSpv;
        static ResourcePath g_cacheLocationRefl;

        static Str<8> g_ReflectionExt;
        static Str<8> g_SPIRVExt;
        // Shader subfolder name that contains SPIRV shader files
        static ResourcePath g_SPIRVShaderLoc;

        struct GLSL {
            // these must match the last 4 characters of the atom file
            static Str<8> g_fragAtomExt;
            static Str<8> g_vertAtomExt;
            static Str<8> g_geomAtomExt;
            static Str<8> g_tescAtomExt;
            static Str<8> g_teseAtomExt;
            static Str<8> g_compAtomExt;
            static Str<8> g_meshAtomExt;
            static Str<8> g_taskAtomExt;
            static Str<8> g_comnAtomExt;

            // Shader subfolder name that contains GLSL shader files
            static ResourcePath g_GLSLShaderLoc;
            // Atom folder names in parent shader folder
            static ResourcePath g_fragAtomLoc;
            static ResourcePath g_vertAtomLoc;
            static ResourcePath g_geomAtomLoc;
            static ResourcePath g_tescAtomLoc;
            static ResourcePath g_teseAtomLoc;
            static ResourcePath g_compAtomLoc;
            static ResourcePath g_meshAtomLoc;
            static ResourcePath g_taskAtomLoc;
            static ResourcePath g_comnAtomLoc;
        }; //class GLSL
    }; //class Shaders

    // include command regex pattern
    static constexpr auto g_includePattern = ctll::fixed_string{ R"(\s*#\s*include\s+["<]([^">]+)*[">])" };
    // define regex pattern
    static constexpr auto g_definePattern = ctll::fixed_string{ R"(([#!][A-z]{2,}[\s]{1,}?([A-z]{2,}[\s]{1,}?)?)([\\(]?[^\s\\)]{1,}[\\)]?)?)" };
    // use command regex pattern
    static constexpr auto g_usePattern = ctll::fixed_string{ R"(\s*use\s*\(\s*\"(.+)\"\s*\).*)" };
    // shader uniform patter
    static constexpr auto g_uniformPattern = ctll::fixed_string{ R"(\s*uniform\s+\s*([^),^;^\s]*)\s+([^),^;^\s]*\[*\s*\]*)\s*(?:=*)\s*(?:\d*.*)\s*(?:;+).*)" };
    // project specifying command line argument
    static constexpr auto g_useProjectPattern = ctll::fixed_string{ R"((--project\s*=\s*)([0-9a-zA-Z]*))" };

    static void initPaths();
}; //class Paths

struct FileEntry
{
    ResourcePath _name{};
    U64 _lastWriteTime{0u};
};
using FileList = vector<FileEntry>;

[[nodiscard]] ResourcePath getWorkingDirectory();

[[nodiscard]] bool pathExists(const ResourcePath& filePath);

[[nodiscard]] bool fileExists(const ResourcePath& filePathAndName);
[[nodiscard]] bool fileExists(const ResourcePath& filePath, std::string_view fileName);

[[nodiscard]] bool fileIsEmpty(const ResourcePath& filePathAndName);
[[nodiscard]] bool fileIsEmpty(const ResourcePath& filePath, std::string_view fileName);

[[nodiscard]] FileError createDirectory(const ResourcePath& path);
[[nodiscard]] FileError removeDirectory( const ResourcePath& path );

[[nodiscard]] bool createFile(const ResourcePath& filePathAndName, bool overwriteExisting);

[[nodiscard]] bool deleteAllFiles(const ResourcePath& filePath, const char* extensionNoDot = nullptr, const char* extensionToSkip = nullptr);

[[nodiscard]] bool getAllFilesInDirectory(const ResourcePath& filePath, FileList& listInOut, const char* extensionNoDot = nullptr);

[[nodiscard]] FileError fileLastWriteTime(const ResourcePath& filePathAndName, U64& timeOutSec);
[[nodiscard]] FileError fileLastWriteTime(const ResourcePath& filePath, std::string_view fileName, U64& timeOutSec);

[[nodiscard]] size_t numberOfFilesInDirectory(const ResourcePath& path);

/// Read the contents of a file into an ifstream. Will not close the stream!
[[nodiscard]] FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, std::ifstream& sreamOut);
[[nodiscard]] FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, string& contentOut);
[[nodiscard]] FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, std::string& contentOut);
[[nodiscard]] FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, Byte* contentOut, size_t& sizeInOut);

[[nodiscard]] FileError openFile(const ResourcePath& filePath, std::string_view fileName);
[[nodiscard]] FileError openFile(std::string_view cmd, const ResourcePath& filePath, std::string_view fileName);

[[nodiscard]] FileError writeFile(const ResourcePath& filePath, std::string_view fileName, bufferPtr content, size_t length, FileType fileType);
[[nodiscard]] FileError writeFile(const ResourcePath& filePath, std::string_view fileName, const char* content, size_t length, FileType fileType);

[[nodiscard]] FileError deleteFile(const ResourcePath& filePath, std::string_view fileName);

[[nodiscard]] FileError copyFile(const ResourcePath& sourcePath, std::string_view sourceName, const ResourcePath& targetPath, std::string_view targetName, bool overwrite);

[[nodiscard]] FileError moveFile(const ResourcePath& sourcePath, std::string_view sourceName, const ResourcePath& targetPath, std::string_view targetName);

[[nodiscard]] FileError copyDirectory(const ResourcePath& sourcePath, const ResourcePath& targetPath, bool recursively, bool overwrite);

[[nodiscard]] FileError findFile(const ResourcePath& filePath, std::string_view fileName, string& foundPath);

[[nodiscard]] bool hasExtension(std::string_view fileName, std::string_view extensionNoDot);

[[nodiscard]] bool hasExtension(const ResourcePath& filePath, std::string_view extensionNoDot);

[[nodiscard]] string getExtension(std::string_view fileName );

[[nodiscard]] string getExtension(const ResourcePath& filePath );

[[nodiscard]] ResourcePath getTopLevelFolderName(const ResourcePath& filePath);

[[nodiscard]] string stripExtension(std::string_view fileName ) noexcept;

[[nodiscard]] ResourcePath stripExtension(const ResourcePath& filePath) noexcept;

[[nodiscard]] string stripQuotes( std::string_view input);

[[nodiscard]] FileNameAndPath splitPathToNameAndLocation(const ResourcePath& input);

[[nodiscard]] string extractFilePathAndName(char* argv0);

}; //namespace Divide

#endif //DVD_PLATFORM_FILE_FILE_MANAGEMENT_H_

#include "FileManagement.inl"
