// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"
#include "Environment/Terrain/Headers/TerrainDescriptor.h"
#include "Environment/Vegetation/Headers/Vegetation.h"
#include "../../../Assets/Shaders/GLSL/Common/treeWind.cmn"

namespace Divide
{
    TEST_CASE("Tree wind sways scaled branches inside conservative culling bounds", "[vegetation][wind]")
    {
        using std::cos;
        using std::sin;
        CHECK_FALSE(TREE_WIND_ANIMATED(1u, 0.f));
        CHECK_FALSE(TREE_WIND_ANIMATED(1u, 0.85f));
        CHECK(TREE_WIND_ANIMATED(1u, 0.851f));
        CHECK_FALSE(TREE_WIND_ANIMATED(2u, 20.f));
        CHECK(TREE_WIND_AMPLITUDE(0.7f, 0.f) == 0.f);
        CHECK(TREE_WIND_PADDING(20.f, 0.f) == 0.f);

        const float2 wind{0.1f, 0.7f};
        for (const F32 scale : {0.05f, 1.f, 8.f})
        {
            const F32 x = 0.7f * scale;
            const F32 height = 10.f * scale;
            const F32 radius = float2{x, height}.length();
            const F32 amplitude = TREE_WIND_AMPLITUDE(x, height);
            const float2 initial{amplitude * wind.x, 0.f};
            const float2 later{amplitude * cos(1.f) * wind.x, amplitude * sin(1.f) * wind.y};
            CHECK(initial != later);
            for (I32 i = 0; i < 100; ++i)
            {
                const F32 time = to_F32(i) * 0.1f;
                const float2 offset{amplitude * cos(time) * wind.x, amplitude * sin(time) * wind.y};
                CHECK(offset.length() <= TREE_WIND_PADDING(radius, wind.length()));
            }
        }
    }

    TEST_CASE("Tree wind invalidates cached shadows once per frame and when wind stops", "[vegetation][wind][shadow]")
    {
        VegetationWindState state;
        const float3 wind{0.1f, 0.7f, 2.f};
        CHECK(state.update(0u, 16667u, wind));
        CHECK_FALSE(state.update(0u, 16667u, wind));
        CHECK(state.update(1u, 16667u, wind));
        CHECK_FALSE(state.update(2u, 0u, wind));
        CHECK(state.update(3u, 16667u, float3{0.f}));
        CHECK_FALSE(state.update(4u, 16667u, float3{0.f}));
        CHECK(state.update(5u, 0u, float3{0.3f, 0.5f, 0.f}));
        CHECK_FALSE(state.update(6u, 16667u, float3{0.3f, 0.5f, 0.f}));
    }

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
