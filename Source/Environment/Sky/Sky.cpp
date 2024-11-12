

#include "Headers/Sky.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Headers/Sun.h"

#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Geometry/Material/Headers/Material.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Geometry/Shapes/Predefined/Headers/Sphere3D.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include "ECS/Components/Headers/RenderingComponent.h"
#include "ECS/Components/Headers/BoundsComponent.h"

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4505)
#endif

#include <TileableVolumeNoise.h>
#include <CurlNoise/Curl.h>

#ifndef STBI_INCLUDE_STB_IMAGE_WRITE_H
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <stb_image.h>
#endif //STBI_INCLUDE_STB_IMAGE_WRITE_H
#ifdef _MSC_VER
# pragma warning (pop)
#endif

namespace Divide
{
    namespace
    {
        constexpr bool g_alwaysGenerateWeatherTextures = false;
        constexpr bool g_useGroundTruthTextures = !g_alwaysGenerateWeatherTextures && false;

        const auto procLocation = []()
        {
            return Paths::g_proceduralTexturesLocation / (g_useGroundTruthTextures ? "ground_truth" : "");
        };

        const string curlTexName{ "curlnoise.bmp" };
        const string weatherTexName{ "weather.bmp" };
        const string worlTexName{ "worlnoise.bmp" };
        const string perlWorlTexName{ "perlworlnoise.tga" };

        void GenerateCurlNoise( const char* fileName, const I32 width, const I32 height )
        {
            Byte* data = new Byte[width * height * 4];
            memset( data, 255, width * height * 4u );

            constexpr F32 frequency[] = { 8.0f, 6.0f, 4.0f };

            for ( I32 i = 0; i < width * height * 4; i += 4 )
            {
                const Vectormath::Aos::Vector3 pos
                {
                    to_F32( (i / 4) % width ) / width,
                    to_F32( ((i / 4) / height) ) / height,
                    height / 10000.f
                };
                for ( U8 pass = 0u; pass < 3; ++pass )
                {
                    CurlNoise::SetCurlSettings( false, frequency[pass], 3, 2.f, 0.5f );
                    const auto res = CurlNoise::ComputeCurlNoBoundaries( pos );
                    const vec3<F32> curl = Normalized( vec3<F32>{res.val[0], res.val[1], res.val[2]} );
                    const F32 cellFBM0 = curl.r * 0.5f + curl.g * 0.35f + curl.b * 0.15f;

                    data[i + pass] = to_byte( cellFBM0 * 128.f + 127.f );
                }
            }

            stbi_write_bmp( fileName, width, height, 4, data );
            delete[] data;
        }

        void GeneratePerlinNoise( const char* fileName, const I32 width, const I32 height )
        {
            const auto smoothstep = []( const F32 edge0, const F32 edge1, const F32 x )
            {
                const F32 t = std::min( std::max( (x - edge0) / (edge1 - edge0), 0.0f ), 1.0f );
                return t * t * (3.f - 2.f * t);
            };


            Byte* data = new Byte[width * height * 4];
            memset( data, 255, width * height * 4u );

            for ( I32 i = 0; i < width * height * 4; i += 4 )
            {
                const glm::vec3 pos = glm::vec3( to_F32( (i / 4) % width ) / to_F32( width ),
                                                 to_F32( (i / 4) / height ) / to_F32( height ),
                                                 0.051f );
                const glm::vec3 offset1 = glm::vec3( 0.f, 0.f, 581.163f );
                const glm::vec3 offset2 = glm::vec3( 0.f, 0.f, 1245.463f );
                //const glm::vec3 offset3 = glm::vec3(0.f, 0.f, 2245.863f);

                const F32 perlinNoise = Tileable3dNoise::PerlinNoise( pos, 8, 3 );
                const F32 perlinNoise2 = Tileable3dNoise::PerlinNoise( pos + offset1, 8, 3 );
                F32 perlinNoise3 = Tileable3dNoise::PerlinNoise( pos + offset2, 2, 3 );
                //F32 perlinNoise4 = Tileable3dNoise::PerlinNoise(pos + offset3, 4, 3);
                perlinNoise3 = std::min( 1.f, (smoothstep( 0.45f, 0.8f, perlinNoise3 ) + smoothstep( 0.25f, 0.45f, perlinNoise3 ) * 0.5f) );
                data[i + 0] = to_byte( perlinNoise * 128.f + 127.f );
                data[i + 1] = to_byte( smoothstep( 0.5f, 0.7f, perlinNoise2 ) * 255.f );
                data[i + 2] = to_byte( perlinNoise3 * 255.f );
            }
            stbi_write_bmp( fileName, width, height, 4, data );
            delete[] data;
        }

        void GenerateWorleyNoise( const char* fileName, const I32 width, const I32 height, const I32 slices )
        {
            Byte* worlNoiseArray = new Byte[slices * width * height * 4];
            memset( worlNoiseArray, 255, slices * width * height * 4u );

            for ( I32 i = 0; i < slices * width * height * 4; i += 4 )
            {
                const glm::vec3 pos = glm::vec3( to_F32( (i / 4) % width ) / to_F32( width ),
                                                 to_F32( ((i / 4) / height) % height ) / to_F32( height ),
                                                 to_F32( (i / 4) / (slices * slices) ) / to_F32( slices ) );
                const F32 cell0 = 1.f - Tileable3dNoise::WorleyNoise( pos, 2.f );
                const F32 cell1 = 1.f - Tileable3dNoise::WorleyNoise( pos, 4.f );
                const F32 cell2 = 1.f - Tileable3dNoise::WorleyNoise( pos, 8.f );
                const F32 cell3 = 1.f - Tileable3dNoise::WorleyNoise( pos, 16.f );

                const F32 cellFBM0 = cell0 * 0.5f + cell1 * 0.35f + cell2 * 0.15f;
                const F32 cellFBM1 = cell1 * 0.5f + cell2 * 0.35f + cell3 * 0.15f;
                const F32 cellFBM2 = cell2 * 0.75f + cell3 * 0.25f; // cellCount=4 -> worleyNoise4 is just noise due to sampling frequency=texel freque. So only take into account 2 frequenciM
                worlNoiseArray[i + 0] = to_byte( cellFBM0 * 255 );
                worlNoiseArray[i + 1] = to_byte( cellFBM1 * 255 );
                worlNoiseArray[i + 2] = to_byte( cellFBM2 * 255 );
            }
            stbi_write_bmp( fileName, width * slices, height, 4, worlNoiseArray );
            delete[] worlNoiseArray;
        }

