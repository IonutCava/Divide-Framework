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

    TEST_CASE( "XML save snapshots write their files later and honour backup policies", "[xml][scene_save]" )
    {
        platformInitRunListener::PlatformInit();
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-xml-snapshot-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };

        const ResourcePath folder{ directory.string() };
        const auto writeText = [&]( const char* name, const char* text )
        {
            std::ofstream output{ directory / name };
            output << text;
            REQUIRE( output.good() );
        };
        const auto readValue = [&]( const char* name )
        {
            boost::property_tree::ptree tree;
            XML::readXML( folder / name, tree );
            return tree.get<int>( "value", -1 );
        };

        writeText( "existing.xml", "<value>1</value>" );

        XML::SaveSnapshot snapshot{};
        snapshot._directories.emplace_back( folder / "nested" );
        {
            XML::FileSnapshot& file = snapshot._files.emplace_back();
            file._folder = folder;
            file._fileName = "existing.xml";
            file._backupSource = file._fileName;
            file._backup = XML::FileSnapshot::Backup::IF_PRESENT;
            file._progressMessage = "existing";
            file._tree.put( "value", 2 );
        }
        {
            XML::FileSnapshot& file = snapshot._files.emplace_back();
            file._folder = folder;
            file._fileName = "new.xml";
            file._backupSource = file._fileName;
            file._backup = XML::FileSnapshot::Backup::IF_PRESENT;
            file._tree.put( "value", 3 );
        }
        {
            XML::FileSnapshot& file = snapshot._files.emplace_back();
            file._folder = folder / "nested";
            file._fileName = "node.xml";
            file._progressMessage = "node";
            file._tree.put( "value", 4 );
        }
        {
            XML::FileSnapshot& file = snapshot._files.emplace_back();
            file._folder = folder;
            file._fileName = "needsBackup.xml";
            file._backupSource = "missingSource.xml";
            file._backup = XML::FileSnapshot::Backup::REQUIRED_TO_WRITE;
            file._tree.put( "value", 5 );
        }

        CHECK( readValue( "existing.xml" ) == 1 );
        CHECK_FALSE( std::filesystem::exists( directory / "nested" ) );

        vector<string> messages;
        CHECK( XML::writeSnapshot( snapshot, [&messages]( const std::string_view message )
        {
            messages.emplace_back( message.data(), message.size() );
        } ) );

        CHECK( messages == vector<string>{ "existing", "node" } );
        CHECK( readValue( "existing.xml" ) == 2 );
        CHECK( readValue( "existing.xml.bak" ) == 1 );
        CHECK( readValue( "new.xml" ) == 3 );
        CHECK_FALSE( std::filesystem::exists( directory / "new.xml.bak" ) );
        CHECK( readValue( "nested/node.xml" ) == 4 );
        CHECK_FALSE( std::filesystem::exists( directory / "needsBackup.xml" ) );

        writeText( "missingSource.xml", "<value>6</value>" );
        CHECK( XML::writeSnapshot( snapshot, {} ) );
        CHECK( readValue( "needsBackup.xml" ) == 5 );
        CHECK( readValue( "missingSource.xml.bak" ) == 6 );
    }
}
