#include "UnitTests/unitTestCommon.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Core/Headers/StringHelper.h"

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