        void GeneratePerlinWorleyNoise( PlatformContext& context, const char* fileName, const I32 width, const I32 height, const I32 slices )
        {
            constexpr bool s_parallelBuild = false;

            Byte* perlWorlNoiseArray = new Byte[slices * width * height * 4];

            const auto cbk = [width, height, slices, perlWorlNoiseArray](const U32 i)
            {
                const glm::vec3 pos = glm::vec3( to_F32( (i / 4) % width ) / to_F32( width ),
                                                 to_F32( ((i / 4) / height) % height ) / to_F32( height ),
                                                 to_F32( (i / 4) / (slices * slices) ) / to_F32( slices ) );
                // Perlin FBM noise
                const F32 perlinNoise = Tileable3dNoise::PerlinNoise( pos, 8, 3 );

                const F32 worleyNoise00 = (1.f - Tileable3dNoise::WorleyNoise( pos, 8 ));
                const F32 worleyNoise01 = (1.f - Tileable3dNoise::WorleyNoise( pos, 32 ));
                const F32 worleyNoise02 = (1.f - Tileable3dNoise::WorleyNoise( pos, 56 ));
                //const F32 worleyNoise3 = (1.f - Tileable3dNoise::WorleyNoise(coord, 80));
                //const F32 worleyNoise4 = (1.f - Tileable3dNoise::WorleyNoise(coord, 104));
                //const F32 worleyNoise5 = (1.f - Tileable3dNoise::WorleyNoise(coord, 128)); // half the frequency of texel, we should not go further (with cellCount = 32 and texture size = 64)
                                                                                              // PerlinWorley noise as described p.101 of GPU Pro 7
                const F32 worleyFBM = worleyNoise00 * 0.625f + worleyNoise01 * 0.25f + worleyNoise02 * 0.125f;
                const F32 PerlWorlNoise = MAP( perlinNoise, 0.f, 1.f, worleyFBM, 1.f );

                //F32 worleyNoise0 = (1.f - Tileable3dNoise::WorleyNoise(coord, 4));
                //F32 worleyNoise1 = (1.f - Tileable3dNoise::WorleyNoise(coord, 8));
                const F32 worleyNoise12 = (1.f - Tileable3dNoise::WorleyNoise( pos, 16 ));
                //F32 worleyNoise3 = (1.f - Tileable3dNoise::WorleyNoise(coord, 32));
                const F32 worleyNoise14 = (1.f - Tileable3dNoise::WorleyNoise( pos, 64 ));
                // Three frequency of Worley FBM noise
                const F32 worleyFBM0 = worleyNoise00 * 0.625f + worleyNoise12 * 0.25f + worleyNoise01 * 0.125f;
                const F32 worleyFBM1 = worleyNoise12 * 0.625f + worleyNoise01 * 0.25f + worleyNoise14 * 0.125f;
                const F32 worleyFBM2 = worleyNoise01 * 0.750f + worleyNoise14 * 0.25f; // cellCount=4 -> worleyNoise5 is just noise due to sampling frequency=texel frequency. So only take into account 2 frequencies for FBM
                perlWorlNoiseArray[i + 0] = to_byte( PerlWorlNoise * 255 );
                perlWorlNoiseArray[i + 1] = to_byte( worleyFBM0 * 255 );
                perlWorlNoiseArray[i + 2] = to_byte( worleyFBM1 * 255 );
                perlWorlNoiseArray[i + 3] = to_byte( worleyFBM2 * 255 );
            };

            if constexpr ( s_parallelBuild )
            {
                ParallelForDescriptor descriptor = {};
                descriptor._iterCount = slices * width * height * 4u;
                descriptor._partitionSize = slices;
                Parallel_For( context.taskPool( TaskPoolType::HIGH_PRIORITY ), descriptor, [&]( const Task*, const U32 start, const U32 end ) -> void
                {
                    for ( U32 i = start; i < end; i += 4 )
                    {
                        cbk(i);
                    }
                });
            }
            else
            {
                for ( U32 i = 0; i < to_U32( slices * width * height * 4u ); i += 4 )
                {
                    cbk(i);
                }
            }
        
            stbi_write_tga( fileName, width * slices, height, 4, perlWorlNoiseArray );
            delete[] perlWorlNoiseArray;
        }
    }


void Sky::OnStartup( PlatformContext& context )
{
    static bool init = false;
    if ( init )
    {
        return;
    }

    init = true;
    //ref: https://github.com/clayjohn/realtime_clouds/blob/master/gen_noise.cpp
    std::array<Task*, 3> tasks{ nullptr };

    const ResourcePath curlNoise = procLocation() / curlTexName;
    const ResourcePath weather = procLocation() / weatherTexName;
    const ResourcePath worlNoise = procLocation() / worlTexName;
    const ResourcePath perWordNoise = procLocation() / perlWorlTexName;

    if ( g_alwaysGenerateWeatherTextures || !fileExists( curlNoise ) )
    {
        Console::printfn( "Generating Curl Noise 128x128 RGB" );
        tasks[0] = CreateTask( [fileName = curlNoise.string()]( const Task& )
                               {
                                   GenerateCurlNoise( fileName.c_str(), 128, 128 );
                               } );
        Start( *tasks[0], context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
        Console::printfn( "Done!" );
    }
    
    if ( g_alwaysGenerateWeatherTextures || !fileExists( weather ) )
    {
        Console::printfn( "Generating Perlin Noise for LUT's" );
        Console::printfn( "Generating weather Noise 512x512 RGB" );
        tasks[1] = CreateTask( [fileName = weather.string()]( const Task& )
                               {
                                   GeneratePerlinNoise( fileName.c_str(), 512, 512 );
                               } );
        Start( *tasks[1], context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
        Console::printfn( "Done!" );
    }

    if ( g_alwaysGenerateWeatherTextures || !fileExists( worlNoise ) )
    {
        //worley and perlin-worley are from github/sebh/TileableVolumeNoise
        //which is in turn based on noise described in 'real time rendering of volumetric cloudscapes for horizon zero dawn'
        Console::printfn( "Generating Worley Noise 32x32x32 RGB" );
        tasks[2] = CreateTask( [fileName = worlNoise.string()]( const Task& )
                               {
                                   GenerateWorleyNoise( fileName.c_str(), 32, 32, 32 );
                               } );
        Start( *tasks[2], context.taskPool( TaskPoolType::HIGH_PRIORITY ) );
        Console::printfn( "Done!" );
    }

    if ( g_alwaysGenerateWeatherTextures || !fileExists( perWordNoise ) )
    {
        Console::printfn( "Generating Perlin-Worley Noise 128x128x128 RGBA" );
        GeneratePerlinWorleyNoise( context, perWordNoise.string().c_str(), 128, 128, 128 );
        Console::printfn( "Done!" );
    }

    bool keepWaiting = true;
    while ( keepWaiting )
    {
        keepWaiting = false;
        for ( Task* task : tasks )
        {
            if ( task != nullptr && !Finished( *task ) )
            {
                keepWaiting = true;
                break;
            }
        }
    }
}

Sky::Sky( const ResourceDescriptor<Sky>& descriptor )
    : SceneNode( descriptor,
                 GetSceneNodeType<Sky>(),
                 to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) | to_base( ComponentType::SCRIPT ) )
    , _diameter( descriptor.ID() )
{
    nightSkyColour( { 0.05f, 0.06f, 0.1f, 1.f } );
    moonColour( { 1.0f, 1.f, 0.8f } );

    const time_t t = time( nullptr );
    _sun.SetLocation( -2.589910f, 51.45414f ); // Bristol :D
    _sun.SetDate( *localtime( &t ) );

    _renderState.addToDrawExclusionMask( RenderStage::SHADOW );
}

bool Sky::load( PlatformContext& context )
{
    if ( _sky != INVALID_HANDLE<Sphere3D> )
    {
        return false;
    }

    std::atomic_uint loadTasks = 0u;
    I32 x, y, n;
    const string perlWolFile = (procLocation() / perlWorlTexName).string();
    Byte* perlWorlData = (Byte*)stbi_load( perlWolFile.c_str(), &x, &y, &n, STBI_rgb_alpha );
    ImageTools::ImageData imgDataPerl = {};
    if ( !imgDataPerl.loadFromMemory( perlWorlData, to_size( x * y * n ), to_U16( y ), to_U16( y ), to_U16( x / y ), to_U8( STBI_rgb_alpha ) ) )
    {
        DIVIDE_UNEXPECTED_CALL();
    }
    stbi_image_free( perlWorlData );

    const string worlFile = (procLocation() / worlTexName).string();
    Byte* worlNoise = (Byte*)stbi_load( worlFile.c_str(), &x, &y, &n, STBI_rgb_alpha );
    ImageTools::ImageData imgDataWorl = {};
    if ( !imgDataWorl.loadFromMemory( worlNoise, to_size( x * y * n ), to_U16( y ), to_U16( y ), to_U16( x / y ), to_U8( STBI_rgb_alpha ) ) )
    {
        DIVIDE_UNEXPECTED_CALL();
    }
    stbi_image_free( worlNoise );

    _skyboxSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
    _skyboxSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
    _skyboxSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
    _skyboxSampler._minFilter = TextureFilter::LINEAR;
    _skyboxSampler._magFilter = TextureFilter::LINEAR;
    _skyboxSampler._mipSampling = TextureMipSampling::NONE;
    _skyboxSampler._anisotropyLevel = 0u;

    SamplerDescriptor noiseSamplerMipMap {};
    noiseSamplerMipMap._wrapU = TextureWrap::REPEAT;
    noiseSamplerMipMap._wrapV = TextureWrap::REPEAT;
    noiseSamplerMipMap._wrapW = TextureWrap::REPEAT;
    noiseSamplerMipMap._minFilter = TextureFilter::LINEAR;
    noiseSamplerMipMap._magFilter = TextureFilter::LINEAR;
    noiseSamplerMipMap._mipSampling = TextureMipSampling::LINEAR;
    noiseSamplerMipMap._anisotropyLevel = 8u;
    {
        STUBBED("ToDo: Investigate why weather textures don't work well with DDS conversion using NVTT + STB -Ionut");
        TextureDescriptor textureDescriptor{};
        textureDescriptor._texType = TextureType::TEXTURE_3D;
        textureDescriptor._textureOptions._alphaChannelTransparency = false;
        textureDescriptor._textureOptions._useDDSCache = false;
        textureDescriptor._mipMappingState = MipMappingState::AUTO;

        ResourceDescriptor<Texture> perlWorlDescriptor( "perlWorl", textureDescriptor );
        perlWorlDescriptor.waitForReady( true );
        _perlWorlNoiseTex = CreateResource( perlWorlDescriptor );
        Get( _perlWorlNoiseTex )->createWithData( imgDataPerl, {});

        ResourceDescriptor<Texture> worlDescriptor( "worlNoise", textureDescriptor );
        worlDescriptor.waitForReady( true );
        _worlNoiseTex = CreateResource( worlDescriptor );
        Get(_worlNoiseTex)->createWithData( imgDataWorl, {});

        textureDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;

        ResourceDescriptor<Texture> weatherDescriptor( "Weather", textureDescriptor );
        weatherDescriptor.assetName( weatherTexName );
        weatherDescriptor.assetLocation( procLocation() );
        weatherDescriptor.waitForReady( false );
        _weatherTex = CreateResource( weatherDescriptor );

        ResourceDescriptor<Texture> curlDescriptor( "CurlNoise", textureDescriptor );
        curlDescriptor.assetName( curlTexName );
        curlDescriptor.assetLocation( procLocation() );
        curlDescriptor.waitForReady( false );
        _curlNoiseTex = CreateResource( curlDescriptor );
    }
    {
        ResourceDescriptor<Texture> skyboxTextures( "SkyTextures" );
        skyboxTextures.assetName(
            "skyboxDay_FRONT.jpg, skyboxDay_BACK.jpg, skyboxDay_UP.jpg, skyboxDay_DOWN.jpg, skyboxDay_LEFT.jpg, skyboxDay_RIGHT.jpg," //Day
            "Milkyway_posx.jpg, Milkyway_negx.jpg, Milkyway_posy.jpg, Milkyway_negy.jpg, Milkyway_posz.jpg, Milkyway_negz.jpg"  //Night
        );
        skyboxTextures.assetLocation( Paths::g_imagesLocation / "SkyBoxes" );
        skyboxTextures.waitForReady( false );

        TextureDescriptor& skyboxTexture = skyboxTextures._propertyDescriptor;
        skyboxTexture._texType = TextureType::TEXTURE_CUBE_ARRAY;
        skyboxTexture._packing = GFXImagePacking::NORMALIZED_SRGB;
        skyboxTexture._textureOptions._alphaChannelTransparency = false;

        _skybox = CreateResource( skyboxTextures, loadTasks );
    }

    const F32 radius = _diameter * 0.5f;

    ResourceDescriptor<Sphere3D> skybox( "SkyBox" );
    skybox.flag( true );  // no default material;
    skybox.ID( 4 ); // resolution
    skybox.enumValue( Util::FLOAT_TO_UINT( radius ) ); // radius
    _sky = CreateResource( skybox );

    ResourcePtr<Sphere3D> skyPtr = Get(_sky);
    skyPtr->renderState().drawState( false );

    WAIT_FOR_CONDITION( loadTasks.load() == 0u );

    ResourceDescriptor<Material> skyMaterial( "skyMaterial_" + resourceName() );
    Handle<Material> skyMat = CreateResource( skyMaterial );
    ResourcePtr<Material> skyMatPtr = Get(skyMat);

    skyMatPtr->updatePriorirty( Material::UpdatePriority::High );
    skyMatPtr->properties().shadingMode( ShadingMode::BLINN_PHONG );
    skyMatPtr->properties().roughness( 0.01f );
    skyMatPtr->setPipelineLayout( PrimitiveTopology::TRIANGLE_STRIP, skyPtr->geometryBuffer()->generateAttributeMap() );
    skyMatPtr->computeRenderStateCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass, RenderStateBlock& blockInOut)
    {
        const bool planarReflection = stagePass._stage == RenderStage::REFLECTION && stagePass._variant == static_cast<RenderStagePass::VariantType>(ReflectorType::PLANAR);
        blockInOut._cullMode = planarReflection ? CullMode::BACK : CullMode::FRONT;
        blockInOut._zFunc = IsDepthPass( stagePass ) ? ComparisonFunction::LEQUAL : ComparisonFunction::EQUAL;
    });

