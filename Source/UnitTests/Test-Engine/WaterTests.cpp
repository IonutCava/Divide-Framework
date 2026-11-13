// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Environment/Water/Headers/Water.h"
#include "../../../Assets/Shaders/GLSL/Common/waterComposite.cmn"

namespace Divide
{
    struct WaterPlaneTestAccess
    {
        static void RegisterEditorFields( WaterPlane& water, PlatformContext& context )
        {
            water.registerEditorFields( context );
        }

        static void NotifyEditorChange( WaterPlane& water )
        {
            water.onEditorChange( "Blur refractions" );
        }
    };

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
        WaterPlaneTestAccess::RegisterEditorFields( water, context );

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

        WaterPlaneTestAccess::NotifyEditorChange( water );
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
        WaterPlaneTestAccess::RegisterEditorFields( water, context );

        boost::property_tree::ptree input;
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.x", 20.f );
        input.put( "WATER.Fog__start__end__distances.<xmlattr>.y", 10.f );
        water.loadFromXML( input );
        CHECK( water.fogStartEnd().x == 20.f );
        CHECK( water.fogStartEnd().y == Catch::Approx( 20.1f ) );

        water.fogStartEnd( float2{ 30.f, 30.f } );
        WaterPlaneTestAccess::NotifyEditorChange( water );
        CHECK( water.fogStartEnd().y == Catch::Approx( 30.1f ) );
    }
}
