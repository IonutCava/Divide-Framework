// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Environment/Water/Headers/Water.h"
#include "Scenes/Headers/SceneState.h"
#include "../../../Assets/Shaders/GLSL/Common/waterComposite.cmn"

#include <algorithm>
#include <cmath>

namespace Divide
{
    namespace Attorney
    {
        class WaterPlaneTestAccess
        {
        public:
            static void RegisterEditorFields( WaterPlane& water, PlatformContext& context )
            {
                water.registerEditorFields( context );
            }

            static void NotifyEditorChange( WaterPlane& water )
            {
                water.onEditorChange( "Blur refractions" );
            }
        };
    }

    namespace
    {
        // Keep in sync with Assets/Shaders/GLSL/Common/waterData.cmn
        constexpr F32 UNDERWATER_SHORELINE_HALF_WIDTH = 0.75f;
        constexpr F32 UNDERWATER_SHALLOW_FADE = 20.0f;
        constexpr F32 WATER_TEST_EPSILON = 1e-5f;

        [[nodiscard]] F32 SmoothStep( const F32 edge0, const F32 edge1, const F32 x ) noexcept
        {
            const F32 t = std::clamp( (x - edge0) / (edge1 - edge0), 0.f, 1.f );
            return t * t * (3.f - 2.f * t);
        }

        [[nodiscard]] F32 Saturate( const F32 value ) noexcept
        {
            return std::clamp( value, 0.f, 1.f );
        }

        // CPU mirror of GetWaterDetails for a single water body.
        [[nodiscard]] float2 GetWaterDetailsCPU( const WaterBodyData& water, const float3& vertexW ) noexcept
        {
            const float4& extents = water._extents;
            const float4& position = water._positionW;

            if ( extents.x <= 0.f || extents.y <= 0.f || extents.z <= 0.f ||
                 std::abs( vertexW.x - position.x ) > extents.x * 0.5f ||
                 std::abs( vertexW.z - position.z ) > extents.z * 0.5f )
            {
                return float2{ 0.f, 1.f };
            }

            const F32 waterBottom = position.y - extents.y;
            const F32 surfaceY = position.y;

            F32 blendWeight = 0.f;
            F32 shallowFactor = 1.f;

            if ( vertexW.y > waterBottom )
            {
                blendWeight = 1.f - SmoothStep( surfaceY - UNDERWATER_SHORELINE_HALF_WIDTH,
                                                surfaceY + UNDERWATER_SHORELINE_HALF_WIDTH,
                                                vertexW.y );
            }

            if ( blendWeight > 0.f && vertexW.y < surfaceY )
            {
                const F32 fadeRange = std::max( std::min( extents.y, UNDERWATER_SHALLOW_FADE ), WATER_TEST_EPSILON );
                const F32 depthBelowSurface = surfaceY - vertexW.y;
                shallowFactor = Saturate( 1.f - depthBelowSurface / fadeRange );
            }

            return float2{ Saturate( blendWeight ), Saturate( shallowFactor ) };
        }

        [[nodiscard]] WaterBodyData MakeTestWaterBody( const float3& positionW,
                                                       const F32 width,
                                                       const F32 depth,
                                                       const F32 length ) noexcept
        {
            WaterBodyData water{};
            water._positionW = float4{ positionW, 0.f };
            water._extents = float4{ width, depth, length, 0.f };
            return water;
        }
    }

    TEST_CASE( "Water Fresnel conserves the planar reflection and refraction inputs", "[water][render_smoke]" )
    {
        const F32 normalIncidence = WATER_FRESNEL( 1.f );
        const F32 grazingIncidence = WATER_FRESNEL( 0.f );
        CHECK( normalIncidence == Catch::Approx( 0.02037f ).margin( 0.00001f ) );
        CHECK( grazingIncidence == Catch::Approx( 1.f ) );
        CHECK( WATER_COMPOSITE( 2.f, 4.f, 0.f ) == 2.f );
        CHECK( WATER_COMPOSITE( 2.f, 4.f, 1.f ) == 4.f );

        F32 previous = normalIncidence;
        for ( I32 i = 9; i >= 0; --i )
        {
            const F32 cosine = to_F32( i ) / 10.f;
            const F32 fresnel = WATER_FRESNEL( cosine );
            CHECK( fresnel >= previous );
            CHECK( fresnel <= 1.f );
            CHECK( WATER_COMPOSITE( 3.f, 3.f, fresnel ) == Catch::Approx( 3.f ) );
            previous = fresnel;
        }
    }