    skyMatPtr->computeShaderCBK( []( [[maybe_unused]] Material* material, const RenderStagePass stagePass )
    {
        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "sky.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "sky.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        
        if ( IsDepthPass( stagePass ) )
        {
            vertModule._variant = "NoClouds";
            vertModule._defines.emplace_back("NO_ATMOSPHERE", true);

            shaderDescriptor._modules.push_back( vertModule );

            if ( stagePass._stage == RenderStage::DISPLAY )
            {
                fragModule._defines.emplace_back( "NO_ATMOSPHERE", true );
                fragModule._variant = "PrePass";
                shaderDescriptor._modules.push_back( fragModule );
                shaderDescriptor._name = "sky_PrePass";
            }
            else
            {
                shaderDescriptor._name = "sky_Depth";
            }
        }
        else
        {
            shaderDescriptor._name = "sky_Display";
            vertModule._variant = "Clouds";
            fragModule._variant = "Clouds";
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );
        }

        shaderDescriptor._globalDefines.emplace_back( "SUN_ENERGY 1000.f" );
        shaderDescriptor._globalDefines.emplace_back( "SUN_SIZE 0.00872663806f" );
        shaderDescriptor._globalDefines.emplace_back( "RAYLEIGH_SCALE 7994.f" );
        shaderDescriptor._globalDefines.emplace_back( "MIE_SCALE 1200.f" );
        shaderDescriptor._globalDefines.emplace_back( "RAYLEIGH_COEFF vec3(5.5f, 13.0f, 22.4f)" );
        shaderDescriptor._globalDefines.emplace_back( "MIE_COEFF vec3(21.0f)" );

        shaderDescriptor._globalDefines.emplace_back( "SKIP_REFLECT_REFRACT", true);
        shaderDescriptor._globalDefines.emplace_back( "dvd_NightSkyColour PushData0[0].xyz" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_RaySteps PushData0[0].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_MoonColour PushData0[1].xyz" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_MoonScale PushData0[1].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_UseDaySkybox (int(PushData0[2].x) == 1)" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_UseNightSkybox (int(PushData0[2].y) == 1)" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_CloudLayerMinMaxHeight PushData0[2].zw" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_SunDiskSize PushData0[3].x" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_PlanetRadius PushData0[3].y" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_CloudDensity PushData0[3].z" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_CloudCoverage PushData0[3].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_EnableClouds (int(PushData1[0].x) == 1)" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_Exposure PushData1[0].y" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_MieEccentricity PushData1[0].z" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_Turbidity PushData1[0].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_RayleighColour PushData1[1].xyz" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_Rayleigh PushData1[1].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_MieColour PushData1[2].xyz" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_Mie PushData1[2].w" );
        shaderDescriptor._globalDefines.emplace_back( "dvd_GroundColour PushData1[3].xyz" );

        return shaderDescriptor;
    });

