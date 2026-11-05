// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Environment/Terrain/Headers/TerrainDescriptor.h"
#include "Environment/Vegetation/Headers/Vegetation.h"

namespace Divide
{
    TEST_CASE( "Terrain tile-factor shader declaration contains a literal opening brace", "[terrain][render_smoke]" )
    {
        CHECK( BuildTerrainTileFactorArrayHeader( 1u ) == "const vec2 CURRENT_TILE_FACTORS[4] = {\n" );
        CHECK( BuildTerrainTileFactorArrayHeader( 3u ) == "const vec2 CURRENT_TILE_FACTORS[12] = {\n" );
        CHECK( BuildTerrainTileFactorArrayHeader( 8u ) == "const vec2 CURRENT_TILE_FACTORS[32] = {\n" );
    }

    TEST_CASE( "Terrain descriptors load the vegetation billboard directory", "[terrain][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        boost::property_tree::ptree terrainNode;
        terrainNode.put( "descriptor", "Main" );
        terrainNode.put( "waterCaustics", "terrain_water_caustics.jpg" );

        TerrainDescriptor descriptor{};
        Init( descriptor, "TerrainTest" );
        REQUIRE( LoadFromXML( descriptor, terrainNode, "TerrainTest" ) );
        CHECK( GetVariable( descriptor, "vegetationTextureLocation" ) == "Assets/MiscImages" );
        CHECK( GetVariable( descriptor, "grassBillboard1" ) == "billboardgrass0001.png" );
    }

    TEST_CASE( "Vegetation draw readiness and range belong to each chunk", "[vegetation][render_smoke]" )
    {
        std::array<VegetationChunkDrawState, 4> chunks{};
        chunks[1]._grassInstanceCount = 8u;
        chunks[2]._treeInstanceCount = 3u;
        chunks[3] = { 5u, 2u };

        CHECK_FALSE( chunks[0].hasInstances() );
        CHECK( chunks[1].hasInstances() );
        CHECK( chunks[2].hasInstances() );
        CHECK( chunks[3].hasInstances() );

        CHECK( chunks[1].requiredRenderRange( 100.f, 250.f ) == 100.f );
        CHECK( chunks[2].requiredRenderRange( 100.f, 250.f ) == 250.f );
        CHECK( chunks[3].requiredRenderRange( 300.f, 250.f ) == 300.f );

        chunks[1] = {};
        CHECK_FALSE( chunks[1].hasInstances() );
        CHECK( chunks[2].hasInstances() );
        CHECK( chunks[3].hasInstances() );
    }

    TEST_CASE( "Vegetation synchronization covers only the chunk's populated instances", "[vegetation][render_smoke]" )
    {
        const BufferRange<> first = VegetationBufferRange( 0u, 64u, 7u );
        const BufferRange<> next = VegetationBufferRange( 1u, 64u, 10u );
        const BufferRange<> distant = VegetationBufferRange( 100000u, 64u, 64u );

        CHECK( first._startOffset == 0u );
        CHECK( first._length == 7u * sizeof( VegetationData ) );
        CHECK( next._startOffset == 64u * sizeof( VegetationData ) );
        CHECK( next._length == 10u * sizeof( VegetationData ) );
        CHECK_FALSE( Overlaps( first, next ) );
        CHECK( distant._startOffset == size_t{ 100000u } * 64u * sizeof( VegetationData ) );
        CHECK( distant._length == 64u * sizeof( VegetationData ) );
    }

    TEST_CASE( "Tree culling bounds enclose vertices around the instance origin", "[vegetation][render_smoke]" )
    {
        const BoundingBox bounds{ float3{ -3.f, -4.f, -2.f }, float3{ 1.f, 2.f, 6.f } };
        CHECK( VegetationTreeCullRadius( bounds ) == Catch::Approx( std::sqrt( 61.f ) ) );
        CHECK( VegetationTreeCullRadius( BoundingBox{ float3{ -1.f }, float3{ 1.f } } ) == Catch::Approx( std::sqrt( 3.f ) ) );
    }
}
