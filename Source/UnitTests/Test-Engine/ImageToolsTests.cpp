// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/TaskPool.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Utility/Headers/DDSCache.h"
#include "Utility/Headers/ImageTools.h"

#if !defined(IS_MACOS_BUILD)
#include <nvtt/nvtt.h>
#endif

namespace Divide
{
#if !defined(IS_MACOS_BUILD)
    namespace
    {
        [[nodiscard]] std::vector<U8> MakeBitmap( const U16 bitsPerPixel, const bool topDown, const U32 padding )
        {
            constexpr U32 width = 8u;
            constexpr U32 height = 8u;
            const bool alpha = bitsPerPixel == 32u;
            const U32 headerSize = alpha ? 108u : 40u;
            const U32 maskSize = bitsPerPixel == 16u ? 12u : 0u;
            const U32 bytesPerPixel = bitsPerPixel / 8u;
            const U32 offset = 14u + headerSize + maskSize + padding;
            std::vector<U8> bytes( offset + width * height * bytesPerPixel, 0u );
            const auto put16 = [&]( const size_t index, const U16 value )
            {
                bytes[index] = to_U8( value & 0xFFu );
                bytes[index + 1u] = to_U8( value >> 8u );
            };
            const auto put32 = [&]( const size_t index, const U32 value )
            {
                for ( U32 i = 0u; i < 4u; ++i )
                {
                    bytes[index + i] = to_U8( ( value >> ( i * 8u ) ) & 0xFFu );
                }
            };

            bytes[0] = 'B';
            bytes[1] = 'M';
            put32( 2u, to_U32( bytes.size() ) );
            put32( 10u, offset );
            put32( 14u, headerSize );
            put32( 18u, width );
            put32( 22u, topDown ? 0xFFFFFFF8u : height );
            put16( 26u, 1u );
            put16( 28u, bitsPerPixel );
            put32( 30u, bitsPerPixel == 24u ? 0u : 3u );
            put32( 34u, width * height * bytesPerPixel );
            if ( bitsPerPixel == 16u )
            {
                put32( 54u, 0x0000F800u );
                put32( 58u, 0x000007E0u );
                put32( 62u, 0x0000001Fu );
            }
            else if ( alpha )
            {
                put32( 54u, 0x00FF0000u );
                put32( 58u, 0x0000FF00u );
                put32( 62u, 0x000000FFu );
                put32( 66u, 0xFF000000u );
            }

            for ( U32 y = 0u; y < height; ++y )
            {
                const bool red = ( topDown ? y : height - 1u - y ) < height / 2u;
                for ( U32 x = 0u; x < width; ++x )
                {
                    const size_t index = offset + ( y * width + x ) * bytesPerPixel;
                    if ( bitsPerPixel == 16u )
                    {
                        put16( index, red ? 0xF800u : 0x001Fu );
                    }
                    else
                    {
                        bytes[index] = red ? 0u : 255u;
                        bytes[index + 2u] = red ? 255u : 0u;
                        if ( alpha )
                        {
                            bytes[index + 3u] = red ? 128u : 64u;
                        }
                    }
                }
            }
            return bytes;
        }

        [[nodiscard]] bool WriteBitmap( const std::filesystem::path& path, const std::vector<U8>& bytes )
        {
            std::ofstream output{ path, std::ios::binary };
            output.write( reinterpret_cast<const char*>( bytes.data() ), to_I64( bytes.size() ) );
            output.close();
            return output.good();
        }

        void CheckBitmapPixels( const nvtt::Surface& image, const bool alpha, const F32 tolerance )
        {
            REQUIRE( image.width() == 8 );
            REQUIRE( image.height() == 8 );
            const size_t top = 0u;
            const size_t bottom = 7u * 8u;
            CHECK( image.channel( 0 )[top] == Catch::Approx( 1.f ).margin( tolerance ) );
            CHECK( image.channel( 1 )[top] == Catch::Approx( 0.f ).margin( tolerance ) );
            CHECK( image.channel( 2 )[top] == Catch::Approx( 0.f ).margin( tolerance ) );
            CHECK( image.channel( 0 )[bottom] == Catch::Approx( 0.f ).margin( tolerance ) );
            CHECK( image.channel( 1 )[bottom] == Catch::Approx( 0.f ).margin( tolerance ) );
            CHECK( image.channel( 2 )[bottom] == Catch::Approx( 1.f ).margin( tolerance ) );
            if ( alpha )
            {
                CHECK( image.channel( 3 )[top] == Catch::Approx( 128.f / 255.f ).margin( tolerance ) );
                CHECK( image.channel( 3 )[bottom] == Catch::Approx( 64.f / 255.f ).margin( tolerance ) );
            }
        }
    }