    WaitForReady( Get(_weatherTex) );

    skyMatPtr->setTexture( TextureSlot::UNIT0, _skybox, _skyboxSampler, TextureOperation::NONE );
    skyMatPtr->setTexture( TextureSlot::HEIGHTMAP, _weatherTex, noiseSamplerMipMap, TextureOperation::NONE );
    skyMatPtr->setTexture( TextureSlot::UNIT1, _curlNoiseTex, noiseSamplerMipMap, TextureOperation::NONE );
    skyMatPtr->setTexture( TextureSlot::SPECULAR, _worlNoiseTex, noiseSamplerMipMap, TextureOperation::NONE );
    skyMatPtr->setTexture( TextureSlot::NORMALMAP, _perlWorlNoiseTex, noiseSamplerMipMap, TextureOperation::NONE );

    setMaterialTpl( skyMat );

    setBounds( BoundingBox( vec3<F32>( -radius ), vec3<F32>( radius ) ) );

    Console::printfn( LOCALE_STR( "CREATE_SKY_RES_OK" ) );

    return SceneNode::load( context );
}

void Sky::setSkyShaderData( const RenderStagePass renderStagePass, PushConstantsStruct& constantsInOut )
{
    U16 targetRayCount = rayCount();
    if ( targetRayCount > 16u &&
         renderStagePass._stage != RenderStage::DISPLAY &&
         renderStagePass._stage != RenderStage::NODE_PREVIEW )
    {
        targetRayCount = std::max<U16>(targetRayCount / 4, 4u);
    }

    constantsInOut.data[0]._vec[0].set( nightSkyColour().rgb, to_F32( targetRayCount ) );
    constantsInOut.data[0]._vec[1].set( moonColour().rgb, moonScale() );
    constantsInOut.data[0]._vec[2].set( useDaySkybox() ? 1.f : 0.f, useNightSkybox() ? 1.f : 0.f, _atmosphere._cloudLayerMinMaxHeight.min, _atmosphere._cloudLayerMinMaxHeight.max );
    constantsInOut.data[0]._vec[3].set( _atmosphere._sunDiskSize, _atmosphere._planetRadius, _atmosphere._cloudDensity, _atmosphere._cloudCoverage );
    constantsInOut.data[1]._vec[0].set( enableProceduralClouds() ? 1.f : 0.f, exposure(), _atmosphere._mieEccentricity, _atmosphere._turbidity );
    constantsInOut.data[1]._vec[1].set( _atmosphere._rayleighColour.r, _atmosphere._rayleighColour.g, _atmosphere._rayleighColour.b, _atmosphere._rayleigh );
    constantsInOut.data[1]._vec[2].set( _atmosphere._mieColour.r, _atmosphere._mieColour.g, _atmosphere._mieColour.b, _atmosphere._mie );
    constantsInOut.data[1]._vec[3].set( _groundColour.r, _groundColour.g, _groundColour.b, 0.f );
}

