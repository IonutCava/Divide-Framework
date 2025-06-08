
#include "Headers/FileManagement.h"
#include "Headers/ResourcePath.h"

#include <filesystem>

namespace Divide {

namespace 
{
    constexpr U8 s_maxIORetries = 3u;
}

ResourcePath getWorkingDirectory()
{
    return ResourcePath { std::filesystem::current_path().lexically_normal().string() };
}

FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, std::ifstream& sreamOut)
{
    if (filePath.empty() || fileName.empty() || !pathExists(filePath))
    {
        return FileError::FILE_NOT_FOUND;
    }

    sreamOut = std::ifstream((filePath / fileName).fileSystemPath(),
                              fileType == FileType::BINARY
                                        ? std::ios::in | std::ios::binary
                                        : std::ios::in);


    return (sreamOut.eof() || sreamOut.fail()) 
                            ? FileError::FILE_READ_ERROR
                            : FileError::NONE;
}

FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, string& contentOut)
{
    std::ifstream streamIn;
    const FileError ret = readFile(filePath, fileName, fileType, streamIn);
    if ( ret != FileError::NONE)
    {
       return ret;
    }

    std::stringstream buffer;
    buffer << streamIn.rdbuf();
    contentOut = buffer.str();

    return contentOut.empty() ? FileError::FILE_EMPTY : FileError::NONE;
}

FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, std::string& contentOut)
{
    std::ifstream streamIn;
    const FileError ret = readFile(filePath, fileName, fileType, streamIn);
    if ( ret != FileError::NONE)
    {
       return ret;
    }

    std::stringstream buffer;
    buffer << streamIn.rdbuf();
    contentOut = buffer.str();

    return contentOut.empty() ? FileError::FILE_EMPTY : FileError::NONE;
}

FileError readFile(const ResourcePath& filePath, std::string_view fileName, FileType fileType, Byte* contentOut, size_t& sizeInOut)
{
    if (contentOut == nullptr || sizeInOut == 0u)
    {
        return FileError::FILE_TARGET_BUFFER_ERROR;
    }

    std::ifstream streamIn;
    const FileError ret = readFile(filePath, fileName, fileType, streamIn);
    if ( ret != FileError::NONE)
    {
       return ret;
    }

    streamIn.seekg(0, std::ios::end);
    const size_t fileSize = to_size(streamIn.tellg());
    streamIn.seekg(0);
    if (fileSize == 0u)
    {
        return FileError::FILE_EMPTY;
    }

    std::memset(contentOut, 0, sizeInOut * sizeof(Byte));
    sizeInOut = std::min(sizeInOut, fileSize);
    streamIn.read(reinterpret_cast<char*>(contentOut), sizeInOut);
 
    return FileError::NONE;
}

FileError writeFile(const ResourcePath& filePath, const std::string_view fileName, const char* content, const size_t length, const FileType fileType)
{
    if (!filePath.empty() && content != nullptr && length > 0)
    {
        if (!pathExists(filePath) && createDirectory(filePath) != FileError::NONE)
        {
            return FileError::FILE_NOT_FOUND;
        }

        std::ofstream outputFile((filePath / fileName).fileSystemPath(),
                                 fileType == FileType::BINARY
                                           ? std::ios::out | std::ios::binary
                                           : std::ios::out);

        outputFile.write(content, length);
        outputFile.close();
        if (!outputFile)
        {
            return FileError::FILE_WRITE_ERROR;
        }

        return FileError::NONE;
    }

    return FileError::FILE_NOT_FOUND;
}

string stripQuotes( const std::string_view input)
{

    if (input.empty())
    {
        return "";
    }

    string ret { input };
    ret.erase(std::remove(std::begin(ret), std::end(ret), '\"'), std::end(ret));
    return ret;
}

FileNameAndPath splitPathToNameAndLocation(const ResourcePath& input)
{
    return FileNameAndPath
    {
        input.fileSystemPath().filename().generic_string().c_str(),
        ResourcePath( input.fileSystemPath().parent_path().generic_string().c_str() )
    };
}

bool pathExists(const ResourcePath& filePath)
{
    std::error_code ec;
    const bool ret = is_directory(filePath.fileSystemPath(), ec);
    return ec ? false : ret;
}

