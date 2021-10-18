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
#ifndef _PLATFORM_FILE_FILE_MANAGEMENT_H_
#define _PLATFORM_FILE_FILE_MANAGEMENT_H_

namespace Divide {

enum class CacheType : U8 {
    SHADER_TEXT = 0,
    SHADER_BIN,
    TERRAIN,
    MODELS,
    COLLISION,
    TEXTURES,
    COUNT
};

enum class FileError : U8 {
    NONE = 0,
    FILE_NOT_FOUND,
    FILE_EMPTY,
    FILE_READ_ERROR,
    FILE_OPEN_ERROR,
    FILE_WRITE_ERROR,
    FILE_DELETE_ERROR,
    FILE_OVERWRITE_ERROR,
    FILE_COPY_ERROR,
    COUNT
};
struct SysInfo;
class PlatformContext;
struct Paths {
    static ResourcePath g_rootPath;
    static ResourcePath g_logPath;
    static ResourcePath g_assetsLocation;
    static ResourcePath g_shadersLocation;
    static ResourcePath g_texturesLocation;
    static ResourcePath g_proceduralTxturesLocation;
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
    static ResourcePath g_saveLocation;
    static ResourcePath g_GUILocation;
    static ResourcePath g_fontsPath;
    static ResourcePath g_localisationPath;
    static ResourcePath g_cacheLocation;
    static ResourcePath g_terrainCacheLocation;
    static ResourcePath g_geometryCacheLocation;
    static ResourcePath g_collisionMeshCacheLocation;

    struct Editor {
        static ResourcePath g_saveLocation;
        static ResourcePath g_tabLayoutFile;
        static ResourcePath g_panelLayoutFile;
    };

    struct Scripts {
        static ResourcePath g_scriptsLocation;
        static ResourcePath g_scriptsAtomsLocation;
    };

    struct Textures {
        static ResourcePath g_metadataLocation;
    };

    struct Shaders {
        static ResourcePath g_cacheLocation;
        static ResourcePath g_cacheLocationText;
        static ResourcePath g_cacheLocationBin;

        struct GLSL {
            // these must match the last 4 characters of the atom file
            static Str8 g_fragAtomExt;
            static Str8 g_vertAtomExt;
            static Str8 g_geomAtomExt;
            static Str8 g_tescAtomExt;
            static Str8 g_teseAtomExt;
            static Str8 g_compAtomExt;
            static Str8 g_comnAtomExt;

            // Shader subfolder name that contains shader files for OpenGL
            static ResourcePath g_parentShaderLoc;
            // Atom folder names in parent shader folder
            static ResourcePath g_fragAtomLoc;
            static ResourcePath g_vertAtomLoc;
            static ResourcePath g_geomAtomLoc;
            static ResourcePath g_tescAtomLoc;
            static ResourcePath g_teseAtomLoc;
            static ResourcePath g_compAtomLoc;
            static ResourcePath g_comnAtomLoc;
        }; //class GLSL
        struct HLSL {
            static ResourcePath g_parentShaderLoc;
        }; //class HLSL
    }; //class Shaders