void Sky::postLoad( SceneGraphNode* sgn )
{
    DIVIDE_ASSERT( _sky != INVALID_HANDLE<Sphere3D> );

    SceneGraphNodeDescriptor skyNodeDescriptor;
    skyNodeDescriptor._serialize = false;
    skyNodeDescriptor._nodeHandle = FromHandle(_sky);
    skyNodeDescriptor._name = sgn->name() + "_geometry";
    skyNodeDescriptor._usageContext = NodeUsageContext::NODE_STATIC;
    skyNodeDescriptor._componentMask = to_base( ComponentType::TRANSFORM ) |
                                       to_base( ComponentType::BOUNDS ) |
                                       to_base( ComponentType::RENDERING ) |
                                       to_base( ComponentType::NAVIGATION );
    sgn->addChildNode( skyNodeDescriptor )->get<BoundsComponent>()->collisionsEnabled(false);
    sgn->get<BoundsComponent>()->collisionsEnabled( false );

    RenderingComponent* renderable = sgn->get<RenderingComponent>();
    if ( renderable )
    {
        renderable->lockLoD( 0u );
        renderable->toggleRenderOption( RenderingComponent::RenderOptions::CAST_SHADOWS, false );
        renderable->occlusionCull(false);
    }

    _defaultAtmosphere = atmosphere();

    PlatformContext& pContext = sgn->context();
    registerEditorComponent( pContext );
    DIVIDE_ASSERT( _editorComponent != nullptr );

    _editorComponent->onChangedCbk( [this]( const std::string_view field )
                                       {
                                           if ( field == "Reset To Scene Default" )
                                           {
                                               _atmosphere = defaultAtmosphere();
                                           }
                                           else if ( field == "Reset To Global Default" )
                                           {
                                               _atmosphere = initialAtmosphere();
                                           }
                                           else if ( field == "Enable Procedural Clouds" )
                                           {
                                               rebuildDrawCommands( true );
                                           }
                                           else if ( field == "Update Sky Light" )
                                           {
                                               SceneEnvironmentProbePool::SkyLightNeedsRefresh( true );
                                           }

                                           _atmosphereChanged = true;
                                       } );

    {
        EditorComponentField separatorField = {};
        separatorField._name = "Sun/Sky";
        separatorField._type = EditorComponentFieldType::SEPARATOR;
        _editorComponent->registerField( MOV( separatorField ) );

        EditorComponentField rayCountField = {};
        rayCountField._name = "Cloud Ray Count";
        rayCountField._tooltip = "Base number of rays used for cloud marching";
        rayCountField._data = &_rayCount;
        rayCountField._type = EditorComponentFieldType::SLIDER_TYPE;
        rayCountField._resetValue = 128.f;
        rayCountField._readOnly = false;
        rayCountField._range = { 32.f, 512.f };
        rayCountField._basicType = PushConstantType::UINT;
        rayCountField._basicTypeSize = PushConstantSize::WORD;
        _editorComponent->registerField( MOV( rayCountField ) );

        EditorComponentField sunIntensityField = {};
        sunIntensityField._name = "Sun Disk Size";
        sunIntensityField._tooltip = "(0.01x - 15.0x) - visual size of the sun disc.";
        sunIntensityField._data = &_atmosphere._sunDiskSize;
        sunIntensityField._type = EditorComponentFieldType::SLIDER_TYPE;
        sunIntensityField._resetValue = 1.f;
        sunIntensityField._readOnly = false;
        sunIntensityField._range = { 0.01f, 15.0f };
        sunIntensityField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( sunIntensityField ) );

        EditorComponentField skyLuminanceField = {};
        skyLuminanceField._name = "Exposure";
        skyLuminanceField._tooltip = "(0.01 - 128.f) - Tone mapping luminance value.";
        skyLuminanceField._data = &_exposure;
        skyLuminanceField._type = EditorComponentFieldType::SLIDER_TYPE;
        skyLuminanceField._readOnly = false;
        skyLuminanceField._range = { 0.01f, 128.f };
        skyLuminanceField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( skyLuminanceField ) );
    }
    {
        EditorComponentField separatorField = {};
        separatorField._name = "Atmosphere";
        separatorField._type = EditorComponentFieldType::SEPARATOR;
        _editorComponent->registerField( MOV( separatorField ) );

        EditorComponentField planetRadiusField = {};
        planetRadiusField._name = "Planet Radius (m)";
        planetRadiusField._tooltip = "The radius of the Earth (default: 6371e3m, range: [2000e3m...9000e3m])";
        planetRadiusField._data = &_atmosphere._planetRadius;
        planetRadiusField._type = EditorComponentFieldType::PUSH_TYPE;
        planetRadiusField._resetValue = 6'371'000.f;
        planetRadiusField._readOnly = false;
        planetRadiusField._range = { 2'000'000.f, 9'000'000.f };
        planetRadiusField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( planetRadiusField ) );

        EditorComponentField cloudHeightOffsetField = {};
        cloudHeightOffsetField._name = "Cloud height range (m)";
        cloudHeightOffsetField._tooltip = "Cloud layer will be limited to the range [cloudRadius + x, cloudRadius + y].";
        cloudHeightOffsetField._data = &_atmosphere._cloudLayerMinMaxHeight;
        cloudHeightOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
        cloudHeightOffsetField._readOnly = false;
        cloudHeightOffsetField._range = { 10.f, 50000.f };
        cloudHeightOffsetField._basicType = PushConstantType::VEC2;
        _editorComponent->registerField( MOV( cloudHeightOffsetField ) );

        EditorComponentField rayleighColourField = {};
        rayleighColourField._name = "Rayleigh Colour";
        rayleighColourField._data = &_atmosphere._rayleighColour;
        rayleighColourField._type = EditorComponentFieldType::PUSH_TYPE;
        rayleighColourField._readOnly = false;
        rayleighColourField._basicType = PushConstantType::FCOLOUR3;
        _editorComponent->registerField( MOV( rayleighColourField ) );

        EditorComponentField rayleighField = {};
        rayleighField._name = "Rayleigh Factor";
        rayleighField._data = &_atmosphere._rayleigh;
        rayleighField._type = EditorComponentFieldType::SLIDER_TYPE;
        rayleighField._resetValue = 2.f;
        rayleighField._readOnly = false;
        rayleighField._range = { 0.f, 64.0f };
        rayleighField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( rayleighField ) );

        EditorComponentField mieColourField = {};
        mieColourField._name = "Mie Colour";
        mieColourField._data = &_atmosphere._mieColour;
        mieColourField._type = EditorComponentFieldType::PUSH_TYPE;
        mieColourField._readOnly = false;
        mieColourField._basicType = PushConstantType::FCOLOUR3;
        _editorComponent->registerField( MOV( mieColourField ) );

        EditorComponentField mieField = {};
        mieField._name = "Mie Factor";
        mieField._data = &_atmosphere._mie;
        mieField._type = EditorComponentFieldType::SLIDER_TYPE;
        mieField._resetValue = 0.005f;
        mieField._readOnly = false;
        mieField._range = { 0.f, 64.0f };
        mieField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( mieField ) );

        EditorComponentField mieEccentricityField = {};
        mieEccentricityField._name = "Mie Eccentricity Factor";
        mieEccentricityField._data = &_atmosphere._mieEccentricity;
        mieEccentricityField._type = EditorComponentFieldType::SLIDER_TYPE;
        mieEccentricityField._resetValue = 0.8f;
        mieEccentricityField._readOnly = false;
        mieEccentricityField._range = { -1.f, 1.f };
        mieEccentricityField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( mieEccentricityField ) );

        EditorComponentField turbidityField = {};
        turbidityField._name = "Turbidity Factor";
        turbidityField._data = &_atmosphere._turbidity;
        turbidityField._type = EditorComponentFieldType::SLIDER_TYPE;
        turbidityField._resetValue = 10.f;
        turbidityField._readOnly = false;
        turbidityField._range = { 0.f, 1000.f };
        turbidityField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( turbidityField ) );
    }
    {
        EditorComponentField separatorField = {};
        separatorField._name = "Weather";
        separatorField._type = EditorComponentFieldType::SEPARATOR;
        _editorComponent->registerField( MOV( separatorField ) );

        EditorComponentField cloudCoverageField = {};
        cloudCoverageField._name = "Cloud Coverage";
        cloudCoverageField._data = &_atmosphere._cloudCoverage;
        cloudCoverageField._type = EditorComponentFieldType::SLIDER_TYPE;
        cloudCoverageField._resetValue = 0.35f;
        cloudCoverageField._readOnly = false;
        cloudCoverageField._range = { 0.001f, 1.f };
        cloudCoverageField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( cloudCoverageField ) );

        EditorComponentField cloudDensityField = {};
        cloudDensityField._name = "Cloud Density";
        cloudDensityField._data = &_atmosphere._cloudDensity;
        cloudDensityField._type = EditorComponentFieldType::SLIDER_TYPE;
        cloudDensityField._resetValue = 0.05f;
        cloudDensityField._readOnly = false;
        cloudDensityField._range = { 0.001f, 1.f };
        cloudDensityField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( cloudDensityField ) );
    }
    {
        EditorComponentField separator3Field = {};
        separator3Field._name = "Skybox";
        separator3Field._type = EditorComponentFieldType::SEPARATOR;
        _editorComponent->registerField( MOV( separator3Field ) );

        EditorComponentField useDaySkyboxField = {};
        useDaySkyboxField._name = "Use Day Skybox";
        useDaySkyboxField._data = &_useDaySkybox;
        useDaySkyboxField._type = EditorComponentFieldType::PUSH_TYPE;
        useDaySkyboxField._readOnly = false;
        useDaySkyboxField._basicType = PushConstantType::BOOL;
        _editorComponent->registerField( MOV( useDaySkyboxField ) );

        EditorComponentField useNightSkyboxField = {};
        useNightSkyboxField._name = "Use Night Skybox";
        useNightSkyboxField._data = &_useNightSkybox;
        useNightSkyboxField._type = EditorComponentFieldType::PUSH_TYPE;
        useNightSkyboxField._readOnly = false;
        useNightSkyboxField._basicType = PushConstantType::BOOL;
        _editorComponent->registerField( MOV( useNightSkyboxField ) );

        EditorComponentField useProceduralCloudsField = {};
        useProceduralCloudsField._name = "Enable Procedural Clouds";
        useProceduralCloudsField._data = &_enableProceduralClouds;
        useProceduralCloudsField._type = EditorComponentFieldType::PUSH_TYPE;
        useProceduralCloudsField._readOnly = false;
        useProceduralCloudsField._basicType = PushConstantType::BOOL;
        _editorComponent->registerField( MOV( useProceduralCloudsField ) );

        EditorComponentField groundColourField = {};
        groundColourField._name = "Ground Colour";
        groundColourField._data = &_groundColour;
        groundColourField._type = EditorComponentFieldType::PUSH_TYPE;
        groundColourField._readOnly = false;
        groundColourField._basicType = PushConstantType::FCOLOUR4;
        _editorComponent->registerField( MOV( groundColourField ) );

        EditorComponentField nightColourField = {};
        nightColourField._name = "Night Colour";
        nightColourField._data = &_nightSkyColour;
        nightColourField._type = EditorComponentFieldType::PUSH_TYPE;
        nightColourField._readOnly = false;
        nightColourField._basicType = PushConstantType::FCOLOUR4;
        _editorComponent->registerField( MOV( nightColourField ) );

        EditorComponentField moonColourField = {};
        moonColourField._name = "Moon Colour";
        moonColourField._data = &_moonColour;
        moonColourField._type = EditorComponentFieldType::PUSH_TYPE;
        moonColourField._readOnly = false;
        moonColourField._basicType = PushConstantType::FCOLOUR4;
        _editorComponent->registerField( MOV( moonColourField ) );

        EditorComponentField moonScaleField = {};
        moonScaleField._name = "Moon Scale";
        moonScaleField._data = &_moonScale;
        moonScaleField._type = EditorComponentFieldType::PUSH_TYPE;
        moonScaleField._readOnly = false;
        moonScaleField._range = { 0.001f, 0.99f };
        moonScaleField._basicType = PushConstantType::FLOAT;
        _editorComponent->registerField( MOV( moonScaleField ) );
    }
    {
        EditorComponentField separatorField = {};
        separatorField._name = "Reset";
        separatorField._type = EditorComponentFieldType::SEPARATOR;
        _editorComponent->registerField( MOV( separatorField ) );

        EditorComponentField resetSceneField = {};
        resetSceneField._name = "Reset To Scene Default";
        resetSceneField._tooltip = "Default = whatever value was set at load time for this scene.";
        resetSceneField._type = EditorComponentFieldType::BUTTON;
        _editorComponent->registerField( MOV( resetSceneField ) );

        EditorComponentField resetGlobalField = {};
        resetGlobalField._name = "Reset To Global Default";
        resetGlobalField._tooltip = "Default = whatever value was encoded into the engine.";
        resetGlobalField._type = EditorComponentFieldType::BUTTON;
        _editorComponent->registerField( MOV( resetGlobalField ) );

        EditorComponentField rebuildSkyLightField = {};
        rebuildSkyLightField._name = "Update Sky Light";
        rebuildSkyLightField._tooltip = "Rebuild the sky light data (refresh sky probe)";
        rebuildSkyLightField._type = EditorComponentFieldType::BUTTON;
        _editorComponent->registerField( MOV( rebuildSkyLightField ) );
    }

    SceneNode::postLoad( sgn );
}

