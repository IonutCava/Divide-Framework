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

    TEST_CASE("Tree wind animates only in passes that refresh their shadows every frame", "[vegetation][wind][shadow]")
    {
        const auto shadowPass = []( const ShadowType type, const U16 index, const U8 pass )
        {
            RenderStagePass stagePass{ RenderStage::SHADOW, RenderPassType::COUNT, index,
                                       static_cast<RenderStagePass::VariantType>(type) };
            stagePass._pass = static_cast<RenderStagePass::PassIndex>(pass);
            return stagePass;
        };

        CHECK( TreeWindAnimatedInPass( RenderStagePass{ RenderStage::DISPLAY, RenderPassType::PRE_PASS } ) );
        CHECK( TreeWindAnimatedInPass( RenderStagePass{ RenderStage::DISPLAY, RenderPassType::MAIN_PASS } ) );
        CHECK( TreeWindAnimatedInPass( RenderStagePass{ RenderStage::REFLECTION, RenderPassType::MAIN_PASS } ) );
        CHECK( TreeWindAnimatedInPass( shadowPass( ShadowType::SINGLE, 2u, 0u ) ) );
        CHECK( TreeWindAnimatedInPass( shadowPass( ShadowType::CUBEMAP, 1u, 5u ) ) );
        CHECK( TreeWindAnimatedInPass( shadowPass( ShadowType::CSM, 0u, 0u ) ) );
        CHECK( TreeWindAnimatedInPass( shadowPass( ShadowType::CSM, 1u, 0u ) ) );
        for ( U8 cascade = 1u; cascade < Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT; ++cascade )
        {
            CAPTURE( cascade );
            CHECK_FALSE( TreeWindAnimatedInPass( shadowPass( ShadowType::CSM, 0u, cascade ) ) );
        }
        CHECK_FALSE( TreeWindAnimatedInPass( ShadowMap::WorldAOStagePass() ) );

        CHECK( TREE_WIND_ENABLED( 1.f ) );
        CHECK_FALSE( TREE_WIND_ENABLED( 0.f ) );
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

    TEST_CASE( "Terrain descriptors load every grass map layer in order", "[terrain][vegetation][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        boost::property_tree::ptree terrainNode;
        terrainNode.put( "descriptor", "Main" );
        terrainNode.put( "waterCaustics", "terrain_water_caustics.jpg" );

        TerrainDescriptor descriptor{};
        Init( descriptor, "TerrainTest" );
        REQUIRE( LoadFromXML( descriptor, terrainNode, "TerrainTest" ) );
        REQUIRE( descriptor._grassMaps.size() == 2u );
        CHECK( descriptor._grassMaps[0]._file == "alpha_maps_Alpha1.png" );
        CHECK( descriptor._grassMaps[0]._channelMask == 3u );
        CHECK( descriptor._grassMaps[1]._file == "alpha_maps_Alpha2.png" );
        CHECK( descriptor._grassMaps[1]._channelMask == 8u );
    }

    TEST_CASE( "Grass map parsing keeps its order, applies defaults and enforces the layer cap", "[terrain][vegetation]" )
    {
        const auto addGrassMap = []( boost::property_tree::ptree& node, const std::string& file, const I32 mask )
        {
            boost::property_tree::ptree& child = node.add( "grassMap", file );
            if ( mask >= 0 )
            {
                child.put( "<xmlattr>.channelMask", mask );
            }
        };

        TerrainDescriptor::GrassMaps maps;
        SECTION( "A single layer defaults to every channel" )
        {
            boost::property_tree::ptree node;
            addGrassMap( node, "only.png", -1 );
            REQUIRE( ParseGrassMaps( node, maps, "test" ) );
            REQUIRE( maps.size() == 1u );
            CHECK( maps[0]._file == "only.png" );
            CHECK( maps[0]._channelMask == 15u );
        }

        SECTION( "Layers up to the cap load in document order" )
        {
            boost::property_tree::ptree node;
            for ( U8 i = 0u; i < MAX_GRASS_MAP_LAYERS; ++i )
            {
                addGrassMap( node, Util::StringFormat( "map{}.png", i ).c_str(), i );
            }
            REQUIRE( ParseGrassMaps( node, maps, "test" ) );
            REQUIRE( maps.size() == MAX_GRASS_MAP_LAYERS );
            for ( U8 i = 0u; i < MAX_GRASS_MAP_LAYERS; ++i )
            {
                CHECK( maps[i]._file == Util::StringFormat( "map{}.png", i ).c_str() );
                CHECK( maps[i]._channelMask == i );
            }
        }

        SECTION( "One layer over the cap is rejected" )
        {
            boost::property_tree::ptree node;
            for ( U8 i = 0u; i <= MAX_GRASS_MAP_LAYERS; ++i )
            {
                addGrassMap( node, "map.png", 1 );
            }
            CHECK_FALSE( ParseGrassMaps( node, maps, "test" ) );
        }

        SECTION( "A descriptor without any layer is rejected" )
        {
            boost::property_tree::ptree node;
            node.put( "treeMap", "trees.png" );
            CHECK_FALSE( ParseGrassMaps( node, maps, "test" ) );
        }

        SECTION( "Empty file names and out of range masks are rejected" )
        {
            boost::property_tree::ptree emptyFile;
            addGrassMap( emptyFile, "", 3 );
            CHECK_FALSE( ParseGrassMaps( emptyFile, maps, "test" ) );

            boost::property_tree::ptree badMask;
            addGrassMap( badMask, "map.png", 16 );
            CHECK_FALSE( ParseGrassMaps( badMask, maps, "test" ) );

            boost::property_tree::ptree edgeMask;
            addGrassMap( edgeMask, "map.png", 15 );
            CHECK( ParseGrassMaps( edgeMask, maps, "test" ) );
        }
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

    TEST_CASE( "Vegetation chunk bounds hold every sphere the cull shader tests", "[vegetation][render_smoke]" )
    {
        // instanceCullVegetation.glsl: rotate_vertex_position
        const auto rotate = []( const float3& v, const float4& q )
        {
            const float3 axis{ q.x, q.y, q.z };
            return v + Cross( axis, Cross( axis, v ) + v * q.w ) * 2.f;
        };
        const auto holds = []( const BoundingBox& box, const float3& centre, const F32 radius )
        {
            constexpr F32 tolerance = 1e-3f;
            const float3 low = centre - float3{ radius - tolerance };
            const float3 high = centre + float3{ radius - tolerance };
            return low.x >= box._min.x && low.y >= box._min.y && low.z >= box._min.z &&
                   high.x <= box._max.x && high.y <= box._max.y && high.z <= box._max.z;
        };

        const float4 grassExtents{ 1.f, 1.f, 1.f, 1.9f };
        const F32 cullRadius = 12.f;
        const float2 wind{ 0.1f, 0.7f };

        std::array<VegetationData, 3> instances{};
        instances[0]._positionAndScale = float4{ 10.f, 2.f, -30.f, 0.6f };
        instances[1]._positionAndScale = float4{ -40.f, 15.f, 25.f, 1.25f };
        instances[2]._positionAndScale = float4{ 5.f, -3.f, 60.f, 2.f };
        instances[0]._orientationQuat = quatf( WORLD_X_AXIS, Angle::to_RADIANS( Angle::DEGREES_F( 30.f ) ) )._elements;
        instances[1]._orientationQuat = quatf( WORLD_Z_AXIS, Angle::to_RADIANS( Angle::DEGREES_F( -75.f ) ) )._elements;
        instances[2]._orientationQuat = quatf( WORLD_Y_AXIS, Angle::to_RADIANS( Angle::DEGREES_F( 140.f ) ) )._elements;

        VegetationInstanceBounds bounds;
        CHECK( bounds.empty() );
        for ( const VegetationData& instance : instances )
        {
            bounds.add( instance );
        }
        REQUIRE_FALSE( bounds.empty() );
        CHECK( bounds._maxScale == 2.f );

        const BoundingBox grassBox = bounds.padded( VegetationGrassReach( grassExtents, bounds._maxScale, wind.x, wind.y ) );
        const BoundingBox treeBox = bounds.padded( VegetationTreeReach( cullRadius, bounds._maxScale, wind.x, wind.y ) );
        for ( const VegetationData& instance : instances )
        {
            const float3 origin = instance._positionAndScale.xyz;
            const F32 scale = instance._positionAndScale.w;
            const float3 grassCentre = origin + rotate( float3{ 0.f, grassExtents.y * scale * 0.5f, 0.f }, instance._orientationQuat );
            CHECK( holds( grassBox, grassCentre, grassExtents.w * scale ) );

            const F32 treeRadius = cullRadius * scale;
            CHECK( holds( treeBox, origin, treeRadius + TREE_WIND_PADDING( treeRadius, wind.length() ) ) );
        }

        CHECK( VegetationGrassReach( grassExtents, 1.f, 0.5f, 0.9f ) > VegetationGrassReach( grassExtents, 1.f, wind.x, wind.y ) );
        CHECK( VegetationTreeReach( cullRadius, 1.f, 0.5f, 0.9f ) > VegetationTreeReach( cullRadius, 1.f, wind.x, wind.y ) );
    }
}