FileError createDirectoryInternal(const ResourcePath& path)
{
    NO_DESTROY static Mutex s_DirectoryLock;

    LockGuard<Mutex> w_lock( s_DirectoryLock );

    if (!pathExists(path))
    {
        std::error_code ec;
        const bool ret = create_directories(path.fileSystemPath(), ec);
        if (ec)
        {
            return FileError::FILE_CREATE_ERROR;
        }

        if (!ret)
        {
            return pathExists(path) ? FileError::NONE : FileError::FILE_CREATE_ERROR;
        }
    }

    return FileError::NONE;
}

FileError createDirectory(const ResourcePath& path)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do 
    {
        ret = createDirectoryInternal(path);
    }
    while(ret == FileError::FILE_CREATE_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

FileError removeDirectoryInternal( const ResourcePath& path )
{
    if ( pathExists(path) )
    {
        std::error_code ec;
        if (std::filesystem::remove_all(path.fileSystemPath(), ec) == 0)
        {
            NOP();
        }

        if (ec)
        {
            return FileError::FILE_DELETE_ERROR;
        }
    }

    return FileError::NONE;
}

FileError removeDirectory(const ResourcePath& path)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do
    {
        ret = removeDirectoryInternal(path);
    } while (ret == FileError::FILE_DELETE_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

bool fileExists(const ResourcePath& filePathAndName)
{
    std::error_code ec;
    const bool result = is_regular_file(filePathAndName.fileSystemPath(), ec);
    return ec ? false : result;
}

bool fileIsEmpty(const ResourcePath& filePathAndName)
{
    std::error_code ec;
    const bool result = std::filesystem::is_empty(filePathAndName.fileSystemPath(), ec);
    return ec ? false : result;
}

FileError fileLastWriteTime(const ResourcePath& filePathAndName, U64& timeOutSec)
{
    if (filePathAndName.empty() || !fileExists(filePathAndName))
    {
        return FileError::FILE_NOT_FOUND;
    }

    std::error_code ec;
    const auto timeStamp = std::filesystem::last_write_time(filePathAndName.fileSystemPath(), ec).time_since_epoch();
    if (ec)
    {
        return FileError::FILE_READ_ERROR;
    }

    timeOutSec = to_U64(std::chrono::duration_cast<std::chrono::seconds>(timeStamp).count());

    return FileError::NONE;
}

size_t numberOfFilesInDirectory( const ResourcePath& path )
{
     return std::count_if( std::filesystem::directory_iterator( path.fileSystemPath() ),
                          std::filesystem::directory_iterator{},
                          [](const std::filesystem::path& p){ return std::filesystem::is_regular_file( p ); });
}

bool createFileInternal(const ResourcePath& filePathAndName, const bool overwriteExisting)
{
    if (overwriteExisting && fileExists(filePathAndName))
    {
        return std::ofstream(filePathAndName.string().c_str(), std::fstream::in | std::fstream::trunc).good();
    }

    DIVIDE_EXPECTED_CALL(createDirectory(const_sysInfo()._workingDirectory / splitPathToNameAndLocation(filePathAndName)._path) == FileError::NONE );

    return std::ifstream(filePathAndName.string().c_str(), std::fstream::in).good();
}

bool createFile(const ResourcePath& filePathAndName, const bool overwriteExisting)
{
    U8 retryCount = 0u;
    bool state = true;

    do
    {
        state = createFileInternal(filePathAndName, overwriteExisting);
    } while (!state && ++retryCount < s_maxIORetries);

    return state;
}

FileError openFile(const std::string_view cmd, const ResourcePath& filePath, const std::string_view fileName)
{
    if (fileName.empty() || !fileExists(filePath, fileName))
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath file = const_sysInfo()._workingDirectory / filePath / fileName;

    bool ret = false;
    if (cmd.empty())
    {
        ret = CallSystemCmd(file.string(), "");
    }
    else
    {
        ret = CallSystemCmd( cmd, file.string() );
    }

    return ret ? FileError::NONE : FileError::FILE_OPEN_ERROR;
}

FileError deleteFileInternal(const ResourcePath& filePath, const std::string_view fileName)
{
    if ( fileName.empty() )
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath fullPath{ filePath / fileName };
    if (!fileExists(fullPath))
    {
        return FileError::FILE_NOT_FOUND;
    }

    std::error_code ec;
    if (std::filesystem::remove(fullPath.fileSystemPath(), ec))
    {
        return FileError::NONE;
    }

    return FileError::FILE_DELETE_ERROR;
}

FileError deleteFile(const ResourcePath& filePath, const std::string_view fileName)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do
    {
        ret = deleteFileInternal(filePath, fileName);
    } while (ret == FileError::FILE_DELETE_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

FileError moveFileInternal(const ResourcePath& sourcePath, const std::string_view sourceName, const ResourcePath& targetPath, const std::string_view targetName)
{
    if (sourceName.empty() || targetName.empty())
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath source{ sourcePath / sourceName };

    if (!fileExists(source))
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath target{ targetPath / targetName };

    std::error_code ec;
    rename(source.fileSystemPath(), target.fileSystemPath(), ec);
    if (!ec)
    {
        return FileError::NONE;
    }

    return FileError::FILE_MOVE_ERROR;
}

FileError moveFile(const ResourcePath& sourcePath, const std::string_view sourceName, const ResourcePath& targetPath, const std::string_view targetName)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do
    {
        ret = moveFileInternal(sourcePath, sourceName, targetPath, targetName);
    } while (ret == FileError::FILE_MOVE_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

FileError copyFileInternal(const ResourcePath& sourcePath, const std::string_view sourceName, const ResourcePath&  targetPath, const std::string_view targetName, const bool overwrite)
{
    if (sourceName.empty() || targetName.empty())
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath source{ sourcePath / sourceName };

    if (!fileExists(source))
    {
        return FileError::FILE_NOT_FOUND;
    }

    const ResourcePath target{ targetPath / targetName };

    if (!overwrite && fileExists(target))
    {
        return FileError::FILE_OVERWRITE_ERROR;
    }

    std::error_code ec;
    if (copy_file(source.fileSystemPath(),
                  target.fileSystemPath(),
                  std::filesystem::copy_options::overwrite_existing,
                  ec) && !ec)
    {
        return FileError::NONE;
    }

    return FileError::FILE_COPY_ERROR;
}

FileError copyFile(const ResourcePath& sourcePath, const std::string_view sourceName, const ResourcePath& targetPath, const std::string_view targetName, const bool overwrite)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do
    {
        ret = copyFileInternal(sourcePath, sourceName, targetPath, targetName, overwrite);
    } while (ret == FileError::FILE_COPY_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

FileError copyDirectoryInternal( const ResourcePath& sourcePath, const ResourcePath& targetPath, bool recursively, bool overwrite )
{
    if (!pathExists(sourcePath))
    {
        return FileError::FILE_NOT_FOUND;
    }

    if (!overwrite && pathExists(targetPath))
    {
        return FileError::FILE_OVERWRITE_ERROR;
    }
    
    std::error_code ec;
    if ( !std::filesystem::exists( targetPath.fileSystemPath(), ec ) )
    {
        std::filesystem::create_directories( targetPath.fileSystemPath(), ec );
    }

    if ( !ec ) 
    {
        std::filesystem::copy(sourcePath.fileSystemPath(), 
                              targetPath.fileSystemPath(),
                                (overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none) | 
                                (recursively ? std::filesystem::copy_options::recursive : std::filesystem::copy_options::none),
                              ec);
    }

    return ec ? FileError::FILE_COPY_ERROR: FileError::NONE;
}

FileError copyDirectory(const ResourcePath& sourcePath, const ResourcePath& targetPath, bool recursively, bool overwrite)
{
    U8 retryCount = 0u;
    FileError ret = FileError::NONE;
    do
    {
        ret = copyDirectoryInternal(sourcePath, targetPath, recursively, overwrite);
    } while (ret == FileError::FILE_COPY_ERROR && ++retryCount < s_maxIORetries);

    return ret;
}

FileError findFile(const ResourcePath& filePath, const std::string_view fileName, string& foundPath)
{
    const std::filesystem::path file_name(fileName);

    const std::filesystem::recursive_directory_iterator end;
    const auto it = std::find_if(std::filesystem::recursive_directory_iterator( filePath.fileSystemPath()),
                                 end,
                                 [&file_name](const std::filesystem::directory_entry& e)
                                 {
                                    const bool ret = e.path().filename() == file_name;
                                    return ret;
                                 });
    if (it == end)
    {
        return FileError::FILE_NOT_FOUND;
    }

    foundPath = it->path().string();
    return FileError::NONE;
}

string getExtension(const std::string_view fileName)
{
    return getExtension(ResourcePath{ fileName });
}

string getExtension(const ResourcePath& fileName )
{
    return string { fileName.fileSystemPath().extension().string().c_str() };
}

ResourcePath getTopLevelFolderName(const ResourcePath& filePath)
{
    return ResourcePath { filePath.fileSystemPath().filename().string() };
}

string stripExtension( const std::string_view fileName ) noexcept
{
    return stripExtension(ResourcePath{fileName}).string();
}

ResourcePath stripExtension( const ResourcePath& filePath ) noexcept
{
    return ResourcePath{ filePath.fileSystemPath().stem().string() };
}

bool hasExtension(const ResourcePath& filePath, const std::string_view extensionNoDot)
{
    return hasExtension(filePath.string(), extensionNoDot);
}

bool hasExtension(const std::string_view filePath, const std::string_view extensionNoDot)
{
    const string targetExt = getExtension(filePath);
    if (extensionNoDot.empty())
    {
        return targetExt.empty();
    }

    if (targetExt.empty())
    {
        return false;
    }

    return Util::CompareIgnoreCase(targetExt.substr(1), extensionNoDot);
}

bool deleteAllFiles(const ResourcePath& filePath, const char* extension, const char* extensionToSkip)
{
    bool ret = false;

    if (pathExists(filePath))
    {
        for (const auto& p : std::filesystem::directory_iterator(filePath.fileSystemPath()))
        {
            try
            {
                if (is_regular_file(p.status()))
                {
                    const auto extensionString = p.path().extension().string().substr( 1 );
                    if (extensionToSkip && Util::CompareIgnoreCase( extensionString.c_str(), extensionToSkip) )
                    {
                        continue;
                    }

                    if (!extension || Util::CompareIgnoreCase( extensionString.c_str(), extension ))
                    {
                        if (std::filesystem::remove(p.path()))
                        {
                            ret = true;
                        }
                    }
                }
                else
                {
                    //ToDo: check if this recurse in subfolders actually works
                    if (!deleteAllFiles(ResourcePath{ p.path().string() }, extension , extensionToSkip))
                    {
                        NOP();
                    }
                }
            }
            catch ([[maybe_unused]] const std::exception &ex)
            {
                NOP();
            }
        }
    }

    return ret;
}

bool getAllFilesInDirectory( const ResourcePath& filePath, FileList& listInOut, const char* extensionNoDot )
{
    bool ret = false;
    if (pathExists(filePath))
    {
        for (const auto& p : std::filesystem::directory_iterator( filePath.fileSystemPath() ))
        {
            try
            {
                if (is_regular_file(p.status()))
                {
                    const auto extensionString = p.path().extension().string().substr( 1 );

                    if (!extensionNoDot || Util::CompareIgnoreCase(extensionString.c_str(), extensionNoDot))
                    {
                        const U64 timeOutSec = to_U64(std::chrono::duration_cast<std::chrono::seconds>(p.last_write_time().time_since_epoch()).count());

                        listInOut.emplace_back(FileEntry
                        {
                            ._name = ResourcePath{ p.path().filename().string() },
                            ._lastWriteTime = timeOutSec
                        });
                        ret = true;
                    }
                }
                else
                {
                    if (!getAllFilesInDirectory(ResourcePath{ p.path().string() }, listInOut, extensionNoDot))
                    {
                        NOP();
                    }
                }
            }
            catch ([[maybe_unused]] const std::exception& ex)
            {
                NOP();
            }
        }
    }

    return ret;
}

string extractFilePathAndName(char* argv0)
{
    auto currentPath = std::filesystem::current_path();
    currentPath.append(argv0);

    std::error_code ec;
    std::filesystem::path p(canonical(currentPath, ec));

    return p.make_preferred().string().c_str();
}

}; //namespace Divide