bool Sky::postLoad()
{
    return SceneNode::postLoad();
}

bool Sky::unload()
{
    DestroyResource(_sky);
    return SceneNode::unload();
}

const SunInfo& Sky::setDateTime( struct tm* dateTime ) noexcept
{
    _sun.SetDate( *dateTime );
    return getCurrentDetails();
}

const SunInfo& Sky::setGeographicLocation( const SimpleLocation location ) noexcept
{
    _sun.SetLocation( location._longitude, location._latitude );
    return getCurrentDetails();
}

const SunInfo& Sky::setDateTimeAndLocation( struct tm* dateTime, SimpleLocation location ) noexcept
{
    _sun.SetLocation( location._longitude, location._latitude );
    _sun.SetDate( *dateTime );
    return getCurrentDetails();
}

const SunInfo& Sky::getCurrentDetails() const
{
    return _sun.GetDetails();
}

[[nodiscard]] vec3<F32> Sky::getSunPosition( const F32 radius ) const
{
    return _sun.GetSunPosition( radius );
}

[[nodiscard]] vec3<F32> Sky::getSunDirection( const F32 radius) const
{
    return Normalized(getSunPosition(radius));
}

bool Sky::isDay() const
{
    return getCurrentDetails().altitude > 0.f;
}

SimpleTime Sky::GetTimeOfDay() const noexcept
{
    return _sun.GetTimeOfDay();
}

