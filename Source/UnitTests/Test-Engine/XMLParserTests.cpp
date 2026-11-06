// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Configuration.h"
#include "Utility/Headers/XMLParser.h"

namespace Divide
{
    TEST_CASE( "XML configuration loading reports errors and restores backups", "[configuration][xml]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-xml-loading-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        constexpr const char* fileName = "config{failure}.xml";
        const std::filesystem::path file = directory / fileName;
        const std::filesystem::path backup = directory / "config{failure}.xml.bak";
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove( file, error );
            std::filesystem::remove( backup, error );
            std::filesystem::remove( directory, error );
        };

        const ResourcePath path{ directory.string() };
        Configuration config{};
        config.runtime.assetLoaderThreads = 3;

        SECTION( "a directory without a backup fails" )
        {
            REQUIRE( std::filesystem::create_directory( file ) );
            CHECK_FALSE( XML::loadFromXML( config, path, fileName ) );
            CHECK( std::filesystem::is_directory( file ) );
            CHECK( config.runtime.assetLoaderThreads == 3 );
        }

        SECTION( "a directory remains intact when its backup cannot be restored" )
        {
            REQUIRE( std::filesystem::create_directory( file ) );
            {
                std::ofstream output{ backup };
                output << "<config><runtime><assetLoaderThreads>7</assetLoaderThreads></runtime></config>";
                REQUIRE( output.good() );
            }
            CHECK_FALSE( XML::loadFromXML( config, path, fileName ) );
            CHECK( std::filesystem::is_directory( file ) );
            CHECK( std::filesystem::is_regular_file( backup ) );
            CHECK( config.runtime.assetLoaderThreads == 3 );
        }

        SECTION( "a directory cannot be overwritten by an XML save" )
        {
            REQUIRE( std::filesystem::create_directory( file ) );
            CHECK_FALSE( XML::saveToXML( config, path, fileName ) );
            CHECK( std::filesystem::is_directory( file ) );
        }

        SECTION( "a direct XML tree read preserves the tree on an I/O failure" )
        {
            REQUIRE( std::filesystem::create_directory( file ) );
            boost::property_tree::ptree tree;
            tree.put( "existing.value", 11 );
            CHECK_NOTHROW( XML::readXML( ResourcePath{ file.string() }, tree ) );
            CHECK( tree.get<int>( "existing.value" ) == 11 );
        }

        SECTION( "a missing file is restored from its backup" )
        {
            {
                std::ofstream output{ backup };
                output << "<config><runtime><assetLoaderThreads>7</assetLoaderThreads></runtime></config>";
                REQUIRE( output.good() );
            }
            REQUIRE( XML::loadFromXML( config, path, fileName ) );
            CHECK( std::filesystem::is_regular_file( file ) );
            CHECK( config.runtime.assetLoaderThreads == 7 );
        }

        SECTION( "malformed XML fails without changing the configuration" )
        {
            {
                std::ofstream output{ file };
                output << "<config><runtime>";
                REQUIRE( output.good() );
            }
            CHECK_FALSE( XML::loadFromXML( config, path, fileName ) );
            CHECK( config.runtime.assetLoaderThreads == 3 );
        }
    }
}