    TEST_CASE( "DDS conversion preserves BMP colour, alpha, and orientation", "[image_data][render_smoke][dds_cache]" )
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-bmp-cache-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };
        const std::filesystem::path sourcePath = directory / "source.bmp";
        const std::filesystem::path ddsPath = directory / "output.dds";

        SECTION( "16-bit BMP with bitfield masks and padding" )
        {
            REQUIRE( WriteBitmap( sourcePath, MakeBitmap( 16u, false, 8u ) ) );
            nvtt::Surface image;
            bool hasAlpha = true;
            REQUIRE( image.load( sourcePath.string().c_str(), &hasAlpha ) );
            CHECK_FALSE( hasAlpha );
            CheckBitmapPixels( image, false, 0.001f );
        }

        SECTION( "24-bit bottom-up BMP" )
        {
            REQUIRE( WriteBitmap( sourcePath, MakeBitmap( 24u, false, 0u ) ) );
            nvtt::Surface image;
            bool hasAlpha = true;
            REQUIRE( image.load( sourcePath.string().c_str(), &hasAlpha ) );
            CHECK_FALSE( hasAlpha );
            CheckBitmapPixels( image, false, 0.001f );
        }

        SECTION( "32-bit padded BMP is compressed to DDS" )
        {
            REQUIRE( WriteBitmap( sourcePath, MakeBitmap( 32u, false, 16u ) ) );
            nvtt::Surface image;
            bool hasAlpha = false;
            REQUIRE( image.load( sourcePath.string().c_str(), &hasAlpha ) );
            CHECK( hasAlpha );
            CheckBitmapPixels( image, true, 0.001f );

            {
                nvtt::Context context;
                context.enableCudaAcceleration( false );
                nvtt::CompressionOptions compression;
                compression.setFormat( nvtt::Format_BC3 );
                compression.setQuality( nvtt::Quality_Fastest );
                nvtt::OutputOptions output;
                output.setFileName( ddsPath.string().c_str() );
                output.setContainer( nvtt::Container_DDS );
                REQUIRE( context.outputHeader( image, 1, compression, output ) );
                REQUIRE( context.compress( image, 0, 0, compression, output ) );
            }

            nvtt::Surface compressed;
            REQUIRE( compressed.load( ddsPath.string().c_str() ) );
            CheckBitmapPixels( compressed, true, 0.02f );
        }

        SECTION( "32-bit top-down BMP" )
        {
            REQUIRE( WriteBitmap( sourcePath, MakeBitmap( 32u, true, 0u ) ) );
            nvtt::Surface image;
            bool hasAlpha = false;
            REQUIRE( image.load( sourcePath.string().c_str(), &hasAlpha ) );
            CHECK( hasAlpha );
            CheckBitmapPixels( image, true, 0.001f );
        }

        SECTION( "Pixel offsets outside the input are rejected" )
        {
            auto bitmap = MakeBitmap( 32u, false, 0u );
            for ( const U32 offset : { 40u, to_U32( bitmap.size() ), to_U32( bitmap.size() + 1u ) } )
            {
                for ( U32 i = 0u; i < 4u; ++i )
                {
                    bitmap[10u + i] = to_U8( ( offset >> ( i * 8u ) ) & 0xFFu );
                }
                REQUIRE( WriteBitmap( sourcePath, bitmap ) );
                nvtt::Surface image;
                CHECK_FALSE( image.load( sourcePath.string().c_str() ) );
            }
        }

        SECTION( "PNG loading remains available" )
        {
            constexpr std::array<Byte, 3> red{{ Byte{ 255u }, Byte{ 0u }, Byte{ 0u } }};
            const ResourcePath pngPath{ ( directory / "source.png" ).string() };
            REQUIRE( ImageTools::SaveImage( pngPath, 1u, 1u, 3u, 3u, false, red.data(),
                                            ImageTools::SaveImageFormat::PNG ) );
            nvtt::Surface image;
            REQUIRE( image.load( pngPath.string().c_str() ) );
            CHECK( image.channel( 0 )[0] == Catch::Approx( 1.f ).margin( 0.001f ) );
            CHECK( image.channel( 1 )[0] == Catch::Approx( 0.f ).margin( 0.001f ) );
            CHECK( image.channel( 2 )[0] == Catch::Approx( 0.f ).margin( 0.001f ) );
        }
    }

    TEST_CASE( "DDS texture layers load completed conversions consistently", "[image_data][render_smoke][dds_cache]" )
    {
        platformInitRunListener::PlatformInit();
        platformInitRunListener::LocalizationScope localization;
        ImageTools::OnStartup( false );
        SCOPE_EXIT
        {
            ImageTools::OnShutdown();
        };

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-dds-layers-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };
        REQUIRE( WriteBitmap( directory / "first.bmp", MakeBitmap( 32u, false, 16u ) ) );
        REQUIRE( WriteBitmap( directory / "second.bmp", MakeBitmap( 32u, false, 16u ) ) );
        REQUIRE( Texture::UseTextureDDSCache() );

        Application app;
        PlatformContext context{ app };
        TaskPool& pool = context.taskPool( TaskPoolType::HIGH_PRIORITY );
        REQUIRE( pool.init( 1u ) );
        SCOPE_EXIT
        {
            pool.shutdown();
        };

        const ResourcePath path{ directory.string() };
        const auto load = [&]( ImageTools::ImageData& data, ImageTools::ImportOptions& options, const char* name )
        {
            return data.loadFromFile( context, false, 0u, 0u, path, name, options );
        };
        ImageTools::ImportOptions options{};
        options._fastCompression = true;

        SECTION( "A requested synchronous conversion loads the newly created DDS" )
        {
            options._waitForDDSConversion = true;
            ImageTools::ImageData data;
            REQUIRE( load( data, options, "first.bmp" ) );
            CHECK( std::filesystem::is_regular_file( directory / "first.bmp.dds" ) );
            CHECK( data.format() == GFXImageFormat::DXT5_RGBA );
            CHECK( data.mipCount() == 4u );
            REQUIRE( load( data, options, "second.bmp" ) );
            CHECK( data.layerCount() == 2u );
            CHECK( ImageTools::HaveCompatibleMipChains( data.imageLayers() ) );
        }

        SECTION( "A warm first layer waits for a missing second cache entry" )
        {
            options._waitForDDSConversion = true;
            ImageTools::ImageData warmup;
            REQUIRE( load( warmup, options, "first.bmp" ) );
            options._waitForDDSConversion = false;
            ImageTools::ImageData data;
            REQUIRE( load( data, options, "first.bmp" ) );
            REQUIRE( data.mipCount() == 4u );
            REQUIRE( load( data, options, "second.bmp" ) );
            CHECK( std::filesystem::is_regular_file( directory / "second.bmp.dds" ) );
            CHECK( data.format() == GFXImageFormat::DXT5_RGBA );
            CHECK( data.layerCount() == 2u );
            CHECK( ImageTools::HaveCompatibleMipChains( data.imageLayers() ) );
        }

        SECTION( "A cold asynchronous load keeps every layer in the source format" )
        {
            ImageTools::ImageData data;
            REQUIRE( load( data, options, "first.bmp" ) );
            REQUIRE( load( data, options, "second.bmp" ) );
            CHECK( data.format() == GFXImageFormat::RGBA );
            CHECK( data.mipCount() == 1u );
            CHECK( data.layerCount() == 2u );
            CHECK( ImageTools::HaveCompatibleMipChains( data.imageLayers() ) );
            for ( U32 i = 0u; i < 500u &&
                  !(std::filesystem::is_regular_file( directory / "first.bmp.dds" ) &&
                    std::filesystem::is_regular_file( directory / "second.bmp.dds" )); ++i )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds{ 10 } );
            }
            REQUIRE( std::filesystem::is_regular_file( directory / "first.bmp.dds" ) );
            REQUIRE( std::filesystem::is_regular_file( directory / "second.bmp.dds" ) );
        }

        SECTION( "A failed later DDS conversion cannot fall back to source data" )
        {
            options._waitForDDSConversion = true;
            ImageTools::ImageData data;
            REQUIRE( load( data, options, "first.bmp" ) );
            REQUIRE( std::filesystem::create_directory( directory / "second.bmp.dds" ) );
            CHECK_FALSE( load( data, options, "second.bmp" ) );
            CHECK( data.layerCount() == 1u );
            CHECK( data.mipCount() == 4u );
        }

        SECTION( "Skipped mipmaps produce a single-level DDS" )
        {
            options._waitForDDSConversion = true;
            options._skipMipMaps = true;
            ImageTools::ImageData data;
            REQUIRE( load( data, options, "first.bmp" ) );
            CHECK( data.format() == GFXImageFormat::DXT5_RGBA );
            CHECK( data.mipCount() == 1u );
            CHECK( ImageTools::HaveCompatibleMipChains( data.imageLayers() ) );
        }
    }