SimpleLocation Sky::GetGeographicLocation() const noexcept
{
    return _sun.GetGeographicLocation();
}

void Sky::setAtmosphere( const Atmosphere& atmosphere )
{
    _atmosphere = atmosphere;
    _atmosphereChanged = true;
}

Handle<Texture> Sky::activeSkyBox() const noexcept
{
    return _skybox;
}

void Sky::sceneUpdate( const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState )
{
    if ( _atmosphereChanged )
    {
        if ( _atmosphere._cloudLayerMinMaxHeight < 1.f )
        {
            _atmosphere._cloudLayerMinMaxHeight = 1.f;
        }
        if ( _atmosphere._cloudLayerMinMaxHeight.min > _atmosphere._cloudLayerMinMaxHeight.max )
        {
            std::swap( _atmosphere._cloudLayerMinMaxHeight.min, _atmosphere._cloudLayerMinMaxHeight.max );
        }
        _atmosphereChanged = false;
    }

    SceneNode::sceneUpdate( deltaTimeUS, sgn, sceneState );
}

void Sky::enableProceduralClouds( const bool val )
{
    _enableProceduralClouds = val;
    _atmosphereChanged = true;
}

void Sky::useDaySkybox( const bool val )
{
    _useDaySkybox = val;
    _atmosphereChanged = true;
}