    TEST_CASE( "Water wave offsets vanish for flat normals or zero noise", "[water][render_smoke]" )
    {
        const float2 surfaceNormal{ 0.2f, 0.3f };
        const float2 waveNormal{ 0.6f, -0.1f };
        const float2 strength{ 0.2f, 0.1f };

        const float2 flatOffset = WATER_WAVE_OFFSET( surfaceNormal, surfaceNormal, strength );
        CHECK( flatOffset.x == 0.f );
        CHECK( flatOffset.y == 0.f );

        const float2 disabledOffset = WATER_WAVE_OFFSET( waveNormal, surfaceNormal, float2{ 0.f } );
        CHECK( disabledOffset.x == 0.f );
        CHECK( disabledOffset.y == 0.f );

        const float2 waveOffset = WATER_WAVE_OFFSET( waveNormal, surfaceNormal, strength );
        CHECK( waveOffset.x == Catch::Approx( 0.08f ) );
        CHECK( waveOffset.y == Catch::Approx( -0.04f ) );
    }

    TEST_CASE( "Water settings load from XML before graph insertion", "[water][scene_loading][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{ app };
        ResourceDescriptor<WaterPlane> descriptor{ "water_serialization_test" };
        WaterPlane water{ descriptor };
        Attorney::WaterPlaneTestAccess::RegisterEditorFields( water, context );

        boost::property_tree::ptree input;
        input.put( "dimensions.<xmlattr>.width", 1024u );
        input.put( "WATER.Blur__refractions", false );
        input.put( "WATER.Blur__kernel__size", 7u );
        input.put( "WATER.Refraction__Plane__Offset", 2.f );
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.x", 256.f );
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.y", 1024.f );

        water.loadFromXML( input );
        CHECK( water.getDimensions().width == 1024u );
        CHECK_FALSE( water.blurRefractions() );
        CHECK( water.blurKernelSize() == 7u );
        CHECK( water.refrPlaneOffset() == 2.f );
        CHECK( water.fogStartEnd().x == 256.f );
        CHECK( water.fogStartEnd().y == 1024.f );

        Attorney::WaterPlaneTestAccess::NotifyEditorChange( water );
        CHECK( water.fogStartEnd().y == 1024.f );

        boost::property_tree::ptree saved;
        water.saveToXML( saved );
        CHECK_FALSE( saved.get<bool>( "WATER.Blur__refractions" ) );
        CHECK( saved.get<F32>( "WATER.Refraction__Plane__Offset" ) == 2.f );
        CHECK( saved.get<F32>( "WATER.Fog__start__end__distances.<xmlattr>.y" ) == 1024.f );
    }

    TEST_CASE( "Water fog distances remain ordered after loading and editing", "[water][scene_loading][render_smoke]" )
    {
        platformInitRunListener::PlatformInit();
        Application app;
        PlatformContext context{ app };
        ResourceDescriptor<WaterPlane> descriptor{ "water_fog_test" };
        WaterPlane water{ descriptor };
        Attorney::WaterPlaneTestAccess::RegisterEditorFields( water, context );

        boost::property_tree::ptree input;
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.x", 20.f );
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.y", 10.f );
        water.loadFromXML( input );
        CHECK( water.fogStartEnd().x == 20.f );
        CHECK( water.fogStartEnd().y == Catch::Approx( 20.1f ) );

        water.fogStartEnd( float2{ 30.f, 30.f } );
        Attorney::WaterPlaneTestAccess::NotifyEditorChange( water );
        CHECK( water.fogStartEnd().y == Catch::Approx( 30.1f ) );
    }

