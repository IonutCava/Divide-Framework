// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Platform/File/Headers/FileManagement.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <iostream>

namespace Divide
{

TEST_CASE( "File Existence Check" "[file_management]" )
{
    platformInitRunListener::PlatformInit();

    const ResourcePath invalidFileName{ "abc.cba" };

    const Divide::SysInfo& systemInfo = Divide::const_sysInfo();
    const ResourcePath workingPath = systemInfo._workingDirectory;

    CHECK_TRUE(Divide::pathExists(workingPath));
    CHECK_FALSE(Divide::fileExists(invalidFileName));
}

TEST_CASE( "Path Existence Check" "[file_management]" )
{
    platformInitRunListener::PlatformInit();

    const ResourcePath invalidPath{"abccba"};

    CHECK_TRUE(Divide::pathExists(Divide::Paths::g_assetsLocation));
    CHECK_FALSE(Divide::pathExists(invalidPath));
}

TEST_CASE( "Creating files preserves existing contents unless overwrite is requested", "[file_management]" )
{
    platformInitRunListener::PlatformInit();
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            ( "divide-create-file-" + std::to_string( suffix ) );
    REQUIRE( std::filesystem::create_directory( directory ) );
    SCOPE_EXIT
    {
        std::error_code error;
        std::filesystem::remove( directory / "nested" / "file.txt", error );
        std::filesystem::remove( directory / "nested", error );
        std::filesystem::remove( directory / "blocker", error );
        std::filesystem::remove( directory, error );
    };

    const std::filesystem::path file = directory / "nested" / "file.txt";
    const ResourcePath filePath{ file.string() };
    REQUIRE( createFile( filePath, false ) );
    REQUIRE( std::filesystem::is_regular_file( file ) );
    {
        std::ofstream output{ file };
        output << "preserve";
        REQUIRE( output.good() );
    }
    REQUIRE( createFile( filePath, false ) );
    {
        std::ifstream input{ file };
        std::string contents;
        input >> contents;
        CHECK( contents == "preserve" );
    }
    REQUIRE( createFile( filePath, true ) );
    CHECK( std::filesystem::file_size( file ) == 0u );

    const std::filesystem::path blocker = directory / "blocker";
    {
        std::ofstream output{ blocker };
        REQUIRE( output.good() );
    }
    CHECK_FALSE( createFile( ResourcePath{ ( blocker / "file.txt" ).string() }, false ) );
}

TEST_CASE( "Extension Check" "[file_management]" )
{
    const ResourcePath file1{"temp.xyz"};
    const ResourcePath file2{"folder/temp.st"};
    const ResourcePath file3{"folder/temp"};
    const char* ext1 = "xyz";
    const char* ext2 = "st";

    CHECK_TRUE(Divide::hasExtension(file1, ext1));
    CHECK_TRUE(Divide::hasExtension(file2, ext2));
    CHECK_FALSE(Divide::hasExtension(file1, ext2));
    CHECK_FALSE(Divide::hasExtension(file3, ext2));
}

TEST_CASE( "Lexically Normal Path Compare" "[file_management]" )
{
    const ResourcePath path1_in{"foo/./bar/.."};
    const ResourcePath path2_in{"foo/.///bar/../"};

    const ResourcePath path_out{"foo/"};

    CHECK_EQUAL(path1_in, path_out);
    CHECK_EQUAL(path2_in, path_out);
}

} //namespace Divide