void Sky::useNightSkybox( const bool val )
{
    _useNightSkybox = val;
    _atmosphereChanged = true;
}

void Sky::moonScale( const F32 val )
{
    _moonScale = val;
    _atmosphereChanged = true;
}

void Sky::rayCount( const U16 val )
{
    _rayCount = CLAMPED<U16>(val, 32u, 512u);
    _atmosphereChanged = true;
}

void Sky::exposure( const F32 val )
{
    _exposure = CLAMPED(val, 0.01f, 128.0f);
    _atmosphereChanged = true;
}

void Sky::moonColour( const FColour4 val )
{
    _moonColour = val;
    _atmosphereChanged = true;
}

void Sky::nightSkyColour( const FColour4 val )
{
    _nightSkyColour = val;
    _atmosphereChanged = true;
}


void Sky::prepareRender( SceneGraphNode* sgn,
                         RenderingComponent& rComp,
                         RenderPackage& pkg,
                         GFX::MemoryBarrierCommand& postDrawMemCmd,
                         const RenderStagePass renderStagePass,
                         const CameraSnapshot& cameraSnapshot,
                         const bool refreshData )
{

    setSkyShaderData( renderStagePass, pkg.pushConstantsCmd()._fastData );

    const VertexBuffer_ptr& skyBuffer = Get(_sky)->geometryBuffer();
    rComp.setIndexBufferElementOffset(skyBuffer->firstIndexOffsetCount());
    SceneNode::prepareRender( sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData );
}

void Sky::buildDrawCommands( SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut )
{
    GenericDrawCommand& cmd = cmdsOut.emplace_back();
    toggleOption(cmd, CmdRenderOptions::RENDER_INDIRECT);

    const VertexBuffer_ptr& skyBuffer = Get(_sky)->geometryBuffer();
    cmd._sourceBuffer = skyBuffer->handle();
    cmd._cmd.indexCount = to_U32( skyBuffer->getIndexCount() );

    SceneEnvironmentProbePool::SkyLightNeedsRefresh( true );
    _atmosphereChanged = true;

    SceneNode::buildDrawCommands( sgn, cmdsOut );
}

}
