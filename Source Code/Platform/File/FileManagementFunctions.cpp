#include "stdafx.h"

#include "Core/Headers/StringHelper.h"
#include "Headers/FileManagement.h"
#include "Headers/ResourcePath.h"

#include <filesystem>

namespace Divide {

std::string getWorkingDirectory() {
    return std::filesystem::current_path().generic_string();
}

FileError writeFile(const ResourcePath& filePath, const ResourcePath& fileName, const bufferPtr content, const size_t length, const FileType fileType) {
    return writeFile(filePath.c_str(), fileName.c_str(), static_cast<const char*>(content), length, fileType);
}

FileError writeFile(const char* filePath, const char* fileName, const bufferPtr content, const size_t length, const FileType fileType) {
    return writeFile(filePath, fileName, static_cast<const char*>(content), length, fileType);
}

FileError writeFile(const ResourcePath & filePath, const ResourcePath & fileName, const char* content, const size_t length, const FileType fileType) {
    return writeFile(filePath.c_str(), fileName.c_str(), content, length, fileType);
}

FileError writeFile(const char* filePath, const char* fileName, const char* content, const size_t length, const FileType fileType) {
    if (!Util::IsEmptyOrNull(filePath) && content != nullptr && length > 0) {
        if (!pathExists(filePath) && !CreateDirectories(filePath)) {
            return FileError::FILE_NOT_FOUND;
        }

        std::ofstream outputFile(string{ filePath } +fileName,
                                 fileType == FileType::BINARY
                                           ? std::ios::out | std::ios::binary
                                           : std::ios::out);

        outputFile.write(content, length);
        outputFile.close();
        if (!outputFile) {
            return FileError::FILE_WRITE_ERROR;
        }

        return FileError::NONE;
    }

    return FileError::FILE_NOT_FOUND;
}

string stripQuotes(const char* input) {
    string ret = input;

    if (!ret.empty()) {
        ret.erase(std::remove(std::begin(ret), std::end(ret), '\"'), std::end(ret));
    }

    return ret;
}

FileAndPath splitPathToNameAndLocation(const ResourcePath& input) {
    return splitPathToNameAndLocation(input.c_str());
}

FileAndPath splitPathToNameAndLocation(const char* input) {
    const auto targetPath = std::filesystem::path(input).lexically_normal();

    FileAndPath ret = {
      ResourcePath(targetPath.filename().generic_string()),
      ResourcePath(targetPath.parent_path().generic_string())
    };

    return ret;
}

bool pathCompare(const char* filePathA, const char* filePathB) {
    auto pathA = asPath(filePathA).lexically_normal();
    auto pathB = asPath(filePathB).lexically_normal();

    const bool ret = pathA.compare(pathB) == 0;
    if (!ret) {
        if (pathA.empty() || pathB.empty()) {
            return pathA.empty() == pathB.empty();
        }

        const bool pathATrailingSlash = filePathA[strlen(filePathA) - 1] == '/';
        const bool pathBTrailingSlash = filePathB[strlen(filePathB) - 1] == '/';
        if (pathATrailingSlash != pathBTrailingSlash) {
           if (pathATrailingSlash) {
               pathB += '/';
           } else {
               pathA += '/';
           }

           return pathA.compare(pathB) == 0;
        }

        return false;
    }
    
    return true;
}

bool pathExists(const ResourcePath& filePath) {
    return pathExists(filePath.c_str());
}

bool pathExists(const char* filePath) {
    return is_directory(std::filesystem::path(filePath));
}

bool createDirectory(const ResourcePath& path) {
    return createDirectory(path.c_str());
}

bool createDirectory(const char* path) {
    if (!pathExists(path)) {
        const auto targetPath = std::filesystem::path(path);
        std::error_code ec = {};
        const bool ret = create_directory(targetPath, ec);
        return ret;
    }

    return true;
}

bool fileExists(const ResourcePath& filePathAndName) {
    return fileExists(filePathAndName.c_str());
}

bool fileExists(const char* filePathAndName) {
    return is_regular_file(std::filesystem::path(filePathAndName));
}

bool fileExists(const char* filePath, const char* fileName) {
    return is_regular_file(std::filesystem::path(string{ filePath } + fileName));
}

bool fileIsEmpty(const ResourcePath& filePathAndName) {
    return fileIsEmpty(filePathAndName.c_str());
}

bool fileIsEmpty(const char* filePathAndName) {
    return std::filesystem::is_empty(std::filesystem::path(filePathAndName));
}

bool fileIsEmpty(const char* filePath, const char* fileName) {
    return std::filesystem::is_empty(std::filesystem::path(string{ filePath } + fileName));
}

bool createFile(const char* filePathAndName, const bool overwriteExisting) {
    if (overwriteExisting && fileExists(filePathAndName)) {
        const bool ret = std::ofstream(filePathAndName, std::fstream::in | std::fstream::trunc).good();
        return ret;
    }

    if (!CreateDirectories((const_sysInfo()._workingDirectory + splitPathToNameAndLocation(filePathAndName).second.str()).c_str())) {
        DebugBreak();
    }

    return std::ifstream(filePathAndName, std::fstream::in).good();
}

FileError openFile(const ResourcePath& filePath, const ResourcePath& fileName) {
    return openFile(filePath.c_str(), fileName.c_str());
}

FileError openFile(const char* filePath, const char* fileName) {
    return openFile("", filePath, fileName);
}

FileError openFile(const char* cmd, const ResourcePath& filePath, const ResourcePath& fileName) {
    return openFile(cmd, filePath.c_str(), fileName.c_str());
}

FileError openFile(const char* cmd, const char* filePath, const char* fileName) {
    if (Util::IsEmptyOrNull(fileName) || !fileExists(filePath, fileName)) {
        return FileError::FILE_NOT_FOUND;
    }

    constexpr std::array<std::string_view, 2> searchPattern = {
        "//", "\\"
    };

    const string file = "\"" + Util::ReplaceString(
        ResourcePath{ const_sysInfo()._workingDirectory + filePath + fileName }.str(),
        searchPattern, 
        "/", 
        true) + "\"";

    if (strlen(cmd) == 0) {
        const bool ret = CallSystemCmd(file.c_str(), "");
        return ret ? FileError::NONE : FileError::FILE_OPEN_ERROR;
    }

    const bool ret = CallSystemCmd(cmd, file.c_str());
    return ret ? FileError::NONE : FileError::FILE_OPEN_ERROR;
}

FileError deleteFile(const ResourcePath& filePath, const ResourcePath& fileName) {
    return deleteFile(filePath.c_str(), fileName.c_str());
}

FileError deleteFile(const char* filePath, const char* fileName) {
    if (Util::IsEmptyOrNull(fileName)) {
        return FileError::FILE_NOT_FOUND;
    }
    const std::filesystem::path file(string{ filePath } +fileName);
    if (std::filesystem::remove(file)) {
        return FileError::NONE;
    }

    return FileError::FILE_DELETE_ERROR;
}

FileError copyFile(const ResourcePath& sourcePath, const ResourcePath&  sourceName, const ResourcePath&  targetPath, const ResourcePath& targetName, const bool overwrite) {
    return copyFile(sourcePath.c_str(), sourceName.c_str(), targetPath.c_str(), targetName.c_str(), overwrite);
}

FileError copyFile(const char* sourcePath, const char* sourceName, const char* targetPath, const char* targetName, const bool overwrite) {
    if (Util::IsEmptyOrNull(sourceName) || Util::IsEmptyOrNull(targetName)) {
        return FileError::FILE_NOT_FOUND;
    }
    string source{ sourcePath };
    source.append("/");
    source.append(sourceName);

    if (!fileExists(source.c_str())) {
        return FileError::FILE_NOT_FOUND;
    }

    string target{ targetPath };
    target.append(targetName);

    if (!overwrite && fileExists(target.c_str())) {
        return FileError::FILE_OVERWRITE_ERROR;
    }

    if (copy_file(std::filesystem::path(source),
                  std::filesystem::path(target),
                  std::filesystem::copy_options::overwrite_existing)) {
        return FileError::NONE;
    }

    return FileError::FILE_COPY_ERROR;
}

FileError copyDirectory(const char* sourcePath, const char* targetPath, bool recursively, bool overwrite) {
    if (Util::IsEmptyOrNull(sourcePath) || Util::IsEmptyOrNull(targetPath)) {
        return FileError::FILE_NOT_FOUND;
    }
    if (!pathExists(sourcePath)) {
        return FileError::FILE_NOT_FOUND;
    }
    if (!overwrite && pathExists(targetPath)) {
        return FileError::FILE_OVERWRITE_ERROR;
    }
    try
    {
        std::filesystem::copy(sourcePath, 
                              targetPath,
                              (overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none) | 
                               (recursively ? std::filesystem::copy_options::recursive : std::filesystem::copy_options::none));
    }
    catch (std::exception& e)
    {
        std::cout << e.what();
        return FileError::FILE_COPY_ERROR;
    }

    return FileError::NONE;
}

FileError copyDirectory(const ResourcePath& sourcePath, const ResourcePath& targetPath, bool recursively, bool overwrite) {
    return copyDirectory(sourcePath.c_str(), targetPath.c_str(), recursively, overwrite);
}

FileError findFile(const ResourcePath& filePath, const char* fileName, string& foundPath) {
    return findFile(filePath.c_str(), fileName, foundPath);
}

FileError findFile(const char* filePath, const char* fileName, string& foundPath) {
    const std::filesystem::path dir_path(filePath);
    std::filesystem::path file_name(fileName);

    const std::filesystem::recursive_directory_iterator end;
    const auto it = std::find_if(std::filesystem::recursive_directory_iterator(dir_path), end,
        [&file_name](const std::filesystem::directory_entry& e) {
        const bool ret = e.path().filename() == file_name;
        return ret;
    });
    if (it == end) {
        return FileError::FILE_NOT_FOUND;
    }

    foundPath = it->path().string();
    return FileError::NONE;
}

bool hasExtension(const ResourcePath& filePath, const Str16& extension) {
    return hasExtension(filePath.c_str(), extension);
}

bool hasExtension(const char* filePath, const Str16& extension) {
    const Str16 ext("." + extension);
    return Util::CompareIgnoreCase(Util::GetTrailingCharacters(string_fast{ filePath }, ext.length()), ext);
}

bool deleteAllFiles(const ResourcePath& filePath, const char* extension) {
    return deleteAllFiles(filePath.c_str(), extension);
}

bool deleteAllFiles(const char* filePath, const char* extension) {
    bool ret = false;

    if (pathExists(filePath)) {
        const std::filesystem::path pathIn(filePath);
        for (const auto& p : std::filesystem::directory_iterator(pathIn)) {
            try {
                if (is_regular_file(p.status())) {
                    if (!extension || extension != nullptr && p.path().extension() == extension) {
                        if (std::filesystem::remove(p.path())) {
                            ret = true;
                        }
                    }
                } else {
                    //ToDo: check if this recurse in subfolders actually works
                    if (!deleteAllFiles(p.path().string().c_str(), extension)) {
                        NOP();
                    }
                }
            } catch (const std::exception &ex) {
                ACKNOWLEDGE_UNUSED(ex);
            }
        }
    }

    return ret;
}

bool clearCache() {
    for (U8 i = 0; i < to_U8(CacheType::COUNT); ++i) {
        if (!clearCache(static_cast<CacheType>(i))) {
            return false;
        }
    }
    return true;
}

bool clearCache(const CacheType type) {
    ResourcePath cache;
    switch (type) {
        case CacheType::SHADER_TEXT: cache = Paths::Shaders::g_cacheLocationText; break;
        case CacheType::SHADER_BIN : cache = Paths::Shaders::g_cacheLocationBin; break;
        case CacheType::TERRAIN    : cache = Paths::g_terrainCacheLocation; break;
        case CacheType::MODELS     : cache = Paths::g_geometryCacheLocation; break;
        case CacheType::COLLISION  : cache = Paths::g_collisionMeshCacheLocation; break;
        case CacheType::TEXTURES   : cache = Paths::Textures::g_metadataLocation; break;

        case CacheType::COUNT:
        default: return false;
    }

    return deleteAllFiles(Paths::g_cacheLocation + cache);
}

std::string extractFilePathAndName(char* argv0) {
    auto currentPath = std::filesystem::current_path();
    currentPath.append(argv0);

    std::error_code ec;
    std::filesystem::path p(canonical(currentPath, ec));

    return p.make_preferred().string();
}

}; //namespace Divide