    TEST_CASE( "PointInsideWater matches the water volume footprint and depth slab", "[water][render_smoke]" )
    {
        const WaterBodyData water = MakeTestWaterBody( float3{ 0.f, 0.f, 0.f }, 100.f, 50.f, 80.f );

        CHECK( PointInsideWater( water, float3{ 0.f, -1.f, 0.f } ) );
        CHECK( PointInsideWater( water, float3{ 40.f, -25.f, 30.f } ) );
        CHECK_FALSE( PointInsideWater( water, float3{ 0.f, 0.1f, 0.f } ) );   // above surface
        CHECK_FALSE( PointInsideWater( water, float3{ 0.f, -50.f, 0.f } ) );  // on / below bottom
        CHECK_FALSE( PointInsideWater( water, float3{ 60.f, -1.f, 0.f } ) );  // outside X footprint
        CHECK_FALSE( PointInsideWater( water, float3{ 0.f, -1.f, 50.f } ) );  // outside Z footprint

        WaterBodyData inactive{};
        CHECK_FALSE( PointInsideWater( inactive, float3{ 0.f, -1.f, 0.f } ) );
    }

    TEST_CASE( "GetWaterDetailsCPU clamps to the water volume and fades shallow to deep", "[water][render_smoke]" )
    {
        const WaterBodyData water = MakeTestWaterBody( float3{ 0.f, 0.f, 0.f }, 100.f, 50.f, 80.f );

        const float2 dry = GetWaterDetailsCPU( water, float3{ 0.f, 2.f, 0.f } );
        CHECK( dry.x == Catch::Approx( 0.f ).margin( 0.001f ) );
        CHECK( dry.y == Catch::Approx( 1.f ).margin( 0.001f ) );

        const float2 belowBottom = GetWaterDetailsCPU( water, float3{ 0.f, -51.f, 0.f } );
        CHECK( belowBottom.x == Catch::Approx( 0.f ).margin( 0.001f ) );

        const float2 deep = GetWaterDetailsCPU( water, float3{ 0.f, -UNDERWATER_SHALLOW_FADE, 0.f } );
        CHECK( deep.x == Catch::Approx( 1.f ).margin( 0.001f ) );
        CHECK( deep.y == Catch::Approx( 0.f ).margin( 0.001f ) );

        const float2 mid = GetWaterDetailsCPU( water, float3{ 0.f, -10.f, 0.f } );
        CHECK( mid.x == Catch::Approx( 1.f ).margin( 0.001f ) );
        CHECK( mid.y == Catch::Approx( 0.5f ).margin( 0.001f ) );

        const float2 nearSurface = GetWaterDetailsCPU( water, float3{ 0.f, -0.1f, 0.f } );
        CHECK( nearSurface.x == Catch::Approx( 1.f ).margin( 0.001f ) );
        CHECK( nearSurface.y == Catch::Approx( 0.995f ).margin( 0.01f ) );

        // Shoreline band: halfway above the surface inside the soft width.
        const float2 shoreline = GetWaterDetailsCPU( water, float3{ 0.f, UNDERWATER_SHORELINE_HALF_WIDTH * 0.5f, 0.f } );
        CHECK( shoreline.x > 0.f );
        CHECK( shoreline.x < 1.f );
        CHECK( shoreline.y == Catch::Approx( 1.f ).margin( 0.001f ) );

        // Shallow fade is capped by body depth when the pool is shallower than UNDERWATER_SHALLOW_FADE.
        const WaterBodyData shallowPool = MakeTestWaterBody( float3{ 0.f, 0.f, 0.f }, 40.f, 5.f, 40.f );
        const float2 shallowBottom = GetWaterDetailsCPU( shallowPool, float3{ 0.f, -4.9f, 0.f } );
        CHECK( shallowBottom.x == Catch::Approx( 1.f ).margin( 0.001f ) );
        CHECK( shallowBottom.y == Catch::Approx( 1.f - 4.9f / 5.f ).margin( 0.01f ) );
    }
}