    // include command regex pattern
    static std::regex g_includePattern;
    // define regex pattern
    static std::regex g_definePattern;
    // use command regex pattern
    static std::regex g_usePattern;

    
    static void initPaths(const SysInfo& info);
    static void updatePaths(const PlatformContext& context);
}; //class Paths


[[nodiscard]] std::string getWorkingDirectory();

//returns true if both paths are identical regardless of number of slashes and capitalization

using asPath = std::filesystem::path;

[[nodiscard]] bool pathCompare(const char* filePathA, const char* filePathB);
[[nodiscard]] bool pathExists(const char* filePath);
[[nodiscard]] bool pathExists(const ResourcePath& filePath);
[[nodiscard]] bool fileExists(const char* filePathAndName);
[[nodiscard]] bool fileExists(const ResourcePath& filePathAndName);
[[nodiscard]] bool fileExists(const char* filePath, const char* filename);
[[nodiscard]] bool fileIsEmpty(const char* filePathAndName);
[[nodiscard]] bool fileIsEmpty(const ResourcePath& filePathAndName);
[[nodiscard]] bool fileIsEmpty(const char* filePath, const char* filename);
[[nodiscard]] bool createDirectory(const char* path);
[[nodiscard]] bool createDirectory(const ResourcePath& path);
[[nodiscard]] bool createFile(const char* filePathAndName, bool overwriteExisting);
[[nodiscard]] bool deleteAllFiles(const char* filePath, const char* extension = nullptr);
[[nodiscard]] bool deleteAllFiles(const ResourcePath& filePath, const char* extension = nullptr);

template<typename T,
typename std::enable_if<std::is_same<decltype(has_assign<T>(nullptr)), std::true_type>::value, bool>::type* = nullptr>
[[nodiscard]] FileError readFile(const char* filePath, const char* fileName, T& contentOut, FileType fileType);
template<typename T,
typename std::enable_if<std::is_same<decltype(has_assign<T>(nullptr)), std::true_type>::value, bool>::type* = nullptr>
[[nodiscard]] FileError readFile(const ResourcePath& filePath, const ResourcePath& fileName, T& contentOut, FileType fileType);

[[nodiscard]] FileError openFile(const char* filePath, const char* fileName);
[[nodiscard]] FileError openFile(const ResourcePath& filePath, const ResourcePath& fileName);

[[nodiscard]] FileError openFile(const char* cmd, const char* filePath, const char* fileName);
[[nodiscard]] FileError openFile(const char* cmd, const ResourcePath& filePath, const ResourcePath& fileName);

[[nodiscard]] FileError writeFile(const ResourcePath& filePath, const ResourcePath& fileName, const char* content, size_t length, FileType fileType);
[[nodiscard]] FileError writeFile(const ResourcePath& filePath, const ResourcePath& fileName, bufferPtr content, size_t length, FileType fileType);
[[nodiscard]] FileError writeFile(const char* filePath, const char* fileName, bufferPtr content, size_t length, FileType fileType);
[[nodiscard]] FileError writeFile(const char* filePath, const char* fileName, const char* content, size_t length, FileType fileType);

[[nodiscard]] FileError deleteFile(const char* filePath, const char* fileName);
[[nodiscard]] FileError deleteFile(const ResourcePath& filePath, const ResourcePath& fileName);

[[nodiscard]] FileError copyFile(const char* sourcePath, const char* sourceName, const char* targetPath, const char* targetName, bool overwrite);
[[nodiscard]] FileError copyFile(const ResourcePath& sourcePath, const ResourcePath&  sourceName, const ResourcePath&  targetPath, const ResourcePath& targetName, bool overwrite);

[[nodiscard]] FileError copyDirectory(const char* sourcePath, const char* targetPath, bool recursively, bool overwrite);
[[nodiscard]] FileError copyDirectory(const ResourcePath& sourcePath, const ResourcePath& targetPath, bool recursively, bool overwrite);

[[nodiscard]] FileError findFile(const char* filePath, const char* fileName, string& foundPath);
[[nodiscard]] FileError findFile(const ResourcePath& filePath, const char* fileName, string& foundPath);

/// will add '.' automatically at the start of 'extension'
[[nodiscard]] bool hasExtension(const char* filePath, const Str16& extension);
[[nodiscard]] bool hasExtension(const ResourcePath& filePath, const Str16& extension);

[[nodiscard]] string stripQuotes(const char* input);

[[nodiscard]] FileAndPath splitPathToNameAndLocation(const char* input);
[[nodiscard]] FileAndPath splitPathToNameAndLocation(const ResourcePath& input);

[[nodiscard]] bool clearCache();
[[nodiscard]] bool clearCache(CacheType type);

[[nodiscard]] std::string extractFilePathAndName(char* argv0);

}; //namespace Divide

#endif //_PLATFORM_FILE_FILE_MANAGEMENT_H_

#include "FileManagement.inl"