#endif

    TEST_CASE( "Texture alpha inspection combines layers and validates cached source data", "[image_data][texture][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        const platformInitRunListener::LocalizationScope localization{};
        ImageTools::OnStartup( false );
        SCOPE_EXIT
        {
            ImageTools::OnShutdown();
        };

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-alpha-layers-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };
        const ResourcePath path{ directory.string() };

        constexpr U16 width = 80u;
        constexpr U16 height = 2u;
        constexpr size_t pixelCount = width * height;
        using Alpha = std::array<U8, pixelCount>;
        const auto writeTga = [&]( const char* name, const Alpha& alpha, const bool addTrailingByte = false )
        {
            std::array<U8, 18> header{};
            header[2] = 2u;
            header[12] = to_U8( width & 0xFFu );
            header[13] = to_U8( width >> 8u );
            header[14] = to_U8( height & 0xFFu );
            header[15] = to_U8( height >> 8u );
            header[16] = 32u;
            header[17] = 0x28u;
            std::ofstream output{ directory / name, std::ios::binary };
            output.write( reinterpret_cast<const char*>( header.data() ), to_I64( header.size() ) );
            for ( const U8 value : alpha )
            {
                const std::array<U8, 4> pixel{{ 40u, 80u, 120u, value }};
                output.write( reinterpret_cast<const char*>( pixel.data() ), to_I64( pixel.size() ) );
            }
            if ( addTrailingByte )
            {
                output.put( '\0' );
            }
            output.close();
            return output.good();
        };

        Alpha opaque{};
        opaque.fill( 255u );
        Alpha early = opaque;
        for ( const size_t x : { 1u, 17u, 33u, 49u } )
        {
            early[x] = 0u;
        }
        Alpha late = opaque;
        late[65u] = 128u;
        Alpha invisible{};
        REQUIRE( writeTga( "early.tga", early ) );
        REQUIRE( writeTga( "late.tga", late ) );
        REQUIRE( writeTga( "invisible.tga", invisible ) );
        REQUIRE( writeTga( "opaque.tga", opaque ) );

        Application app;
        PlatformContext context{ app };
        TaskPool& pool = context.taskPool( TaskPoolType::HIGH_PRIORITY );
        REQUIRE( pool.init( 2u ) );
        SCOPE_EXIT
        {
            pool.shutdown();
        };
        const auto load = [&]( ImageTools::ImageData& data, const char* name )
        {
            return data.loadFromFile( context, false, 0u, 0u, path, name );
        };
        const auto check = [&]( const ImageTools::ImageData& data, const char* name, const U16 firstLayer,
                                TextureTransparencyInfo& info )
        {
            return CheckTextureTransparency( context, true, path, name, data, firstLayer, info );
        };
        const auto findCache = [&]( const std::string_view name )
        {
            const std::string prefix = std::string{ name } + ".alpha-2-";
            for ( const auto& entry : std::filesystem::directory_iterator{ directory } )
            {
                if ( entry.path().filename().string().starts_with( prefix ) )
                {
                    return entry.path();
                }
            }
            return std::filesystem::path{};
        };

        SECTION( "Non-opaque pixels accumulate across partitions and source layers" )
        {
            ImageTools::ImageData data;
            TextureTransparencyInfo info{};
            REQUIRE( load( data, "early.tga" ) );
            REQUIRE( data.format() == GFXImageFormat::RGBA );
            REQUIRE( check( data, "early.tga", 0u, info ) );
            CHECK( info._nonOpaquePixels == 4u );
            CHECK_FALSE( info.hasTransparency() );
            CHECK_FALSE( info.hasTranslucency() );

            const U16 firstLayer = data.layerCount();
            REQUIRE( load( data, "late.tga" ) );
            REQUIRE( data.layerCount() == 2u );
            TextureTransparencyInfo lateOnly{};
            REQUIRE( check( data, "late.tga", firstLayer, lateOnly ) );
            CHECK( lateOnly._nonOpaquePixels == 1u );
            CHECK_FALSE( lateOnly.hasTransparency() );
            REQUIRE( check( data, "late.tga", firstLayer, info ) );
            CHECK( info._nonOpaquePixels == TextureTransparencyInfo::PIXEL_THRESHOLD );
            CHECK( info.hasTransparency() );
            CHECK( info.hasTranslucency() );
            CHECK_FALSE( findCache( "early.tga" ).empty() );
            CHECK_FALSE( findCache( "late.tga" ).empty() );
        }

        SECTION( "Invisible alpha is meaningful only when another layer is visible" )
        {
            ImageTools::ImageData data;
            TextureTransparencyInfo info{};
            REQUIRE( load( data, "invisible.tga" ) );
            REQUIRE( check( data, "invisible.tga", 0u, info ) );
            CHECK_FALSE( info.hasTransparency() );
            CHECK_FALSE( info.hasTranslucency() );

            const U16 firstLayer = data.layerCount();
            REQUIRE( load( data, "opaque.tga" ) );
            REQUIRE( check( data, "opaque.tga", firstLayer, info ) );
            CHECK( info.hasTransparency() );
            CHECK_FALSE( info.hasTranslucency() );
        }

        SECTION( "Cache hits, corrupt entries and changed sources retain correct classifications" )
        {
            ImageTools::ImageData cold;
            TextureTransparencyInfo coldInfo{};
            REQUIRE( load( cold, "late.tga" ) );
            REQUIRE( check( cold, "late.tga", 0u, coldInfo ) );
            REQUIRE( coldInfo._nonOpaquePixels == 1u );
            const std::filesystem::path cache = findCache( "late.tga" );
            REQUIRE_FALSE( cache.empty() );

            ImageTools::ImageData warm;
            REQUIRE( load( warm, "late.tga" ) );
            auto* decoded = static_cast<U8*>( warm.data( 0u, 0u ) );
            REQUIRE( decoded != nullptr );
            for ( size_t pixel = 0u; pixel < pixelCount; ++pixel )
            {
                decoded[pixel * 4u + 3u] = 255u;
            }
            TextureTransparencyInfo warmInfo{};
            REQUIRE( check( warm, "late.tga", 0u, warmInfo ) );
            CHECK( warmInfo._nonOpaquePixels == 1u );
            CHECK( warmInfo._hasTranslucentPixel );

            {
                std::ofstream corrupt{ cache, std::ios::binary | std::ios::trunc };
                corrupt.put( '\0' );
                REQUIRE( corrupt.good() );
            }
            ImageTools::ImageData repaired;
            TextureTransparencyInfo repairedInfo{};
            REQUIRE( load( repaired, "late.tga" ) );
            REQUIRE( check( repaired, "late.tga", 0u, repairedInfo ) );
            CHECK( repairedInfo._nonOpaquePixels == 1u );
            CHECK( repairedInfo._hasTranslucentPixel );
            CHECK( std::filesystem::file_size( cache ) == sizeof( U16 ) + sizeof( U64 ) + sizeof( U8 ) * 4u );

            const auto originalSize = std::filesystem::file_size( directory / "late.tga" );
            REQUIRE( writeTga( "late.tga", opaque, true ) );
            REQUIRE( std::filesystem::file_size( directory / "late.tga" ) > originalSize );
            ImageTools::ImageData changed;
            TextureTransparencyInfo changedInfo{};
            REQUIRE( load( changed, "late.tga" ) );
            REQUIRE( check( changed, "late.tga", 0u, changedInfo ) );
            CHECK( changedInfo._nonOpaquePixels == 0u );
            CHECK_FALSE( changedInfo.hasTransparency() );
            CHECK_FALSE( changedInfo.hasTranslucency() );
        }

        SECTION( "Alpha inspection can be disabled for data textures" )
        {
            ImageTools::ImageData empty;
            TextureTransparencyInfo info{};
            CHECK( CheckTextureTransparency( context, false, path, "unused", empty, 0u, info ) );
            CHECK_FALSE( info.hasTransparency() );
        }
    }

    TEST_CASE( "Texture alpha summaries preserve threshold and visibility boundaries", "[image_data][texture]" )
    {
        TextureTransparencyInfo summary{};
        for ( U8 i = 0u; i < 4u; ++i )
        {
            summary.add( 0u );
        }
        summary.add( 251u );
        CHECK( summary._nonOpaquePixels == 4u );
        CHECK_FALSE( summary.hasTransparency() );
        summary.add( 250u );
        CHECK( summary.hasTransparency() );
        CHECK( summary.hasTranslucency() );

        TextureTransparencyInfo invisible{};
        for ( U8 i = 0u; i < 5u; ++i )
        {
            invisible.add( 1u );
        }
        CHECK_FALSE( invisible.hasTransparency() );
        TextureTransparencyInfo visible{};
        visible.add( 2u );
        invisible.merge( visible );
        CHECK( invisible._nonOpaquePixels == TextureTransparencyInfo::PIXEL_THRESHOLD );
        CHECK( invisible.hasTransparency() );
        CHECK( invisible.hasTranslucency() );
    }

    TEST_CASE( "Image layers require compatible, complete mip chains", "[image_data][render_smoke]" )
    {
        vector<ImageTools::ImageLayer> layers( 2u );
        const auto addMip = []( ImageTools::ImageLayer& layer, const size_t size, const U16 width,
                                const U16 height, const U16 depth )
        {
            REQUIRE( layer.allocateMip<Byte>( size, width, height, depth, 0u ) != nullptr );
        };

        SECTION( "Matching 3D mip chains are accepted" )
        {
            for ( auto& layer : layers )
            {
                addMip( layer, 64u, 8u, 4u, 2u );
                addMip( layer, 8u, 4u, 2u, 1u );
                addMip( layer, 2u, 2u, 1u, 1u );
                addMip( layer, 1u, 1u, 1u, 1u );
            }
            CHECK( ImageTools::HaveCompatibleMipChains( layers ) );
        }

        SECTION( "Missing layers or mip levels are rejected" )
        {
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( {} ) );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
            addMip( layers[0], 16u, 4u, 4u, 1u );
            addMip( layers[0], 4u, 2u, 2u, 1u );
            addMip( layers[1], 16u, 4u, 4u, 1u );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
        }

        SECTION( "Different payload sizes are rejected" )
        {
            addMip( layers[0], 16u, 4u, 4u, 1u );
            addMip( layers[1], 8u, 4u, 4u, 1u );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
        }

        SECTION( "Different layer dimensions are rejected" )
        {
            addMip( layers[0], 16u, 4u, 4u, 1u );
            addMip( layers[1], 16u, 8u, 2u, 1u );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
        }

        SECTION( "A mip must halve the previous dimensions" )
        {
            for ( auto& layer : layers )
            {
                addMip( layer, 16u, 4u, 4u, 1u );
                addMip( layer, 8u, 4u, 2u, 1u );
            }
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
        }

        SECTION( "Zero extents and extra levels after 1x1x1 are rejected" )
        {
            addMip( layers[0], 1u, 0u, 1u, 1u );
            addMip( layers[1], 1u, 0u, 1u, 1u );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( layers ) );
            vector<ImageTools::ImageLayer> tooMany( 1u );
            addMip( tooMany[0], 1u, 1u, 1u, 1u );
            addMip( tooMany[0], 1u, 1u, 1u, 1u );
            CHECK_FALSE( ImageTools::HaveCompatibleMipChains( tooMany ) );
        }
    }

    TEST_CASE( "Image mip storage sizes and copies are measured in bytes", "[image_data][render_smoke]" )
    {
        SECTION( "16-bit pixels with an explicit element count" )
        {
            constexpr std::array<U16, 4> pixels{ 0x1234u, 0x2345u, 0x3456u, 0x4567u };
            ImageTools::ImageLayer layer;
            const U16* const data = layer.allocateMip( pixels.data(), pixels.size(), 2u, 2u, 1u, 1u );
            REQUIRE( data != nullptr );
            CHECK( layer.getMip( 0u )->_size == pixels.size() * sizeof( U16 ) );
            CHECK( std::equal( pixels.begin(), pixels.end(), data ) );
        }

        SECTION( "An implicit element count copies the entire image" )
        {
            constexpr std::array<U16, 4> pixels{ 0x1234u, 0x2345u, 0x3456u, 0x4567u };
            ImageTools::ImageLayer layer;
            const U16* const data = layer.allocateMip( pixels.data(), 0u, 2u, 2u, 1u, 1u );
            REQUIRE( data != nullptr );
            CHECK( layer.getMip( 0u )->_size == pixels.size() * sizeof( U16 ) );
            CHECK( std::equal( pixels.begin(), pixels.end(), data ) );
        }

        SECTION( "32-bit data retains zero-initialized padding" )
        {
            constexpr std::array<F32, 2> pixels{ 1.0f, 2.0f };
            ImageTools::ImageLayer layer;
            const F32* const data = layer.allocateMip( pixels.data(), pixels.size(), 2u, 2u, 1u, 1u );
            REQUIRE( data != nullptr );
            CHECK( layer.getMip( 0u )->_size == 4u * sizeof( F32 ) );
            CHECK( data[0] == pixels[0] );
            CHECK( data[1] == pixels[1] );
            CHECK( data[2] == 0.0f );
            CHECK( data[3] == 0.0f );
        }

        SECTION( "Compressed byte payloads keep their exact size" )
        {
            constexpr std::array<Byte, 8> bytes{ Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 4 },
                                                 Byte{ 5 }, Byte{ 6 }, Byte{ 7 }, Byte{ 8 } };
            ImageTools::ImageLayer layer;
            const Byte* const data = layer.allocateMip( bytes.data(), bytes.size(), 8u, 8u, 1u, 0u );
            REQUIRE( data != nullptr );
            CHECK( layer.getMip( 0u )->_size == bytes.size() );
            CHECK( std::equal( bytes.begin(), bytes.end(), data ) );
        }
    }

    TEST_CASE( "DDS cache writes handle nested paths, competing writers, and failures", "[image_data][render_smoke][dds_cache]" )
    {
        using namespace ImageTools::DDSCache;

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                                ( "divide-dds-cache-" + std::to_string( suffix ) );
        REQUIRE( std::filesystem::create_directory( directory ) );
        SCOPE_EXIT
        {
            std::error_code error;
            std::filesystem::remove_all( directory, error );
        };

        const ResourcePath cachePath{ directory.string() };
        const string cacheName = "nested/image.jpg.dds";
        const std::filesystem::path target = directory / "nested" / "image.jpg.dds";
        const std::filesystem::path temp = directory / "nested" / "image.jpg.dds.tmp";
        std::atomic_uint conversionCount{ 0u };
        const auto writeData = [&]( const ResourcePath& path )
        {
            ++conversionCount;
            std::ofstream stream{ path.fileSystemPath(), std::ios::binary };
            stream << "cache-data";
            stream.close();
            return stream.good();
        };

        SECTION( "Nested entries are published and existing entries are not regenerated" )
        {
            const WriteResult first = Write( cachePath, cacheName, writeData );
            CHECK( first._status == WriteStatus::CREATED );
            CHECK( first._error == FileError::NONE );
            CHECK( std::filesystem::is_regular_file( target ) );
            CHECK_FALSE( std::filesystem::exists( temp ) );
            std::ifstream input{ target, std::ios::binary };
            std::string contents;
            input >> contents;
            CHECK( contents == "cache-data" );
            input.close();

            const WriteResult second = Write( cachePath, cacheName, writeData );
            CHECK( second._status == WriteStatus::ALREADY_EXISTS );
            CHECK( conversionCount.load() == 1u );
        }

        SECTION( "Competing writers publish only one completed cache file" )
        {
            std::atomic_uint ready{ 0u };
            std::atomic_bool start{ false };
            std::array<WriteResult, 2> results
            {
                WriteResult{ WriteStatus::CONVERSION_ERROR, FileError::NONE },
                WriteResult{ WriteStatus::CONVERSION_ERROR, FileError::NONE }
            };
            const auto worker = [&]( const size_t index )
            {
                ++ready;
                while ( !start.load() )
                {
                    std::this_thread::yield();
                }
                results[index] = Write( cachePath, cacheName, [&]( const ResourcePath& path )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds{ 30 } );
                    return writeData( path );
                } );
            };

            std::thread first{ worker, 0u };
            std::thread second{ worker, 1u };
            while ( ready.load() != 2u )
            {
                std::this_thread::yield();
            }
            start.store( true );
            first.join();
            second.join();

            CHECK( conversionCount.load() == 1u );
            CHECK( ( ( results[0]._status == WriteStatus::CREATED && results[1]._status == WriteStatus::ALREADY_EXISTS ) ||
                     ( results[1]._status == WriteStatus::CREATED && results[0]._status == WriteStatus::ALREADY_EXISTS ) ) );
            CHECK( std::filesystem::is_regular_file( target ) );
            CHECK_FALSE( std::filesystem::exists( temp ) );
        }

        SECTION( "Failed conversions remove their partial file and allow a retry" )
        {
            bool wrotePartial = false;
            const WriteResult failed = Write( cachePath, cacheName, [&]( const ResourcePath& path )
            {
                wrotePartial = writeData( path );
                return false;
            } );
            REQUIRE( wrotePartial );
            CHECK( failed._status == WriteStatus::CONVERSION_ERROR );
            CHECK_FALSE( std::filesystem::exists( target ) );
            CHECK_FALSE( std::filesystem::exists( temp ) );
            CHECK( Write( cachePath, cacheName, writeData )._status == WriteStatus::CREATED );
            CHECK( conversionCount.load() == 2u );
        }

        SECTION( "An unwritable parent is reported without invoking the converter" )
        {
            std::ofstream blocker{ directory / "nested" };
            blocker << "not a directory";
            blocker.close();
            REQUIRE( blocker.good() );

            const WriteResult failed = Write( cachePath, cacheName, writeData );
            CHECK( failed._status == WriteStatus::DIRECTORY_ERROR );
            CHECK( failed._error == FileError::FILE_CREATE_ERROR );
            CHECK( conversionCount.load() == 0u );
            CHECK( std::filesystem::is_regular_file( directory / "nested" ) );
        }

        SECTION( "Failed publication removes only the incomplete temporary file" )
        {
            const WriteResult failed = Write( cachePath, cacheName, [&]( const ResourcePath& path )
            {
                return writeData( path ) && std::filesystem::create_directory( target );
            } );
            CHECK( failed._status == WriteStatus::MOVE_ERROR );
            CHECK( failed._error == FileError::FILE_MOVE_ERROR );
            CHECK( std::filesystem::is_directory( target ) );
            CHECK_FALSE( std::filesystem::exists( temp ) );
        }
    }
}
