#pragma once
// Copyright (c) 2026 DIVIDE-Studio

namespace Divide
{
    enum class FileError : U8;
    struct ResourcePath;

    namespace ImageTools::DDSCache
    {
        enum class WriteStatus : U8
        {
            CREATED,
            ALREADY_EXISTS,
            DIRECTORY_ERROR,
            CONVERSION_ERROR,
            MOVE_ERROR
        };

        struct WriteResult
        {
            WriteStatus _status;
            FileError _error;
        };

        // Serialises access to one cache file. Holders of guards for different paths never contend.
        class PathGuard
        {
        public:
            explicit PathGuard( const ResourcePath& cacheFilePath );
            ~PathGuard();

            PathGuard( const PathGuard& ) = delete;
            PathGuard& operator=( const PathGuard& ) = delete;

        private:
            U64 _pathHash{ 0u };
        };

        [[nodiscard]] WriteResult Write( const ResourcePath& cachePath, std::string_view cacheFileName,
                                         const std::function<bool( const ResourcePath& )>& generate );
    }
}
