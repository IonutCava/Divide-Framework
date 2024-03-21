

#include "config.h"

#include "Headers/GFXDevice.h"
#include "Headers/GFXRTPool.h"
#include "Editor/Headers/Editor.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "GUI/Headers/GUI.h"
#include "Scenes/Headers/SceneShaderData.h"

#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"

#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Headers/Renderer.h"
#include "Rendering/PostFX/Headers/PostFX.h"

#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Material/Headers/ShaderComputeQueue.h"

#include "Platform/File/Headers/FileManagement.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/IMPrimitive.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Platform/Video/RenderBackend/None/Headers/NoneWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Headers/CommandBufferPool.h"

namespace Divide
{

    namespace TypeUtil
    {
        const char* GraphicResourceTypeToName( const GraphicsResource::Type type ) noexcept
        {
            return Names::resourceTypes[to_base( type )];
        };

        const char* RenderStageToString( const RenderStage stage ) noexcept
        {
            return Names::renderStage[to_base( stage )];
        }

        RenderStage StringToRenderStage( const char* stage ) noexcept
        {
            for ( U8 i = 0; i < to_U8( RenderStage::COUNT ); ++i )
            {
                if ( strcmp( stage, Names::renderStage[i] ) == 0 )
                {
                    return static_cast<RenderStage>(i);
                }
            }

            return RenderStage::COUNT;
        }

        const char* RenderPassTypeToString( const RenderPassType pass ) noexcept
        {
            return Names::renderPassType[to_base( pass )];
        }

        RenderPassType StringToRenderPassType( const char* pass ) noexcept
        {
            for ( U8 i = 0; i < to_U8( RenderPassType::COUNT ); ++i )
            {
                if ( strcmp( pass, Names::renderPassType[i] ) == 0 )
                {
                    return static_cast<RenderPassType>(i);
                }
            }

            return RenderPassType::COUNT;
        }
    };

    namespace
    {
        /// How many writes we can basically issue per frame to our scratch buffers before we have to sync
        constexpr size_t TargetBufferSizeCam = 1024u;
        constexpr size_t TargetBufferSizeRender = 64u;

        constexpr U32 GROUP_SIZE_AABB = 64u;
        constexpr U32 MAX_INVOCATIONS_BLUR_SHADER_LAYERED = 4u;
        constexpr U32 DEPTH_REDUCE_LOCAL_SIZE = 32u;

        FORCE_INLINE U32 getGroupCount( const U32 threadCount, U32 const localSize )
        {
            return (threadCount + localSize - 1u) / localSize;
        }

        template<typename Data, size_t N>
        inline void DecrementPrimitiveLifetime( DebugPrimitiveHandler<Data, N>& container )
        {
            LockGuard<Mutex> w_lock( container._dataLock );
            for ( auto& entry : container._debugData )
            {
                if ( entry._frameLifeTime > 0u )
                {
                    entry._frameLifeTime -= 1u;
                }
            }
        }
    };

    RenderTargetID RenderTargetNames::BACK_BUFFER = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SCREEN = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SCREEN_PREV = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::NORMALS_RESOLVED = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::OIT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::OIT_REFLECT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SSAO_RESULT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SSR_RESULT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::HI_Z = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::HI_Z_REFLECT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::REFLECTION_PLANAR_BLUR = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::REFLECTION_CUBE = INVALID_RENDER_TARGET_ID;
    std::array<RenderTargetID, Config::MAX_REFLECTIVE_NODES_IN_VIEW> RenderTargetNames::REFLECTION_PLANAR = create_array<Config::MAX_REFLECTIVE_NODES_IN_VIEW, RenderTargetID>( INVALID_RENDER_TARGET_ID );
    std::array<RenderTargetID, Config::MAX_REFRACTIVE_NODES_IN_VIEW> RenderTargetNames::REFRACTION_PLANAR = create_array<Config::MAX_REFRACTIVE_NODES_IN_VIEW, RenderTargetID>( INVALID_RENDER_TARGET_ID );

    D64 GFXDevice::s_interpolationFactor = 1.0;
    U64 GFXDevice::s_frameCount = 0u;

    DeviceInformation GFXDevice::s_deviceInformation{};
    GFXDevice::IMPrimitivePool GFXDevice::s_IMPrimitivePool{};


    ImShaders::ImShaders(GFXDevice& context)
    {
        auto cache = context.context().kernel().resourceCache();

        ShaderModuleDescriptor vertModule = {};
        vertModule._moduleType = ShaderType::VERTEX;
        vertModule._sourceFile = "ImmediateModeEmulation.glsl";

        ShaderModuleDescriptor fragModule = {};
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._sourceFile = "ImmediateModeEmulation.glsl";

        ShaderProgramDescriptor shaderDescriptor = {};
        shaderDescriptor._modules.push_back( vertModule );
        shaderDescriptor._modules.push_back( fragModule );
        {
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imShader = CreateResource<ShaderProgram>( cache, immediateModeShader );
            assert( _imShader != nullptr );
        }
        {
            shaderDescriptor._globalDefines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation-NoTexture" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imShaderNoTexture = CreateResource<ShaderProgram>( cache, immediateModeShader );
            assert( _imShaderNoTexture != nullptr );
        }
        {
            efficient_clear( shaderDescriptor._globalDefines );
            shaderDescriptor._modules.back()._defines.emplace_back( "WORLD_PASS" );
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation-World" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imWorldShader = CreateResource<ShaderProgram>( cache, immediateModeShader );
            assert( _imWorldShader != nullptr );
        }
        {
            shaderDescriptor._globalDefines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation-World-NoTexture" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imWorldShaderNoTexture = CreateResource<ShaderProgram>( cache, immediateModeShader );
        }


        {
            efficient_clear( shaderDescriptor._globalDefines );
            shaderDescriptor._modules.back()._defines.emplace_back( "OIT_PASS" );
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation-OIT" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imWorldOITShader = CreateResource<ShaderProgram>( cache, immediateModeShader );
            assert( _imWorldOITShader != nullptr );
        }
        {
            shaderDescriptor._modules.back()._defines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor immediateModeShader( "ImmediateModeEmulation-OIT-NoTexture" );
            immediateModeShader.waitForReady( true );
            immediateModeShader.propertyDescriptor( shaderDescriptor );
            _imWorldOITShaderNoTexture = CreateResource<ShaderProgram>( cache, immediateModeShader );
            assert( _imWorldOITShaderNoTexture != nullptr );
        }
    }
#pragma region Construction, destruction, initialization

    ErrorCode GFXDevice::initDescriptorSets()
    {
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  0,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: diffuse0
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  1,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: opacity
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  2,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: normalMap
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  3,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: height
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  4,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: specular
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  5,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: metalness
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  6,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: roughness
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  7,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: occlusion
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  8,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: emissive
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW,  9,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: diffuse1
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 10,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: reflect
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 11,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::ALL );                  // Textures: refract
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 12,  DescriptorSetBindingType::IMAGE,                  ShaderStageVisibility::ALL );                  // Image
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 13,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::ALL );                  // SSBO (e.g. bone buffer or histogram buffer)
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 14,  DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::ALL );                  // Generic UBO (for uniforms)
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_DRAW, 15,  DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::ALL );                  // Generic UBO (for uniforms)

        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 0,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::NONE );                  // CMD_BUFFER
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 1,  DescriptorSetBindingType::UNIFORM_BUFFER,        ShaderStageVisibility::ALL );                   // CAM_BLOCK;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 2,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::COMPUTE );               // GPU_COMMANDS;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 3,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::ALL );                   // NODE_TRANSFORM_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 4,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::COMPUTE_AND_GEOMETRY );  // NODE_INDIRECTION_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_BATCH, 5,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER, ShaderStageVisibility::FRAGMENT );              // NODE_MATERIAL_DATA;

        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 0,   DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // SCENE_NORMALS;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 1,   DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // DEPTH;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 2,   DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // TRANSMITANCE;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 3,   DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // SSR_SAMPLE;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 4,   DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // SSAO_SAMPLE;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 5,   DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_GEOMETRY ); // TREE_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 6,   DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_GEOMETRY ); // GRASS_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 7,   DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE );              // ATOMIC_COUNTER;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 8,   DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::COMPUTE_AND_DRAW );     // LIGHT_SCENE;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 9,   DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW );     // LIGHT_NORMAL;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 10,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW );     // LIGHT_INDICES;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 11,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE_AND_DRAW );     // LIGHT_GRID;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 12,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE );              // LIGHT_CLUSTER_AABB;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_PASS, 13,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::COMPUTE );              // LIGHT_GLOBAL_INDEX_COUNT;
        
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 0,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // ENV Prefiltered
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 1,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // ENV Irradiance
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 2,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // BRDF Lut
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 3,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // Cube Reflection;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 4,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // Shadow Array
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 5,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // Shadow Cube
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 6,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // Shadow Single
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 7,  DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::FRAGMENT );             // PROBE_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 8,  DescriptorSetBindingType::UNIFORM_BUFFER,         ShaderStageVisibility::ALL_DRAW );             // SCENE_DATA;
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 9,  DescriptorSetBindingType::SHADER_STORAGE_BUFFER,  ShaderStageVisibility::FRAGMENT );             // LIGHT_SHADOW;

        _api->initDescriptorSets();

        return ShaderProgram::SubmitSetLayouts(*this);
    }

    void GFXDevice::GFXDescriptorSet::clear()
    {
        _impl._bindingCount = { 0u };
        dirty( true );
    }

    void GFXDevice::GFXDescriptorSet::update( const DescriptorSetUsage usage, const DescriptorSet& newBindingData )
    {
        for ( U8 i = 0u; i < newBindingData._bindingCount; ++i )
        {
            update( usage, newBindingData._bindings[i] );
        }
    }

    void GFXDevice::GFXDescriptorSet::update( const DescriptorSetUsage usage, const DescriptorSetBinding& newBindingData )
    {
        for ( U8 i = 0u; i < _impl._bindingCount; ++i )
        {
            DescriptorSetBinding& bindingEntry = _impl._bindings[i];

            if ( bindingEntry._slot == newBindingData._slot )
            {
                DIVIDE_ASSERT( bindingEntry._data._type != DescriptorSetBindingType::COUNT &&
                               newBindingData._data._type != DescriptorSetBindingType::COUNT );

                DIVIDE_ASSERT( usage == DescriptorSetUsage::PER_DRAW || bindingEntry._data._type == newBindingData._data._type );

                if ( bindingEntry != newBindingData )
                {
                    bindingEntry = newBindingData;
                    dirty( true );
                }
                return;
            }
        }

        DIVIDE_ASSERT( _impl._bindingCount < MAX_BINDINGS_PER_DESCRIPTOR_SET - 1u);
        _impl._bindings[_impl._bindingCount++] = newBindingData;
        dirty( true );
    }

    GFXDevice::GFXDevice( PlatformContext& context )
        : FrameListener( "GFXDevice", context.kernel().frameListenerMgr(), 1u),
          PlatformContextComponent( context )
    {
        _queuedShadowSampleChange.fill( s_invalidQueueSampleCount );
    }

    GFXDevice::~GFXDevice()
    {
        closeRenderingAPI();
    }

    ErrorCode GFXDevice::createAPIInstance( const RenderAPI API )
    {
        assert( _api == nullptr && "GFXDevice error: initRenderingAPI called twice!" );

        ErrorCode err = ErrorCode::NO_ERR;
        switch ( API )
        {
            case RenderAPI::OpenGL:
            {
                _api = eastl::make_unique<GL_API>( *this );
            } break;
            case RenderAPI::Vulkan:
            {
                _api = eastl::make_unique<VK_API>( *this );
            } break;
            case RenderAPI::None:
            {
                _api = eastl::make_unique<NONE_API>( *this );
            } break;
            default:
                err = ErrorCode::GFX_NON_SPECIFIED;
                break;
        };

        DIVIDE_ASSERT( _api != nullptr, LOCALE_STR( "ERROR_GFX_DEVICE_API" ) );
        renderAPI( API );

        return err;
    }

    /// Create a display context using the selected API and create all of the needed
    /// primitives needed for frame rendering
    ErrorCode GFXDevice::initRenderingAPI( const I32 argc, char** argv, const RenderAPI API )
    {
        ErrorCode hardwareState = createAPIInstance( API );
        Configuration& config = context().config();
        
        if ( hardwareState == ErrorCode::NO_ERR )
        {
            // Initialize the rendering API
            hardwareState = _api->initRenderingAPI( argc, argv, config );
        }

        if ( hardwareState != ErrorCode::NO_ERR )
        {
            // Validate initialization
            return hardwareState;
        }

        if ( s_deviceInformation._maxTextureUnits <= 16 )
        {
            Console::errorfn( LOCALE_STR( "ERROR_INSUFFICIENT_TEXTURE_UNITS" ) );
            return ErrorCode::GFX_OLD_HARDWARE;
        }
        if ( to_base( AttribLocation::COUNT ) >= s_deviceInformation._maxVertAttributeBindings )
        {
            Console::errorfn( LOCALE_STR( "ERROR_INSUFFICIENT_ATTRIB_BINDS" ) );
            return ErrorCode::GFX_OLD_HARDWARE;
        }
        DIVIDE_ASSERT( Config::MAX_CLIP_DISTANCES <= s_deviceInformation._maxClipDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts" );
        DIVIDE_ASSERT( Config::MAX_CULL_DISTANCES <= s_deviceInformation._maxCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts" );
        DIVIDE_ASSERT( Config::MAX_CULL_DISTANCES + Config::MAX_CLIP_DISTANCES <= s_deviceInformation._maxClipAndCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts" );

        DIVIDE_ASSERT( Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS < s_deviceInformation._maxWorgroupSize[0] &&
                       Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS < s_deviceInformation._maxWorgroupSize[1] &&
                       Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < s_deviceInformation._maxWorgroupSize[2] );

        DIVIDE_ASSERT( to_U32( Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS ) *
                       Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS *
                       Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < s_deviceInformation._maxWorgroupInvocations );

        string refreshRates;

        DisplayManager::OutputDisplayProperties prevMode;
        const auto printMode = [&prevMode, &refreshRates]()
        {
            Console::printfn( LOCALE_STR( "CURRENT_DISPLAY_MODE" ),
                              prevMode._resolution.width,
                              prevMode._resolution.height,
                              prevMode._bitsPerPixel,
                              prevMode._formatName.c_str(),
                              refreshRates.c_str() );
        };

        for ( size_t idx = 0; idx < DisplayManager::ActiveDisplayCount(); ++idx )
        {
            const auto& registeredModes = DisplayManager::GetDisplayModes( idx );
            if ( !registeredModes.empty() )
            {
                Console::printfn( LOCALE_STR( "AVAILABLE_VIDEO_MODES" ), idx, registeredModes.size() );

                prevMode = registeredModes.front();

                for ( const auto& it : registeredModes )
                {
                    if ( prevMode._resolution != it._resolution ||
                         prevMode._bitsPerPixel != it._bitsPerPixel ||
                         prevMode._formatName != it._formatName )
                    {
                        printMode();
                        refreshRates = "";
                        prevMode = it;
                    }

                    if ( refreshRates.empty() )
                    {
                        refreshRates = Util::to_string( to_U32( it._maxRefreshRate ) );
                    }
                    else
                    {
                        refreshRates.append( Util::StringFormat( ", %d", it._maxRefreshRate ) );
                    }
                }
            }
            if ( !refreshRates.empty() )
            {
                printMode();
            }
        }

        _rtPool = MemoryManager_NEW GFXRTPool( *this );

        I32 numLightsPerCluster = config.rendering.numLightsPerCluster;
        if ( numLightsPerCluster < 0 )
        {
            numLightsPerCluster = to_I32( Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER );
        }
        else
        {
            numLightsPerCluster = std::min( numLightsPerCluster, to_I32( Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER ) );
        }
        if ( numLightsPerCluster != config.rendering.numLightsPerCluster )
        {
            config.rendering.numLightsPerCluster = numLightsPerCluster;
            config.changed( true );
        }
        const U16 reflectionProbeRes = to_U16( nextPOW2( CLAMPED( to_U32( config.rendering.reflectionProbeResolution ), 16u, 4096u ) - 1u ) );
        if ( reflectionProbeRes != config.rendering.reflectionProbeResolution )
        {
            config.rendering.reflectionProbeResolution = reflectionProbeRes;
            config.changed( true );
        }


        hardwareState = ShaderProgram::OnStartup( context().kernel().resourceCache() );
        if ( hardwareState == ErrorCode::NO_ERR )
        {
            hardwareState = initDescriptorSets();
        }

        return hardwareState;
    }

    void GFXDevice::updateSceneDescriptorSet( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _sceneData->updateSceneDescriptorSet( bufferInOut, memCmdInOut );
    }

    void GFXDevice::resizeGPUBlocks( size_t targetSizeCam, size_t targetSizeCullCounter )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( targetSizeCam == 0u )
        {
            targetSizeCam = 1u;
        }
        if ( targetSizeCullCounter == 0u )
        {
            targetSizeCullCounter = 1u;
        }

        const bool resizeCamBuffer = _gfxBuffers.crtBuffers()._camDataBuffer == nullptr || _gfxBuffers.crtBuffers()._camDataBuffer->queueLength() != targetSizeCam;
        const bool resizeCullCounter = _gfxBuffers.crtBuffers()._cullCounter == nullptr || _gfxBuffers.crtBuffers()._cullCounter->queueLength() != targetSizeCullCounter;

        if ( !resizeCamBuffer && !resizeCullCounter )
        {
            return;
        }

        DIVIDE_ASSERT( ValidateGPUDataStructure(), "GFXDevice::resizeBlock: GPUBlock does not meet alignment requirements!");

        _gfxBuffers.reset( resizeCamBuffer, resizeCullCounter );

        if ( resizeCamBuffer )
        {
            ShaderBufferDescriptor bufferDescriptor = {};
            bufferDescriptor._bufferParams._elementCount = 1;
            bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::CONSTANT_BUFFER;
            bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::OFTEN;
            bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::CPU_TO_GPU;
            bufferDescriptor._ringBufferLength = to_U16( targetSizeCam );
            bufferDescriptor._bufferParams._elementSize = sizeof( GFXShaderData::CamData );
            bufferDescriptor._initialData = { (Byte*)&_gpuBlock._camData, bufferDescriptor._bufferParams._elementSize };

            for ( U8 i = 0u; i < GFXBuffers::PER_FRAME_BUFFER_COUNT; ++i )
            {
                bufferDescriptor._name = Util::StringFormat( "DVD_GPU_CAM_DATA_%d", i );
                _gfxBuffers._perFrameBuffers[i]._camDataBuffer = newSB( bufferDescriptor );
                _gfxBuffers._perFrameBuffers[i]._camBufferWriteRange = {};
            }
        }

        if ( resizeCullCounter )
        {
            // Atomic counter for occlusion culling
            ShaderBufferDescriptor bufferDescriptor = {};
            bufferDescriptor._bufferParams._elementCount = 1;
            bufferDescriptor._ringBufferLength = to_U16( targetSizeCullCounter );
            bufferDescriptor._bufferParams._hostVisible = true;
            bufferDescriptor._bufferParams._elementSize = 4 * sizeof( U32 );
            bufferDescriptor._bufferParams._flags._usageType = BufferUsageType::UNBOUND_BUFFER;
            bufferDescriptor._bufferParams._flags._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
            bufferDescriptor._bufferParams._flags._updateUsage = BufferUpdateUsage::GPU_TO_CPU;
            bufferDescriptor._separateReadWrite = true;
            bufferDescriptor._initialData = { (bufferPtr)&VECTOR4_ZERO._v[0], 4 * sizeof( U32 ) };
            for ( U8 i = 0u; i < GFXBuffers::PER_FRAME_BUFFER_COUNT; ++i )
            {
                bufferDescriptor._name = Util::StringFormat( "CULL_COUNTER_%d", i );
                _gfxBuffers._perFrameBuffers[i]._cullCounter = newSB( bufferDescriptor );
            }
        }
        _gpuBlock._camNeedsUpload = true;
    }

    ErrorCode GFXDevice::postInitRenderingAPI( const vec2<U16> renderResolution )
    {
        std::atomic_uint loadTasks = 0;
        ResourceCache* cache = context().kernel().resourceCache();
        const Configuration& config = context().config();

        IMPrimitive::InitStaticData();
        ShaderProgram::InitStaticData();
        Texture::OnStartup( *this );
        RenderPassExecutor::OnStartup( *this );
        GFX::InitPools();

        resizeGPUBlocks( TargetBufferSizeCam, Config::MAX_FRAMES_IN_FLIGHT + 1u );

        _shaderComputeQueue = MemoryManager_NEW ShaderComputeQueue( cache );

        // Create general purpose render state blocks
        _defaultStateNoDepthTest._depthTestEnabled = false;

        _state2DRendering._cullMode = CullMode::NONE;
        _state2DRendering._depthTestEnabled = false;

        _stateDepthOnlyRendering._colourWrite.i = 0u;
        _stateDepthOnlyRendering._zFunc = ComparisonFunction::ALWAYS;

        // We need to create all of our attachments for the default render targets
        // Start with the screen render target: Try a half float, multisampled
        // buffer (MSAA + HDR rendering if possible)

        SamplerDescriptor defaultSampler = {};
        defaultSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        defaultSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        defaultSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        defaultSampler._minFilter = TextureFilter::NEAREST;
        defaultSampler._magFilter = TextureFilter::NEAREST;
        defaultSampler._mipSampling = TextureMipSampling::NONE;
        defaultSampler._anisotropyLevel = 0u;

        SamplerDescriptor defaultSamplerMips = {};
        defaultSamplerMips._wrapU = TextureWrap::CLAMP_TO_EDGE;
        defaultSamplerMips._wrapV = TextureWrap::CLAMP_TO_EDGE;
        defaultSamplerMips._wrapW = TextureWrap::CLAMP_TO_EDGE;
        defaultSamplerMips._anisotropyLevel = 0u;

        //PrePass
        TextureDescriptor depthDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_32, GFXImageFormat::RED, GFXImagePacking::DEPTH );
        TextureDescriptor velocityDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RG, GFXImagePacking::UNNORMALIZED );
        //RG - packed normal, B - roughness, A - unused
        TextureDescriptor normalsDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA, GFXImagePacking::UNNORMALIZED );
        depthDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );
        velocityDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );
        normalsDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

        //MainPass
        TextureDescriptor screenDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA, GFXImagePacking::UNNORMALIZED );
        screenDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );
        screenDescriptor.addImageUsageFlag(ImageUsage::SHADER_READ);
        TextureDescriptor materialDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RG, GFXImagePacking::UNNORMALIZED );
        materialDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

        // Normal, Previous and MSAA
        {
            InternalRTAttachmentDescriptors attachments
            {
                InternalRTAttachmentDescriptor{ screenDescriptor,   defaultSampler, RTAttachmentType::COLOUR, ScreenTargets::ALBEDO },
                InternalRTAttachmentDescriptor{ velocityDescriptor, defaultSampler, RTAttachmentType::COLOUR, ScreenTargets::VELOCITY },
                InternalRTAttachmentDescriptor{ normalsDescriptor,  defaultSampler, RTAttachmentType::COLOUR, ScreenTargets::NORMALS, false },
                InternalRTAttachmentDescriptor{ depthDescriptor,    defaultSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
            };

            RenderTargetDescriptor screenDesc = {};
            screenDesc._resolution = renderResolution;
            screenDesc._attachments = attachments;
            screenDesc._msaaSamples = config.rendering.MSAASamples;
            screenDesc._name = "Screen";
            RenderTargetNames::SCREEN = _rtPool->allocateRT( screenDesc )._targetID;

            auto& screenAttachment = attachments[to_base( ScreenTargets::ALBEDO )];
            screenAttachment._texDescriptor.mipMappingState( TextureDescriptor::MipMappingState::MANUAL );
            screenAttachment._texDescriptor.addImageUsageFlag(ImageUsage::SHADER_READ);
            screenAttachment._sampler = defaultSamplerMips;
            screenDesc._msaaSamples = 0u;
            screenDesc._name = "Screen Prev";
            screenDesc._attachments = { screenAttachment };
            RenderTargetNames::SCREEN_PREV = _rtPool->allocateRT( screenDesc )._targetID;

            auto& normalAttachment = attachments[to_base( ScreenTargets::NORMALS )];
            normalAttachment._slot = RTColourAttachmentSlot::SLOT_0;
            normalAttachment._texDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );
            normalAttachment._texDescriptor.addImageUsageFlag( ImageUsage::SHADER_READ );
            normalAttachment._sampler = defaultSamplerMips;
            screenDesc._msaaSamples = 0u;
            screenDesc._name = "Normals Resolved";
            screenDesc._attachments = { normalAttachment };
            RenderTargetNames::NORMALS_RESOLVED = _rtPool->allocateRT( screenDesc )._targetID;
        }
        {
            SamplerDescriptor samplerBackBuffer = {};
            samplerBackBuffer._wrapU = TextureWrap::CLAMP_TO_EDGE;
            samplerBackBuffer._wrapV = TextureWrap::CLAMP_TO_EDGE;
            samplerBackBuffer._wrapW = TextureWrap::CLAMP_TO_EDGE;
            samplerBackBuffer._minFilter = TextureFilter::LINEAR;
            samplerBackBuffer._magFilter = TextureFilter::LINEAR;
            samplerBackBuffer._mipSampling = TextureMipSampling::NONE;
            samplerBackBuffer._anisotropyLevel = 0u;

            // This could've been RGB, but Vulkan doesn't seem to support VK_FORMAT_R8G8B8_UNORM in this situation, so ... well ... whatever.
            // This will contained the final tonemaped image, so unless we desire HDR output, RGBA8 here is fine as anything else will require
            // changes to the swapchain images!
            TextureDescriptor backBufferDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA ); 
            backBufferDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );
            backBufferDescriptor.addImageUsageFlag( ImageUsage::SHADER_READ );
            InternalRTAttachmentDescriptors attachments
            {
                InternalRTAttachmentDescriptor{ backBufferDescriptor, samplerBackBuffer, RTAttachmentType::COLOUR, ScreenTargets::ALBEDO },
            };

            RenderTargetDescriptor screenDesc = {};
            screenDesc._resolution = renderResolution;
            screenDesc._attachments = attachments;
            screenDesc._msaaSamples = 0u;
            screenDesc._name = "BackBuffer";
            RenderTargetNames::BACK_BUFFER = _rtPool->allocateRT( screenDesc )._targetID;
        }
        {
            TextureDescriptor ssaoDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED, GFXImagePacking::UNNORMALIZED );
            ssaoDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            RenderTargetDescriptor ssaoDesc = {};
            ssaoDesc._attachments = 
            {
                InternalRTAttachmentDescriptor{ ssaoDescriptor, defaultSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0}
            };

            ssaoDesc._name = "SSAO Result";
            ssaoDesc._resolution = renderResolution;
            ssaoDesc._msaaSamples = 0u;
            RenderTargetNames::SSAO_RESULT = _rtPool->allocateRT( ssaoDesc )._targetID;
        }
        {
            TextureDescriptor ssrDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA, GFXImagePacking::UNNORMALIZED );
            ssrDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            RenderTargetDescriptor ssrResultDesc = {};
            ssrResultDesc._attachments = 
            {
                InternalRTAttachmentDescriptor{ ssrDescriptor, defaultSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
            };

            ssrResultDesc._name = "SSR Result";
            ssrResultDesc._resolution = renderResolution;
            ssrResultDesc._msaaSamples = 0u;
            RenderTargetNames::SSR_RESULT = _rtPool->allocateRT( ssrResultDesc )._targetID;

        }
        const U32 reflectRes = nextPOW2( CLAMPED( to_U32( config.rendering.reflectionPlaneResolution ), 16u, 4096u ) - 1u );

        TextureDescriptor hiZDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_32, GFXImageFormat::RED, GFXImagePacking::UNNORMALIZED );
        hiZDescriptor.mipMappingState( TextureDescriptor::MipMappingState::MANUAL );
        hiZDescriptor.addImageUsageFlag( ImageUsage::SHADER_WRITE );

        SamplerDescriptor hiZSampler = {};
        hiZSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        hiZSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        hiZSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        hiZSampler._anisotropyLevel = 0u;
        hiZSampler._magFilter = TextureFilter::NEAREST;
        hiZSampler._minFilter = TextureFilter::NEAREST;
        hiZSampler._mipSampling = TextureMipSampling::NONE;

        RenderTargetDescriptor hizRTDesc = {};
        hizRTDesc._attachments =
        {
            InternalRTAttachmentDescriptor{ hiZDescriptor, hiZSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
        };

        {
            hizRTDesc._name = "HiZ";
            hizRTDesc._resolution = renderResolution;
            RenderTargetNames::HI_Z = _rtPool->allocateRT( hizRTDesc )._targetID;

            hizRTDesc._resolution.set( reflectRes, reflectRes );
            hizRTDesc._name = "HiZ_Reflect";
            RenderTargetNames::HI_Z_REFLECT = _rtPool->allocateRT( hizRTDesc )._targetID;
        }

        // Reflection Targets
        SamplerDescriptor reflectionSampler = {};
        reflectionSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        reflectionSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        reflectionSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        reflectionSampler._minFilter = TextureFilter::LINEAR;
        reflectionSampler._magFilter = TextureFilter::LINEAR;
        reflectionSampler._mipSampling = TextureMipSampling::NONE;

        {
            TextureDescriptor environmentDescriptorPlanar( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA);
            TextureDescriptor depthDescriptorPlanar( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::RED, GFXImagePacking::DEPTH );

            environmentDescriptorPlanar.mipMappingState( TextureDescriptor::MipMappingState::MANUAL );
            depthDescriptorPlanar.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            {
                RenderTargetDescriptor refDesc = {};
                refDesc._attachments = 
                {
                    InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
                    InternalRTAttachmentDescriptor{ depthDescriptorPlanar,       reflectionSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
                };

                refDesc._resolution = vec2<U16>( reflectRes );

                for ( U32 i = 0; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i )
                {
                    refDesc._name = Util::StringFormat( "Reflection_Planar_%d", i ).c_str();
                    RenderTargetNames::REFLECTION_PLANAR[i] = _rtPool->allocateRT( refDesc )._targetID;
                }

                for ( U32 i = 0; i < Config::MAX_REFRACTIVE_NODES_IN_VIEW; ++i )
                {
                    refDesc._name = Util::StringFormat( "Refraction_Planar_%d", i ).c_str();
                    RenderTargetNames::REFRACTION_PLANAR[i] = _rtPool->allocateRT( refDesc )._targetID;
                }

                environmentDescriptorPlanar.mipMappingState( TextureDescriptor::MipMappingState::OFF );
                refDesc._attachments = 
                {//skip depth
                    InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
                };

                refDesc._name = "Reflection_blur";
                RenderTargetNames::REFLECTION_PLANAR_BLUR = _rtPool->allocateRT( refDesc )._targetID;
            }
        }
        {
            SamplerDescriptor accumulationSampler = {};
            accumulationSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
            accumulationSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
            accumulationSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
            accumulationSampler._minFilter = TextureFilter::NEAREST;
            accumulationSampler._magFilter = TextureFilter::NEAREST;
            accumulationSampler._mipSampling = TextureMipSampling::NONE;

            TextureDescriptor accumulationDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RGBA, GFXImagePacking::UNNORMALIZED );
            accumulationDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            //R = revealage
            TextureDescriptor revealageDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::FLOAT_16, GFXImageFormat::RED, GFXImagePacking::UNNORMALIZED );
            revealageDescriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            InternalRTAttachmentDescriptors oitAttachments
            {
                InternalRTAttachmentDescriptor{ accumulationDescriptor, accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::ACCUMULATION, false },
                InternalRTAttachmentDescriptor{ revealageDescriptor,    accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::REVEALAGE, false },
                InternalRTAttachmentDescriptor{ normalsDescriptor,      accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::NORMALS, false },
            };
            {
                const RenderTarget* screenTarget = _rtPool->getRenderTarget( RenderTargetNames::SCREEN );
                RTAttachment* screenAttachment = screenTarget->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::ALBEDO );
                RTAttachment* screenDepthAttachment = screenTarget->getAttachment( RTAttachmentType::DEPTH );

                ExternalRTAttachmentDescriptors externalAttachments
                {
                    ExternalRTAttachmentDescriptor{ screenAttachment,  screenAttachment->_descriptor._sampler, RTAttachmentType::COLOUR, ScreenTargets::MODULATE },
                    ExternalRTAttachmentDescriptor{ screenDepthAttachment, screenDepthAttachment->_descriptor._sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0, false }
                };

                RenderTargetDescriptor oitDesc = {};
                oitDesc._name = "OIT";
                oitDesc._resolution = renderResolution;
                oitDesc._attachments = oitAttachments;
                oitDesc._externalAttachments = externalAttachments;
                oitDesc._msaaSamples = config.rendering.MSAASamples;
                RenderTargetNames::OIT = _rtPool->allocateRT( oitDesc )._targetID;
            }
            {
                for ( U16 i = 0u; i < Config::MAX_REFLECTIVE_NODES_IN_VIEW; ++i )
                {
                    const RenderTarget* reflectTarget = _rtPool->getRenderTarget( RenderTargetNames::REFLECTION_PLANAR[i] );
                    RTAttachment* screenAttachment = reflectTarget->getAttachment( RTAttachmentType::COLOUR );
                    RTAttachment* depthAttachment = reflectTarget->getAttachment( RTAttachmentType::DEPTH );

                    RenderTargetDescriptor oitDesc = {};
                    oitDesc._attachments = oitAttachments;

                    ExternalRTAttachmentDescriptors externalAttachments{
                        ExternalRTAttachmentDescriptor{ screenAttachment, screenAttachment->_descriptor._sampler, RTAttachmentType::COLOUR, ScreenTargets::MODULATE },
                        ExternalRTAttachmentDescriptor{ depthAttachment, depthAttachment->_descriptor._sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
                    };

                    oitDesc._name = Util::StringFormat( "OIT_REFLECT_RES_%d", i ).c_str();
                    oitDesc._resolution = vec2<U16>( reflectRes );
                    oitDesc._externalAttachments = externalAttachments;
                    oitDesc._msaaSamples = 0;
                    RenderTargetNames::OIT_REFLECT = _rtPool->allocateRT( oitDesc )._targetID;
                }
            }
        }
        {
            TextureDescriptor environmentDescriptorCube( TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA);
            TextureDescriptor depthDescriptorCube( TextureType::TEXTURE_CUBE_ARRAY, GFXDataFormat::UNSIGNED_INT, GFXImageFormat::RED, GFXImagePacking::DEPTH );

            environmentDescriptorCube.mipMappingState( TextureDescriptor::MipMappingState::OFF );
            depthDescriptorCube.mipMappingState( TextureDescriptor::MipMappingState::OFF );

            RenderTargetDescriptor refDesc = {};
            refDesc._attachments = 
            {
                InternalRTAttachmentDescriptor{ environmentDescriptorCube, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
                InternalRTAttachmentDescriptor{ depthDescriptorCube,       reflectionSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
            };

            refDesc._resolution = vec2<U16>( reflectRes );

            refDesc._name = "Reflection_Cube_Array";
            RenderTargetNames::REFLECTION_CUBE = _rtPool->allocateRT( refDesc )._targetID;
        }
        {
            ShaderModuleDescriptor compModule = {};
            compModule._moduleType = ShaderType::COMPUTE;
            compModule._defines.emplace_back( Util::StringFormat( "LOCAL_SIZE %d", DEPTH_REDUCE_LOCAL_SIZE ) );
            compModule._defines.emplace_back( "imageSizeIn PushData0[0].xy");
            compModule._defines.emplace_back( "imageSizeOut PushData0[0].zw");
            compModule._defines.emplace_back( "wasEven (uint(PushData0[1].x) == 1u)");

            compModule._sourceFile = "HiZConstruct.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( compModule );

            // Initialized our HierarchicalZ construction shader (takes a depth attachment and down-samples it for every mip level)
            ResourceDescriptor descriptor1( "HiZConstruct" );
            descriptor1.waitForReady( false );
            descriptor1.propertyDescriptor( shaderDescriptor );
            _hIZConstructProgram = CreateResource<ShaderProgram>( cache, descriptor1, loadTasks );
            _hIZConstructProgram->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* )
                                                    {
                                                        PipelineDescriptor pipelineDesc{};
                                                        pipelineDesc._shaderProgramHandle = _hIZConstructProgram->handle();
                                                        pipelineDesc._primitiveTopology = PrimitiveTopology::COMPUTE;
                                                        pipelineDesc._stateBlock = getNoDepthTestBlock();

                                                        _hIZPipeline = newPipeline( pipelineDesc );
                                                    } );
        }
        {
            ShaderModuleDescriptor compModule = {};
            compModule._moduleType = ShaderType::COMPUTE;
            compModule._defines.emplace_back( Util::StringFormat( "WORK_GROUP_SIZE %d", GROUP_SIZE_AABB ) );
            compModule._sourceFile = "HiZOcclusionCull.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( compModule );

            ResourceDescriptor descriptor2( "HiZOcclusionCull" );
            descriptor2.waitForReady( false );
            descriptor2.propertyDescriptor( shaderDescriptor );
            _hIZCullProgram = CreateResource<ShaderProgram>( cache, descriptor2, loadTasks );
            _hIZCullProgram->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* )
                                               {
                                                   PipelineDescriptor pipelineDescriptor = {};
                                                   pipelineDescriptor._shaderProgramHandle = _hIZCullProgram->handle();
                                                   pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
                                                   _hIZCullPipeline = newPipeline( pipelineDescriptor );
                                               } );
        }
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "baseVertexShaders.glsl";
            vertModule._variant = "FullScreenQuad";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "fbPreview.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            ResourceDescriptor previewRTShader( "fbPreview" );
            previewRTShader.waitForReady( true );
            previewRTShader.propertyDescriptor( shaderDescriptor );
            _renderTargetDraw = CreateResource<ShaderProgram>( cache, previewRTShader, loadTasks );
            _renderTargetDraw->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* ) noexcept
                                                 {
                                                     _previewRenderTargetColour = _renderTargetDraw;
                                                 } );
        }
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "baseVertexShaders.glsl";
            vertModule._variant = "FullScreenQuad";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "fbPreview.glsl";
            fragModule._variant = "LinearDepth.ScenePlanes";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            ResourceDescriptor previewReflectionRefractionDepth( "fbPreviewLinearDepthScenePlanes" );
            previewReflectionRefractionDepth.waitForReady( false );
            previewReflectionRefractionDepth.propertyDescriptor( shaderDescriptor );
            _previewRenderTargetDepth = CreateResource<ShaderProgram>( cache, previewReflectionRefractionDepth, loadTasks );
        }
        ShaderModuleDescriptor blurVertModule = {};
        blurVertModule._moduleType = ShaderType::VERTEX;
        blurVertModule._sourceFile = "baseVertexShaders.glsl";
        blurVertModule._variant = "FullScreenQuad";
        {
            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "blur.glsl";
            fragModule._variant = "Generic";
            fragModule._defines.emplace_back("layer uint(PushData0[0].x)" );
            fragModule._defines.emplace_back("size PushData0[0].yz");
            fragModule._defines.emplace_back("kernelSize int(PushData0[0].w)");
            fragModule._defines.emplace_back("verticalBlur uint(PushData0[1].x)");
            {
                ShaderProgramDescriptor shaderDescriptorSingle = {};
                shaderDescriptorSingle._modules.push_back( blurVertModule );
                shaderDescriptorSingle._modules.push_back( fragModule );

                ResourceDescriptor blur( "BoxBlur_Single" );
                blur.propertyDescriptor( shaderDescriptorSingle );
                _blurBoxShaderSingle = CreateResource<ShaderProgram>( cache, blur, loadTasks );
                _blurBoxShaderSingle->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* res )
                                                        {
                                                            const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                                                            PipelineDescriptor pipelineDescriptor;
                                                            pipelineDescriptor._stateBlock = get2DStateBlock();
                                                            pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                                                            pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                                                            _blurBoxPipelineSingleCmd._pipeline = newPipeline( pipelineDescriptor );
                                                        } );
            }
            {
                ShaderProgramDescriptor shaderDescriptorLayered = {};
                shaderDescriptorLayered._modules.push_back( blurVertModule );
                shaderDescriptorLayered._modules.push_back( fragModule );
                shaderDescriptorLayered._modules.back()._variant += ".Layered";
                shaderDescriptorLayered._modules.back()._defines.emplace_back( "LAYERED" );

                ResourceDescriptor blur( "BoxBlur_Layered" );
                blur.propertyDescriptor( shaderDescriptorLayered );
                _blurBoxShaderLayered = CreateResource<ShaderProgram>( cache, blur, loadTasks );
                _blurBoxShaderLayered->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* res )
                                                         {
                                                             const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                                                             PipelineDescriptor pipelineDescriptor;
                                                             pipelineDescriptor._stateBlock = get2DStateBlock();
                                                             pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                                                             pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                                                             _blurBoxPipelineLayeredCmd._pipeline = newPipeline( pipelineDescriptor );
                                                         } );
            }
        }
        {
            {
                ShaderModuleDescriptor geomModule = {};
                geomModule._moduleType = ShaderType::GEOMETRY;
                geomModule._sourceFile = "blur.glsl";
                geomModule._variant = "GaussBlur";

                geomModule._defines.emplace_back( "verticalBlur uint(PushData0[1].x)" );
                geomModule._defines.emplace_back( "layerCount int(PushData0[1].y)" );
                geomModule._defines.emplace_back( "layerOffsetRead int(PushData0[1].z)" );
                geomModule._defines.emplace_back( "layerOffsetWrite int(PushData0[1].w)" );

                ShaderModuleDescriptor fragModule = {};
                fragModule._moduleType = ShaderType::FRAGMENT;
                fragModule._sourceFile = "blur.glsl";
                fragModule._variant = "GaussBlur";

                {
                    ShaderProgramDescriptor shaderDescriptorSingle = {};
                    shaderDescriptorSingle._modules.push_back( blurVertModule );
                    shaderDescriptorSingle._modules.push_back( geomModule );
                    shaderDescriptorSingle._modules.push_back( fragModule );
                    shaderDescriptorSingle._globalDefines.emplace_back( "GS_MAX_INVOCATIONS 1" );

                    ResourceDescriptor blur( "GaussBlur_Single" );
                    blur.propertyDescriptor( shaderDescriptorSingle );
                    _blurGaussianShaderSingle = CreateResource<ShaderProgram>( cache, blur, loadTasks );
                    _blurGaussianShaderSingle->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* res )
                                                                 {
                                                                     const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                                                                     PipelineDescriptor pipelineDescriptor;
                                                                     pipelineDescriptor._stateBlock = get2DStateBlock();
                                                                     pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                                                                     pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;
                                                                     _blurGaussianPipelineSingleCmd._pipeline = newPipeline( pipelineDescriptor );
                                                                 } );
                }
                {
                    ShaderProgramDescriptor shaderDescriptorLayered = {};
                    shaderDescriptorLayered._modules.push_back( blurVertModule );
                    shaderDescriptorLayered._modules.push_back( geomModule );
                    shaderDescriptorLayered._modules.push_back( fragModule );
                    shaderDescriptorLayered._modules.back()._variant += ".Layered";
                    shaderDescriptorLayered._modules.back()._defines.emplace_back( "LAYERED" );
                    shaderDescriptorLayered._globalDefines.emplace_back( Util::StringFormat( "GS_MAX_INVOCATIONS %d", MAX_INVOCATIONS_BLUR_SHADER_LAYERED ) );

                    ResourceDescriptor blur( "GaussBlur_Layered" );
                    blur.propertyDescriptor( shaderDescriptorLayered );
                    _blurGaussianShaderLayered = CreateResource<ShaderProgram>( cache, blur, loadTasks );
                    _blurGaussianShaderLayered->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* res )
                                                                  {
                                                                      const ShaderProgram* blurShader = static_cast<ShaderProgram*>(res);
                                                                      PipelineDescriptor pipelineDescriptor;
                                                                      pipelineDescriptor._stateBlock = get2DStateBlock();
                                                                      pipelineDescriptor._shaderProgramHandle = blurShader->handle();
                                                                      pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

                                                                      _blurGaussianPipelineLayeredCmd._pipeline = newPipeline( pipelineDescriptor );
                                                                  } );
                }
            }
            // Create an immediate mode rendering shader that simulates the fixed function pipeline
            _imShaders = eastl::make_unique<ImShaders>(*this);
        }
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "baseVertexShaders.glsl";
            vertModule._variant = "FullScreenQuad";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "display.glsl";
            fragModule._defines.emplace_back( "convertToSRGB uint(PushData0[0].x)" );

            ResourceDescriptor descriptor3( "display" );
            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );
            descriptor3.propertyDescriptor( shaderDescriptor );
            {
                _displayShader = CreateResource<ShaderProgram>( cache, descriptor3, loadTasks );
                _displayShader->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* )
                                                  {
                                                      PipelineDescriptor pipelineDescriptor = {};
                                                      pipelineDescriptor._stateBlock = get2DStateBlock();
                                                      pipelineDescriptor._shaderProgramHandle = _displayShader->handle();
                                                      pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                                                      _drawFSTexturePipelineCmd._pipeline = newPipeline( pipelineDescriptor );

                                                      BlendingSettings& blendState = pipelineDescriptor._blendStates._settings[0];
                                                      blendState.enabled( true );
                                                      blendState.blendSrc( BlendProperty::SRC_ALPHA );
                                                      blendState.blendDest( BlendProperty::INV_SRC_ALPHA );
                                                      blendState.blendOp( BlendOperation::ADD );
                                                      _drawFSTexturePipelineBlendCmd._pipeline = newPipeline( pipelineDescriptor );
                                                  } );
            }
            {
                shaderDescriptor._modules.back()._defines.emplace_back( "DEPTH_ONLY" );
                descriptor3.propertyDescriptor( shaderDescriptor );
                _depthShader = CreateResource<ShaderProgram>( cache, descriptor3, loadTasks );
                _depthShader->addStateCallback( ResourceState::RES_LOADED, [this]( CachedResource* )
                                                {
                                                    PipelineDescriptor pipelineDescriptor = {};
                                                    pipelineDescriptor._stateBlock = _stateDepthOnlyRendering;
                                                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                                                    pipelineDescriptor._shaderProgramHandle = _depthShader->handle();

                                                    _drawFSDepthPipelineCmd._pipeline = newPipeline( pipelineDescriptor );
                                                } );
            }
        }

        context().paramHandler().setParam<bool>( _ID( "rendering.previewDebugViews" ), false );
        {
            // Create general purpose render state blocks
            RenderStateBlock primitiveStateBlock{};

            PipelineDescriptor pipelineDesc;
            pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;
            pipelineDesc._shaderProgramHandle = _imShaders->imWorldShaderNoTexture()->handle();
            pipelineDesc._stateBlock = primitiveStateBlock;

            _debugGizmoPipeline = pipelineDesc;

            primitiveStateBlock._depthTestEnabled = false;
            pipelineDesc._stateBlock = primitiveStateBlock;
            _debugGizmoPipelineNoDepth = pipelineDesc;


            primitiveStateBlock._cullMode = CullMode::NONE;
            pipelineDesc._stateBlock = primitiveStateBlock;
            _debugGizmoPipelineNoCullNoDepth = pipelineDesc;


            primitiveStateBlock._depthTestEnabled = true;
            pipelineDesc._stateBlock = primitiveStateBlock;
            _debugGizmoPipelineNoCull = pipelineDesc;
        }

        _renderer = eastl::make_unique<Renderer>( context(), cache );

        WAIT_FOR_CONDITION( loadTasks.load() == 0 );
        const DisplayWindow* mainWindow = context().app().windowManager().mainWindow();

        SizeChangeParams params{};
        params.width = _rtPool->getRenderTarget( RenderTargetNames::SCREEN )->getWidth();
        params.height = _rtPool->getRenderTarget( RenderTargetNames::SCREEN )->getHeight();
        params.winGUID = mainWindow->getGUID();
        params.isMainWindow = true;
        if ( context().app().onResolutionChange( params ) )
        {
            NOP();
        }

        _sceneData = MemoryManager_NEW SceneShaderData( *this );

        // Everything is ready from the rendering point of view
        return ErrorCode::NO_ERR;
    }

    /// Revert everything that was set up in initRenderingAPI()
    void GFXDevice::closeRenderingAPI()
    {
        if ( _api == nullptr )
        {
            //closeRenderingAPI called without init!
            return;
        }

        _debugLines.reset();
        _debugBoxes.reset();
        _debugOBBs.reset();
        _debugFrustums.reset();
        _debugCones.reset();
        _debugSpheres.reset();
        _debugViews.clear();

        // Delete the renderer implementation
        Console::printfn( LOCALE_STR( "CLOSING_RENDERER" ) );
        _renderer.reset( nullptr );

        GFX::DestroyPools();
        MemoryManager::SAFE_DELETE( _rtPool );
        _previewDepthMapShader = nullptr;
        _previewRenderTargetColour = nullptr;
        _previewRenderTargetDepth = nullptr;
        _renderTargetDraw = nullptr;
        _hIZConstructProgram = nullptr;
        _hIZCullProgram = nullptr;
        _displayShader = nullptr;
        _depthShader = nullptr;
        _blurBoxShaderSingle = nullptr;
        _blurBoxShaderLayered = nullptr;
        _blurGaussianShaderSingle = nullptr;
        _blurGaussianShaderLayered = nullptr;
        _imShaders.reset();
        _gfxBuffers.reset( true, true );
        MemoryManager::SAFE_DELETE( _sceneData );
        // Close the shader manager
        MemoryManager::DELETE( _shaderComputeQueue );
        if ( !ShaderProgram::OnShutdown() )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        RenderPassExecutor::OnShutdown( *this );
        Texture::OnShutdown();
        ShaderProgram::DestroyStaticData();
        assert( ShaderProgram::ShaderProgramCount() == 0 );
        // Close the rendering API
        _api->closeRenderingAPI();
        _api.reset();

        LockGuard<Mutex> lock( _graphicsResourceMutex );
        if ( !_graphicResources.empty() )
        {
            string list = " [ ";
            for ( const std::tuple<GraphicsResource::Type, I64, U64>& res : _graphicResources )
            {
                list.append( TypeUtil::GraphicResourceTypeToName( std::get<0>( res ) ) );
                list.append( "_" );
                list.append( Util::to_string( std::get<1>( res ) ) );
                list.append( "_" );
                list.append( Util::to_string( std::get<2>( res ) ) );
                list.append( "," );
            }
            list.pop_back();
            list += " ]";
            Console::errorfn( LOCALE_STR( "ERROR_GFX_LEAKED_RESOURCES" ), _graphicResources.size() );
            Console::errorfn( list.c_str() );
        }
        _graphicResources.clear();
    }

    void GFXDevice::onThreadCreated( const std::thread::id& threadID, const bool isMainRenderThread ) const
    {
        _api->onThreadCreated( threadID, isMainRenderThread );

        if ( !ShaderProgram::OnThreadCreated( *this, threadID ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

#pragma endregion

#pragma region Main frame loop
    /// After a swap buffer call, the CPU may be idle waiting for the GPU to draw to the screen, so we try to do some processing
    void GFXDevice::idle( const bool fast, const U64 deltaTimeUSGame, [[maybe_unused]] const U64 deltaTimeUSApp )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics );

        _api->idle( fast );

        _shaderComputeQueue->idle();
        // Pass the idle call to the post processing system
        _renderer->idle( deltaTimeUSGame );
        // And to the shader manager
        ShaderProgram::Idle( context(), fast );
    }

    void GFXDevice::update( const U64 deltaTimeUSFixed, const U64 deltaTimeUSApp )
    {
        getRenderer().postFX().update( deltaTimeUSFixed, deltaTimeUSApp );
    }

    void GFXDevice::drawToWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        if ( _resolutionChangeQueued.second )
        {
            SizeChangeParams params{};
            params.isFullScreen = window.fullscreen();
            params.width = _resolutionChangeQueued.first.width;
            params.height = _resolutionChangeQueued.first.height;
            params.winGUID = context().mainWindow().getGUID();
            params.isMainWindow = window.getGUID() == context().mainWindow().getGUID();

            if ( context().app().onResolutionChange( params ) )
            {
                NOP();
            }

            _resolutionChangeQueued.second = false;
        }

        if ( !_api->drawToWindow( window ) )
        {
            NOP();
        }
        
        _activeWindow = &window;
        const vec2<U16> windowDimensions = window.getDrawableSize();
        setViewport( { 0, 0, windowDimensions.width, windowDimensions.height } );
        setScissor( { 0, 0, windowDimensions.width, windowDimensions.height } );

    }

    void GFXDevice::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        {
            LockGuard<Mutex> w_lock( _queuedCommandbufferLock );
            GFX::CommandBuffer& buffer = *window.getCurrentCommandBuffer();
            flushCommandBufferInternal(buffer);
            buffer.clear();
        }

        _api->flushWindow( window, false );
        _activeWindow = nullptr;
    }

    bool GFXDevice::framePreRender( [[maybe_unused]] const FrameEvent& evt )
    {
        return true;
    }

    bool GFXDevice::frameStarted( [[maybe_unused]] const FrameEvent& evt )
    {
        for ( GFXDescriptorSet& set : _descriptorSets )
        {
            set.clear();
        }

        if ( _queuedScreenSampleChange != s_invalidQueueSampleCount )
        {
            setScreenMSAASampleCountInternal( _queuedScreenSampleChange );
            _queuedScreenSampleChange = s_invalidQueueSampleCount;
        }
        for ( U8 i = 0u; i < to_base( ShadowType::COUNT ); ++i )
        {
            if ( _queuedShadowSampleChange[i] != s_invalidQueueSampleCount )
            {
                setShadowMSAASampleCountInternal( static_cast<ShadowType>(i), _queuedShadowSampleChange[i] );
                _queuedShadowSampleChange[i] = s_invalidQueueSampleCount;
            }
        }

        if ( _api->frameStarted() )
        {
            drawToWindow(context().mainWindow());
            return true;
        }

        return false;
    }

    bool GFXDevice::frameEnded( [[maybe_unused]] const FrameEvent& evt ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool editorRunning = Config::Build::ENABLE_EDITOR && context().editor().running();
        if ( !editorRunning )
        {
            PROFILE_SCOPE("Blit Backbuffer", Profiler::Category::Graphics);

            GFX::ScopedCommandBuffer sBuffer = GFX::AllocateScopedCommandBuffer();
            GFX::CommandBuffer& buffer = sBuffer();

            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = SCREEN_TARGET_ID;
            beginRenderPassCmd._name = "Blit Backbuffer";
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLACK, true };
            beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
            GFX::EnqueueCommand( buffer, beginRenderPassCmd );

            const auto& screenAtt = renderTargetPool().getRenderTarget( RenderTargetNames::BACK_BUFFER )->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO );
            const auto& texData = screenAtt->texture()->getView();

            drawTextureInViewport( texData, screenAtt->_descriptor._sampler, context().mainWindow().renderingViewport(), false, false, false, buffer );

            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( buffer );
            flushCommandBuffer( buffer );
        }

        flushWindow( context().mainWindow() );

        {
            PROFILE_SCOPE( "Lifetime updates", Profiler::Category::Graphics );

            frameDrawCallsPrev( frameDrawCalls() );
            frameDrawCalls( 0u );

            DecrementPrimitiveLifetime( _debugLines );
            DecrementPrimitiveLifetime( _debugBoxes );
            DecrementPrimitiveLifetime( _debugOBBs );
            DecrementPrimitiveLifetime( _debugFrustums );
            DecrementPrimitiveLifetime( _debugCones );
            DecrementPrimitiveLifetime( _debugSpheres );

            _performanceMetrics._scratchBufferQueueUsage[0] = to_U32( _gfxBuffers.crtBuffers()._camWritesThisFrame );
            _performanceMetrics._scratchBufferQueueUsage[1] = to_U32( _gfxBuffers.crtBuffers()._renderWritesThisFrame );
        }

        if ( _gfxBuffers._needsResizeCam )
        {
            PROFILE_SCOPE( "Resize GFX Blocks", Profiler::Category::Graphics );

            const GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
            const U32 currentSizeCam = frameBuffers._camDataBuffer->queueLength();
            const U32 currentSizeCullCounter = frameBuffers._cullCounter->queueLength();
            resizeGPUBlocks( _gfxBuffers._needsResizeCam ? currentSizeCam + TargetBufferSizeCam : currentSizeCam, currentSizeCullCounter );
            _gfxBuffers._needsResizeCam = false;
        }

        _gfxBuffers.onEndFrame();
        ShaderProgram::OnEndFrame( *this );
        
        const bool ret = _api->frameEnded();
        ++s_frameCount;
        return ret;
    }
#pragma endregion

#pragma region Utility functions
    /// Generate a cube texture and store it in the provided RenderTarget
    void GFXDevice::generateCubeMap( RenderPassParams& params,
                                     const U16 arrayOffset,
                                     const vec3<F32>& pos,
                                     const vec2<F32> zPlanes,
                                     GFX::CommandBuffer& commandsInOut,
                                     GFX::MemoryBarrierCommand& memCmdInOut,
                                     mat4<F32>* viewProjectionOut)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        // Only the first colour attachment or the depth attachment is used for now and it must be a cube map texture
        RenderTarget* cubeMapTarget = _rtPool->getRenderTarget( params._target );

        // Colour attachment takes precedent over depth attachment
        bool isValidFB = false;
        if ( cubeMapTarget->hasAttachment( RTAttachmentType::COLOUR ) )
        {
            // We only need the colour attachment
            isValidFB = IsCubeTexture( cubeMapTarget->getAttachment( RTAttachmentType::COLOUR )->texture()->descriptor().texType() );
        }
        else if ( cubeMapTarget->hasAttachment( RTAttachmentType::DEPTH ) )
        {
            // We don't have a colour attachment, so we require a cube map depth attachment
            isValidFB = IsCubeTexture( cubeMapTarget->getAttachment( RTAttachmentType::DEPTH )->texture()->descriptor().texType() );
        }

        // Make sure we have a proper render target to draw to
        if ( !isValidFB )
        {
            // Future formats must be added later (e.g. cube map arrays)
            Console::errorfn( LOCALE_STR( "ERROR_GFX_DEVICE_INVALID_FB_CUBEMAP" ) );
            return;
        }

        static const vec3<F32> CameraDirections[] = { WORLD_X_AXIS,  WORLD_X_NEG_AXIS, WORLD_Y_AXIS,  WORLD_Y_NEG_AXIS,  WORLD_Z_AXIS,  WORLD_Z_NEG_AXIS };
        static const vec3<F32> CameraUpVectors[] =  { WORLD_Y_AXIS,  WORLD_Y_AXIS,     WORLD_Z_AXIS,  WORLD_Z_NEG_AXIS,  WORLD_Y_AXIS,  WORLD_Y_AXIS     };
        constexpr const char* PassNames[] =         { "CUBEMAP_X+",  "CUBEMAP_X-",     "CUBEMAP_Y+",  "CUBEMAP_Y-",      "CUBEMAP_Z+",  "CUBEMAP_Z-"     };

        DIVIDE_ASSERT( cubeMapTarget->getWidth() == cubeMapTarget->getHeight());

        RenderPassManager* passMgr = context().kernel().renderPassManager();

        Camera* camera = Camera::GetUtilityCamera( Camera::UtilityCamera::CUBE );

        // For each of the environment's faces (TOP, DOWN, NORTH, SOUTH, EAST, WEST)
        for ( U8 i = 0u; i < 6u; ++i )
        {
            params._passName = PassNames[i];
            params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);

            const DrawLayerEntry layer = { arrayOffset, i};

            // Draw to the current cubemap face
            params._targetDescriptorPrePass._writeLayers[RT_DEPTH_ATTACHMENT_IDX] = layer;
            params._targetDescriptorPrePass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )] = layer;
            params._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX] = layer;
            params._targetDescriptorMainPass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )] = layer;

            // Set a 90 degree horizontal FoV perspective projection
            camera->setProjection( 1.f, Angle::to_VerticalFoV( Angle::DEGREES<F32>( 90.f ), 1.f ), zPlanes );
            // Point our camera to the correct face
            camera->lookAt( pos, pos + (CameraDirections[i] * zPlanes.max), CameraUpVectors[i] );
            // Pass our render function to the renderer
            passMgr->doCustomPass( camera, params, commandsInOut, memCmdInOut );

            if ( viewProjectionOut != nullptr )
            {
                viewProjectionOut[i] = camera->viewProjectionMatrix();
            }
        }
    }

    void GFXDevice::generateDualParaboloidMap( RenderPassParams& params,
                                               const U16 arrayOffset,
                                               const vec3<F32>& pos,
                                               const vec2<F32> zPlanes,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut,
                                               mat4<F32>* viewProjectionOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( arrayOffset < 0 )
        {
            return;
        }

        RenderTarget* paraboloidTarget = _rtPool->getRenderTarget( params._target );
        // Colour attachment takes precedent over depth attachment
        const bool hasColour = paraboloidTarget->hasAttachment( RTAttachmentType::COLOUR );
        const bool hasDepth = paraboloidTarget->hasAttachment( RTAttachmentType::DEPTH );
        const vec2<U16> targetResolution = paraboloidTarget->getResolution();

        bool isValidFB = false;
        if ( hasColour )
        {
            RTAttachment* colourAttachment = paraboloidTarget->getAttachment( RTAttachmentType::COLOUR );
            // We only need the colour attachment
            isValidFB = IsArrayTexture( colourAttachment->texture()->descriptor().texType() );
        }
        else
        {
            RTAttachment* depthAttachment = paraboloidTarget->getAttachment( RTAttachmentType::DEPTH );
            // We don't have a colour attachment, so we require a cube map depth attachment
            isValidFB = hasDepth && IsArrayTexture( depthAttachment->texture()->descriptor().texType() );
        }
        // Make sure we have a proper render target to draw to
        if ( !isValidFB )
        {
            // Future formats must be added later (e.g. cube map arrays)
            Console::errorfn( LOCALE_STR( "ERROR_GFX_DEVICE_INVALID_FB_DP" ) );
            return;
        }

        params._passName = "DualParaboloid";
        const D64 aspect = to_D64( targetResolution.width ) / targetResolution.height;
        RenderPassManager* passMgr = context().kernel().renderPassManager();

        Camera* camera = Camera::GetUtilityCamera( Camera::UtilityCamera::DUAL_PARABOLOID );

        for ( U8 i = 0u; i < 2u; ++i )
        {
            const U16 layer = arrayOffset + i;

            params._targetDescriptorPrePass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer = layer;
            params._targetDescriptorPrePass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )]._layer = layer;
            params._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer = layer;
            params._targetDescriptorMainPass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )]._layer = layer;

            // Point our camera to the correct face
            camera->lookAt( pos, pos + (i == 0 ? WORLD_Z_NEG_AXIS : WORLD_Z_AXIS) * zPlanes.y );
            // Set a 180 degree vertical FoV perspective projection
            camera->setProjection( to_F32( aspect ), Angle::to_VerticalFoV( Angle::DEGREES<F32>( 180.0f ), aspect ), zPlanes );
            // And generated required matrices
            // Pass our render function to the renderer
            params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);

            passMgr->doCustomPass( camera, params, bufferInOut, memCmdInOut );

            if ( viewProjectionOut != nullptr )
            {
                viewProjectionOut[i] = camera->viewProjectionMatrix();
            }
        }
    }

    void GFXDevice::blurTarget( RenderTargetHandle& blurSource,
                                RenderTargetHandle& blurBuffer,
                                const RenderTargetHandle& blurTarget,
                                const RTAttachmentType att,
                                const RTColourAttachmentSlot slot,
                                const I32 kernelSize,
                                const bool gaussian,
                                const U8 layerCount,
                                GFX::CommandBuffer& bufferInOut )
    {
        const auto& inputAttachment = blurSource._rt->getAttachment( att, slot );
        const auto& bufferAttachment = blurBuffer._rt->getAttachment( att, slot );

        PushConstantsStruct pushData{};
        pushData.data[0]._vec[0].w = to_F32( kernelSize );
        pushData.data[0]._vec[1].y = to_F32( layerCount );
        pushData.data[0]._vec[1].z = 0.f;
        pushData.data[0]._vec[1].w = 0.f;

        const U8 loopCount = gaussian ? 1u : layerCount;

        {// Blur horizontally
            pushData.data[0]._vec[0].x = 0.f;
            pushData.data[0]._vec[1].x = 0.f;
            pushData.data[0]._vec[0].yz = vec2<F32>( blurBuffer._rt->getResolution() );
            pushData.data[0]._vec[2].xy.set( 1.f / blurBuffer._rt->getResolution().width, 1.f / blurBuffer._rt->getResolution().height );

            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            renderPassCmd->_target = blurBuffer._targetID;
            renderPassCmd->_name = "BLUR_RENDER_TARGET_HORIZONTAL";
            renderPassCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
            renderPassCmd->_clearDescriptor[to_base(RTColourAttachmentSlot::SLOT_0)] = DEFAULT_CLEAR_ENTRY;
            renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

            GFX::EnqueueCommand( bufferInOut, gaussian ? (layerCount > 1 ? _blurGaussianPipelineLayeredCmd : _blurGaussianPipelineSingleCmd)
                                 : (layerCount > 1 ? _blurBoxPipelineLayeredCmd : _blurBoxPipelineSingleCmd) );

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, inputAttachment->texture()->getView(), inputAttachment->_descriptor._sampler );

    
            if ( !gaussian && layerCount > 1 )
            {
                pushData.data[0]._vec[0].x = 0.f;
            }

            for ( U8 loop = 0u; loop < loopCount; ++loop )
            {
                if ( !gaussian && loop > 0u )
                {
                    pushData.data[0]._vec[0].x = to_F32( loop );
                    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut)->_constants.set( pushData );
                }
                GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );
            }

            GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        }
        {// Blur vertically
            pushData.data[0]._vec[0].x = 0.f;
            pushData.data[0]._vec[1].x = 1.f;
            pushData.data[0]._vec[0].yz = vec2<F32>( blurTarget._rt->getResolution() );
            pushData.data[0]._vec[2].xy.set( 1.0f / blurTarget._rt->getResolution().width, 1.0f / blurTarget._rt->getResolution().height );

            GFX::BeginRenderPassCommand* renderPassCmd = GFX::EnqueueCommand<GFX::BeginRenderPassCommand>( bufferInOut );
            renderPassCmd->_target = blurTarget._targetID;
            renderPassCmd->_name = "BLUR_RENDER_TARGET_VERTICAL";
            renderPassCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
            renderPassCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;
            renderPassCmd->_descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, bufferAttachment->texture()->getView(), bufferAttachment->_descriptor._sampler );

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( pushData );

            for ( U8 loop = 0u; loop < loopCount; ++loop )
            {
                if ( !gaussian && loop > 0u )
                {
                    pushData.data[0]._vec[0].x = to_F32( loop );
                    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants.set( pushData );
                }
                GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );
            }

            GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        }
    }
#pragma endregion

#pragma region Resolution, viewport and window management
    void GFXDevice::increaseResolution()
    {
        stepResolution( true );
    }

    void GFXDevice::decreaseResolution()
    {
        stepResolution( false );
    }

    void GFXDevice::stepResolution( const bool increment ) 
    {
        const auto compare = []( const vec2<U16> a, const vec2<U16> b ) noexcept -> bool
        {
            return a.x > b.x || a.y > b.y;
        };

        const WindowManager& winManager = context().app().windowManager();

        const auto& displayModes = DisplayManager::GetDisplayModes( winManager.mainWindow()->currentDisplayIndex() );

        bool found = false;
        vec2<U16> foundRes;
        if ( increment )
        {
            for ( auto it = displayModes.rbegin(); it != displayModes.rend(); ++it )
            {
                const vec2<U16> res = it->_resolution;
                if ( compare( res, _renderingResolution ) )
                {
                    found = true;
                    foundRes.set( res );
                    break;
                }
            }
        }
        else
        {
            for ( const auto& mode : displayModes )
            {
                const vec2<U16> res = mode._resolution;
                if ( compare( _renderingResolution, res ) )
                {
                    found = true;
                    foundRes.set( res );
                    break;
                }
            }
        }

        if ( found )
        {
            _resolutionChangeQueued.first.set( foundRes );
            _resolutionChangeQueued.second = true;
        }
    }

    void GFXDevice::toggleFullScreen() const
    {
        const WindowManager& winManager = context().app().windowManager();

        switch ( winManager.mainWindow()->type() )
        {
            case WindowType::WINDOW:
                winManager.mainWindow()->changeType( WindowType::FULLSCREEN_WINDOWED );
                break;
            case WindowType::FULLSCREEN_WINDOWED:
                winManager.mainWindow()->changeType( WindowType::FULLSCREEN );
                break;
            case WindowType::FULLSCREEN:
                winManager.mainWindow()->changeType( WindowType::WINDOW );
                break;
            default: break;
        };
    }

    void GFXDevice::setScreenMSAASampleCount( const U8 sampleCount )
    {
        _queuedScreenSampleChange = sampleCount;
    }

    void GFXDevice::setShadowMSAASampleCount( const ShadowType type, const U8 sampleCount )
    {
        _queuedShadowSampleChange[to_base( type )] = sampleCount;
    }

    void GFXDevice::setScreenMSAASampleCountInternal( U8 sampleCount )
    {
        CLAMP( sampleCount, U8_ZERO, DisplayManager::MaxMSAASamples() );
        if ( _context.config().rendering.MSAASamples != sampleCount )
        {
            _context.config().rendering.MSAASamples = sampleCount;
            _rtPool->getRenderTarget( RenderTargetNames::SCREEN )->updateSampleCount( sampleCount );
            _rtPool->getRenderTarget( RenderTargetNames::OIT )->updateSampleCount( sampleCount );
            Material::RecomputeShaders();
        }
    }

    void GFXDevice::setShadowMSAASampleCountInternal( const ShadowType type, U8 sampleCount )
    {
        CLAMP( sampleCount, U8_ZERO, DisplayManager::MaxMSAASamples() );
        ShadowMap::setMSAASampleCount( type, sampleCount );
    }

    /// The main entry point for any resolution change request
    void GFXDevice::onWindowSizeChange( const SizeChangeParams& params )
    {
        if ( params.isMainWindow )
        {
            fitViewportInWindow( params.width, params.height );
        }
    }

    void GFXDevice::onResolutionChange( const SizeChangeParams& params )
    {
        if ( !params.isMainWindow )
        {
            return;
        }

        const U16 w = params.width;
        const U16 h = params.height;

        // Update resolution only if it's different from the current one.
        // Avoid resolution change on minimize so we don't thrash render targets
        if ( w < 1 || h < 1 || _renderingResolution == vec2<U16>( w, h ) )
        {
            return;
        }

        Configuration& config = context().config();

        const F32 aspectRatio = to_F32( w ) / h;
        const F32 vFoV = Angle::to_VerticalFoV( config.runtime.horizontalFOV, to_D64( aspectRatio ) );
        const vec2<F32> zPlanes( Camera::s_minNearZ, config.runtime.cameraViewDistance );

        // Update the 2D camera so it matches our new rendering viewport
        if ( Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->setProjection( vec4<F32>( 0, to_F32( w ), 0, to_F32( h ) ), vec2<F32>( -1, 1 ) ) )
        {
            Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->updateLookAt();
        }
        if ( Camera::GetUtilityCamera( Camera::UtilityCamera::_2D_FLIP_Y )->setProjection( vec4<F32>( 0, to_F32( w ), to_F32( h ), 0 ), vec2<F32>( -1, 1 ) ) )
        {
            Camera::GetUtilityCamera( Camera::UtilityCamera::_2D_FLIP_Y )->updateLookAt();
        }
        if ( Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT )->setProjection( aspectRatio, vFoV, zPlanes ) )
        {
            Camera::GetUtilityCamera( Camera::UtilityCamera::DEFAULT )->updateLookAt();
        }

        // Update render targets with the new resolution
        _rtPool->getRenderTarget( RenderTargetNames::BACK_BUFFER )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::SCREEN )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::SCREEN_PREV )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::SSAO_RESULT )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::SSR_RESULT )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::HI_Z )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::OIT )->resize( w, h );

        // Update post-processing render targets and buffers
        _renderer->updateResolution( w, h );
        _renderingResolution.set( w, h );

        fitViewportInWindow( w, h );
    }

    bool GFXDevice::fitViewportInWindow( const U16 w, const U16 h )
    {
        const F32 currentAspectRatio = renderingAspectRatio();

        I32 left = 0, bottom = 0;
        I32 newWidth = w;
        I32 newHeight = h;

        const I32 tempWidth = to_I32( h * currentAspectRatio );
        const I32 tempHeight = to_I32( w / currentAspectRatio );

        const F32 newAspectRatio = to_F32( tempWidth ) / tempHeight;

        if ( newAspectRatio <= currentAspectRatio )
        {
            newWidth = tempWidth;
            left = to_I32( (w - newWidth) * 0.5f );
        }
        else
        {
            newHeight = tempHeight;
            bottom = to_I32( (h - newHeight) * 0.5f );
        }

        context().mainWindow().renderingViewport( Rect<I32>( left, bottom, newWidth, newHeight ) );
        return false;
    }
#pragma endregion

#pragma region GPU State

    bool GFXDevice::uploadGPUBlock()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Put the viewport update here as it is the most common source of gpu data invalidation and not always
        // needed for rendering (e.g. changed by RenderTarget::End())

        const vec4<F32> tempViewport{ activeViewport() };
        if ( _gpuBlock._camData._viewPort != tempViewport )
        {
            _gpuBlock._camData._viewPort.set( tempViewport );
            const U32 clustersX = to_U32( std::ceil( to_F32( tempViewport.sizeX ) / Config::Lighting::ClusteredForward::CLUSTERS_X ) );
            const U32 clustersY = to_U32( std::ceil( to_F32( tempViewport.sizeY ) / Config::Lighting::ClusteredForward::CLUSTERS_Y ) );
            if ( clustersX != to_U32( _gpuBlock._camData._renderTargetInfo.z ) ||
                 clustersY != to_U32( _gpuBlock._camData._renderTargetInfo.w ) )
            {
                _gpuBlock._camData._renderTargetInfo.z = to_F32( clustersX );
                _gpuBlock._camData._renderTargetInfo.w = to_F32( clustersY );
            }
            _gpuBlock._camNeedsUpload = true;
        }

        if ( _gpuBlock._camNeedsUpload )
        {
            GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
            _gpuBlock._camNeedsUpload = false;
            frameBuffers._camDataBuffer->incQueue();
            if ( ++frameBuffers._camWritesThisFrame >= frameBuffers._camDataBuffer->queueLength() )
            {
                //We've wrapped around this buffer inside of a single frame so sync performance will degrade unless we increase our buffer size
                _gfxBuffers._needsResizeCam = true;
                //Because we are now overwriting existing data, we need to make sure that any fences that could possibly protect us have been flushed
                DIVIDE_ASSERT( frameBuffers._camBufferWriteRange._length == 0u );
            }
            const BufferRange writtenRange = frameBuffers._camDataBuffer->writeData( &_gpuBlock._camData )._range;

            if ( frameBuffers._camBufferWriteRange._length == 0u )
            {
                frameBuffers._camBufferWriteRange = writtenRange;
            }
            else
            {
                Merge( frameBuffers._camBufferWriteRange, writtenRange );
            }

            DescriptorSetBinding binding{};
            binding._shaderStageVisibility = to_base( ShaderStageVisibility::ALL );
            binding._slot = 1;
            Set( binding._data, _gfxBuffers.crtBuffers()._camDataBuffer.get(), { 0, 1 } );

            _descriptorSets[to_base( DescriptorSetUsage::PER_BATCH )].update( DescriptorSetUsage::PER_BATCH, binding );
            return true;
        }

        return false;
    }

    /// set a new list of clipping planes. The old one is discarded
    void GFXDevice::setClipPlanes( const FrustumClipPlanes& clipPlanes )
    {
        if ( clipPlanes != _clippingPlanes )
        {
            _clippingPlanes = clipPlanes;

            auto& planes = _clippingPlanes.planes();
            auto& states = _clippingPlanes.planeState();

            U8 count = 0u;
            for ( U8 i = 0u; i < to_U8( ClipPlaneIndex::COUNT ); ++i )
            {
                if ( states[i] )
                {
                    _gpuBlock._camData._clipPlanes[count++].set( planes[i]._equation );
                    if ( count == Config::MAX_CLIP_DISTANCES )
                    {
                        break;
                    }
                }
            }

            _gpuBlock._camData._cameraProperties.w = to_F32( count );
            _gpuBlock._camNeedsUpload = true;
        }
    }

    void GFXDevice::setDepthRange( const vec2<F32> depthRange )
    {
        GFXShaderData::CamData& data = _gpuBlock._camData;
        if ( data._renderTargetInfo.xy != depthRange )
        {
            data._renderTargetInfo.xy = depthRange;
            _gpuBlock._camNeedsUpload = true;
        }
    }

    void GFXDevice::renderFromCamera( const CameraSnapshot& cameraSnapshot )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GFXShaderData::CamData& data = _gpuBlock._camData;

        bool projectionDirty = false, viewDirty = false;

        if ( cameraSnapshot._projectionMatrix != data._projectionMatrix )
        {
            const F32 zNear = cameraSnapshot._zPlanes.min;
            const F32 zFar = cameraSnapshot._zPlanes.max;

            data._projectionMatrix.set( cameraSnapshot._projectionMatrix );
            data._cameraProperties.xyz.set( zNear, zFar, cameraSnapshot._fov );

            if ( cameraSnapshot._isOrthoCamera )
            {
                data._lightingTweakValues.x = 1.f; //scale
                data._lightingTweakValues.y = 0.f; //bias
            }
            else
            {
                //ref: http://www.aortiz.me/2018/12/21/CG.html
                constexpr F32 CLUSTERS_Z = to_F32( Config::Lighting::ClusteredForward::CLUSTERS_Z );
                const F32 zLogRatio = std::log( zFar / zNear );

                data._lightingTweakValues.x = CLUSTERS_Z / zLogRatio; //scale
                data._lightingTweakValues.y = -(CLUSTERS_Z * std::log( zNear ) / zLogRatio); //bias
            }
            projectionDirty = true;
        }

        if ( cameraSnapshot._viewMatrix != data._viewMatrix )
        {
            data._viewMatrix.set( cameraSnapshot._viewMatrix );
            data._invViewMatrix.set( cameraSnapshot._invViewMatrix );
            viewDirty = true;
        }

        if ( projectionDirty || viewDirty )
        {
            _gpuBlock._camNeedsUpload = true;
            _activeCameraSnapshot = cameraSnapshot;
        }
    }

    void GFXDevice::shadowingSettings( const F32 lightBleedBias, const F32 minShadowVariance ) noexcept
    {
        GFXShaderData::CamData& data = _gpuBlock._camData;

        if ( !COMPARE( data._lightingTweakValues.z, lightBleedBias ) ||
             !COMPARE( data._lightingTweakValues.w, minShadowVariance ) )
        {
            data._lightingTweakValues.zw = { lightBleedBias, minShadowVariance };
            _gpuBlock._camNeedsUpload = true;
        }
    }

    void GFXDevice::worldAOViewProjectionMatrix( const mat4<F32>& vpMatrix ) noexcept
    {
        GFXShaderData::CamData& data = _gpuBlock._camData;
        data._worldAOVPMatrix = vpMatrix;
        _gpuBlock._camNeedsUpload = true;
    }

    void GFXDevice::setPreviousViewProjectionMatrix( const mat4<F32>& prevViewMatrix, const mat4<F32> prevProjectionMatrix )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );


        bool projectionDirty = false, viewDirty = false;
        if ( _gpuBlock._prevFrameData._previousViewMatrix != prevViewMatrix )
        {
            _gpuBlock._prevFrameData._previousViewMatrix = prevViewMatrix;
            viewDirty = true;
        }
        if ( _gpuBlock._prevFrameData._previousProjectionMatrix != prevProjectionMatrix )
        {
            _gpuBlock._prevFrameData._previousProjectionMatrix = prevProjectionMatrix;
            projectionDirty = true;
        }

        if ( projectionDirty || viewDirty )
        {
            mat4<F32>::Multiply( _gpuBlock._prevFrameData._previousProjectionMatrix, _gpuBlock._prevFrameData._previousViewMatrix, _gpuBlock._prevFrameData._previousViewProjectionMatrix );
        }
    }

    bool GFXDevice::setViewport( const Rect<I32>& viewport )
    {
        _activeViewport.set( viewport );
        return _api->setViewportInternal( viewport );
    }

    bool GFXDevice::setScissor( const Rect<I32>& scissor )
    {
        _activeScissor.set( scissor );
        return _api->setScissorInternal( scissor );
    }

    void GFXDevice::setCameraSnapshot( const PlayerIndex index, const CameraSnapshot& snapshot ) noexcept
    {
        _cameraSnapshotHistory[index] = snapshot;
    }

    CameraSnapshot& GFXDevice::getCameraSnapshot( const PlayerIndex index ) noexcept
    {
        return _cameraSnapshotHistory[index];
    }

    const CameraSnapshot& GFXDevice::getCameraSnapshot( const PlayerIndex index ) const noexcept
    {
        return _cameraSnapshotHistory[index];
    }

    const GFXShaderData::CamData& GFXDevice::cameraData() const noexcept
    {
        return _gpuBlock._camData;
    }

    const GFXShaderData::PrevFrameData& GFXDevice::previousFrameData() const noexcept
    {
        return _gpuBlock._prevFrameData;
    }
#pragma endregion

#pragma region Command buffers, occlusion culling, etc
    void GFXDevice::validateAndUploadDescriptorSets()
    {
        uploadGPUBlock();

        thread_local DescriptorSetEntries setEntries{};

        constexpr std::array<DescriptorSetUsage, to_base( DescriptorSetUsage::COUNT )> prioritySorted = {
            DescriptorSetUsage::PER_FRAME,
            DescriptorSetUsage::PER_PASS,
            DescriptorSetUsage::PER_BATCH,
            DescriptorSetUsage::PER_DRAW
        };

        for ( const DescriptorSetUsage usage : prioritySorted )
        {
            GFXDescriptorSet& set = _descriptorSets[to_base( usage )];
            auto& entry = setEntries[to_base(usage)];
            entry._set = &set.impl();
            entry._usage = usage;
            entry._isDirty = set.dirty();
            set.dirty( false );
        }

        _api->bindShaderResources( setEntries );
    }

    void GFXDevice::flushCommandBuffer( GFX::CommandBuffer& commandBuffer, const bool batch )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( batch )
        {
            commandBuffer.batch();
        }

        LockGuard<Mutex> w_lock( _queuedCommandbufferLock );
        _activeWindow->getCurrentCommandBuffer()->add(commandBuffer);
    }

    void GFXDevice::flushCommandBufferInternal( GFX::CommandBuffer& commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const Rect<I32> initialViewport = activeViewport();
        const Rect<I32> initialScissor = activeScissor();

        _api->preFlushCommandBuffer( commandBuffer );

        const GFX::CommandBuffer::CommandOrderContainer& commands = commandBuffer();
        for ( const GFX::CommandBuffer::CommandEntry& cmd : commands )
        {
            const GFX::CommandType cmdType = static_cast<GFX::CommandType>(cmd._idx._type);
            if ( IsSubmitCommand( cmdType ) )
            {
                validateAndUploadDescriptorSets();
            }

            switch ( cmdType )
            {
                case GFX::CommandType::READ_BUFFER_DATA:
                {
                    PROFILE_SCOPE( "READ_BUFFER_DATA", Profiler::Category::Graphics );

                    const GFX::ReadBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ReadBufferDataCommand>( cmd );
                    crtCmd._buffer->readData( { crtCmd._offsetElementCount, crtCmd._elementCount }, crtCmd._target );
                } break;
                case GFX::CommandType::CLEAR_BUFFER_DATA:
                {
                    PROFILE_SCOPE( "CLEAR_BUFFER_DATA", Profiler::Category::Graphics );

                    const GFX::ClearBufferDataCommand& crtCmd = *commandBuffer.get<GFX::ClearBufferDataCommand>( cmd );
                    if ( crtCmd._buffer != nullptr )
                    {
                        GFX::MemoryBarrierCommand memCmd{};
                        memCmd._bufferLocks.push_back( crtCmd._buffer->clearData( { crtCmd._offsetElementCount, crtCmd._elementCount } ) );
                        _api->flushCommand( &memCmd );
                    }
                } break;
                case GFX::CommandType::SET_VIEWPORT:
                {
                    PROFILE_SCOPE( "SET_VIEWPORT", Profiler::Category::Graphics );

                    setViewport( commandBuffer.get<GFX::SetViewportCommand>( cmd )->_viewport );
                } break;
                case GFX::CommandType::PUSH_VIEWPORT:
                {
                    PROFILE_SCOPE( "PUSH_VIEWPORT", Profiler::Category::Graphics );

                    const GFX::PushViewportCommand* crtCmd = commandBuffer.get<GFX::PushViewportCommand>( cmd );
                    _viewportStack.push( activeViewport() );
                    setViewport( crtCmd->_viewport );
                } break;
                case GFX::CommandType::POP_VIEWPORT:
                {
                    PROFILE_SCOPE( "POP_VIEWPORT", Profiler::Category::Graphics );

                    setViewport( _viewportStack.top() );
                    _viewportStack.pop();
                } break;
                case GFX::CommandType::SET_SCISSOR:
                {
                    PROFILE_SCOPE( "SET_SCISSOR", Profiler::Category::Graphics );
                    setScissor( commandBuffer.get<GFX::SetScissorCommand>( cmd )->_rect );
                } break;
                case GFX::CommandType::SET_CAMERA:
                {
                    PROFILE_SCOPE( "SET_CAMERA", Profiler::Category::Graphics );

                    const GFX::SetCameraCommand* crtCmd = commandBuffer.get<GFX::SetCameraCommand>( cmd );
                    // Tell the Rendering API to draw from our desired PoV
                    renderFromCamera( crtCmd->_cameraSnapshot );
                } break;
                case GFX::CommandType::PUSH_CAMERA:
                {
                    PROFILE_SCOPE( "PUSH_CAMERA", Profiler::Category::Graphics );

                    const GFX::PushCameraCommand* crtCmd = commandBuffer.get<GFX::PushCameraCommand>( cmd );
                    DIVIDE_ASSERT( _cameraSnapshots.size() < MAX_CAMERA_SNAPSHOTS, "GFXDevice::flushCommandBuffer error: PUSH_CAMERA stack too deep!" );

                    _cameraSnapshots.push( _activeCameraSnapshot );
                    renderFromCamera( crtCmd->_cameraSnapshot );
                } break;
                case GFX::CommandType::POP_CAMERA:
                {
                    PROFILE_SCOPE( "POP_CAMERA", Profiler::Category::Graphics );

                    renderFromCamera( _cameraSnapshots.top() );
                    _cameraSnapshots.pop();
                } break;
                case GFX::CommandType::SET_CLIP_PLANES:
                {
                    PROFILE_SCOPE( "SET_CLIP_PLANES", Profiler::Category::Graphics );

                    setClipPlanes( commandBuffer.get<GFX::SetClipPlanesCommand>( cmd )->_clippingPlanes );
                } break;
                case GFX::CommandType::BIND_SHADER_RESOURCES:
                {
                    PROFILE_SCOPE( "BIND_SHADER_RESOURCES", Profiler::Category::Graphics );

                    const auto resCmd = commandBuffer.get<GFX::BindShaderResourcesCommand>( cmd );
                    descriptorSet( resCmd->_usage ).update( resCmd->_usage, resCmd->_set );

                } break;
                default: break;
            }

            _api->flushCommand( commandBuffer.get<GFX::CommandBase>( cmd ) );
        }

        GFXBuffers::PerFrameBuffers& frameBuffers = _gfxBuffers.crtBuffers();
        if ( frameBuffers._camBufferWriteRange._length > 0u )
        {
            GFX::MemoryBarrierCommand writeMemCmd{};
            BufferLock& lock = writeMemCmd._bufferLocks.emplace_back();
            lock._buffer = frameBuffers._camDataBuffer->getBufferImpl();
            lock._range = frameBuffers._camBufferWriteRange;
            lock._type = BufferSyncUsage::CPU_WRITE_TO_GPU_READ;
            _api->flushCommand( &writeMemCmd );
            frameBuffers._camBufferWriteRange = {};
        }

        _api->postFlushCommandBuffer( commandBuffer );

        setViewport( initialViewport );
        setScissor( initialScissor );

        // Descriptor sets are only valid per command buffer they are submitted in. If we finish the command buffer submission,
        // we mark them as dirty so that the next command buffer can bind them again even if the data is the same
        // We always check the dirty flags before any draw/compute command by calling "validateAndUploadDescriptorSets" beforehand
        for ( auto& set : _descriptorSets )
        {
            set.dirty( true );
        }
        descriptorSet( DescriptorSetUsage::PER_DRAW ).clear();
    }

    /// Transform our depth buffer to a HierarchicalZ buffer (for occlusion queries and screen space reflections)
    /// Based on RasterGrid implementation: http://rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/
    /// Modified with nVidia sample code: https://github.com/nvpro-samples/gl_occlusion_culling
    std::pair<const Texture_ptr&, SamplerDescriptor> GFXDevice::constructHIZ( RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        assert( depthBuffer != HiZTarget );

        const RTAttachment* SrcAtt = _rtPool->getRenderTarget( depthBuffer )->getAttachment( RTAttachmentType::DEPTH );
        const RTAttachment* HiZAtt = _rtPool->getRenderTarget( HiZTarget )->getAttachment( RTAttachmentType::COLOUR );
        Texture* HiZTex = HiZAtt->texture().get();
        DIVIDE_ASSERT( HiZTex->descriptor().mipMappingState() == TextureDescriptor::MipMappingState::MANUAL );

        GFX::EnqueueCommand( cmdBufferInOut, GFX::BeginDebugScopeCommand{ "Construct Hi-Z" } );
        GFX::EnqueueCommand( cmdBufferInOut, GFX::BindPipelineCommand{ _hIZPipeline } );

        U32 twidth = HiZTex->width();
        U32 theight = HiZTex->height();
        bool wasEven = false;
        U32 owidth = twidth;
        U32 oheight = theight;

        PushConstantsStruct pushConstants{};

        for ( U16 i = 0u; i < HiZTex->mipCount(); ++i )
        {
            twidth = twidth < 1u ? 1u : twidth;
            theight = theight < 1u ? 1u : theight;

            ImageView outImage = HiZTex->getView( { i, 1u } );

            const ImageView inImage = i == 0u ? SrcAtt->texture()->getView( ) 
                                              : HiZTex->getView( { to_U16(i - 1u), 1u }, { 0u, 1u });

            
            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( cmdBufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
            {
                ._targetView = outImage,
                ._sourceLayout = ImageUsage::SHADER_READ,
                ._targetLayout = ImageUsage::SHADER_WRITE
            });
            
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( cmdBufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 12u, ShaderStageVisibility::COMPUTE );
                Set(binding._data, outImage, ImageUsage::SHADER_WRITE);
            }
            {
                DescriptorSetBinding& binding = AddBinding( cmd->_set, 1u, ShaderStageVisibility::COMPUTE );
                Set( binding._data, inImage, HiZAtt->_descriptor._sampler );
            }

            pushConstants.data[0]._vec[0].set( owidth, oheight, twidth, theight );
            pushConstants.data[0]._vec[1].x = wasEven ? 1.f : 0.f;
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( cmdBufferInOut )->_constants.set( pushConstants );

            GFX::EnqueueCommand<GFX::DispatchComputeCommand>( cmdBufferInOut )->_computeGroupSize =
            {
                getGroupCount( twidth, DEPTH_REDUCE_LOCAL_SIZE ),
                getGroupCount( theight, DEPTH_REDUCE_LOCAL_SIZE ),
                1u
            };

            wasEven = twidth % 2 == 0 && theight % 2 == 0;
            owidth = twidth;
            oheight = theight;
            twidth /= 2;
            theight /= 2;

            GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( cmdBufferInOut )->_textureLayoutChanges.emplace_back(TextureLayoutChange
            {
                ._targetView = outImage,
                ._sourceLayout = ImageUsage::SHADER_WRITE,
                ._targetLayout = ImageUsage::SHADER_READ
            });
        }

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( cmdBufferInOut );

        return { HiZAtt->texture(), HiZAtt->_descriptor._sampler };
    }

    void GFXDevice::occlusionCull( const RenderPass::BufferData& bufferData,
                                   const Texture_ptr& hizBuffer,
                                   const SamplerDescriptor sampler,
                                   const CameraSnapshot& cameraSnapshot,
                                   const bool countCulledNodes,
                                   GFX::CommandBuffer& bufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const U32 cmdCount = *bufferData._lastCommandCount;
        const U32 threadCount = getGroupCount( cmdCount, GROUP_SIZE_AABB );

        if ( threadCount == 0u || !enableOcclusionCulling() )
        {
            GFX::EnqueueCommand( bufferInOut, GFX::AddDebugMessageCommand( "Occlusion Culling Skipped" ) );
            return;
        }

        ShaderBuffer* cullBuffer = _gfxBuffers.crtBuffers()._cullCounter.get();
        GFX::EnqueueCommand( bufferInOut, GFX::BeginDebugScopeCommand{ "Occlusion Cull" } );

        // Not worth the overhead for a handful of items and the Pre-Z pass should handle overdraw just fine
        GFX::EnqueueCommand( bufferInOut, GFX::BindPipelineCommand{ _hIZCullPipeline } );
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, hizBuffer->getView(), sampler );
        }
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 7u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, cullBuffer, { 0u, 1u });
        }

        PushConstantsStruct fastConstants{};
        mat4<F32>::Multiply( cameraSnapshot._projectionMatrix, cameraSnapshot._viewMatrix, fastConstants.data[0] );
        fastConstants.data[1] = cameraSnapshot._viewMatrix;

        auto& pushConstants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants;
        pushConstants.set( _ID( "dvd_countCulledItems" ), PushConstantType::UINT, countCulledNodes ? 1u : 0u );
        pushConstants.set( _ID( "dvd_numEntities" ), PushConstantType::UINT, cmdCount );
        pushConstants.set( _ID( "dvd_viewSize" ), PushConstantType::VEC2, vec2<F32>( hizBuffer->width(), hizBuffer->height() ) );
        pushConstants.set( _ID( "dvd_frustumPlanes" ), PushConstantType::VEC4, cameraSnapshot._frustumPlanes );
        pushConstants.set( fastConstants );

        GFX::EnqueueCommand( bufferInOut, GFX::DispatchComputeCommand{ threadCount, 1, 1 } );

        // Occlusion culling barrier
        GFX::EnqueueCommand<GFX::MemoryBarrierCommand>( bufferInOut )->_bufferLocks.emplace_back(BufferLock
        {
            ._range = {0u, U32_MAX },
            ._type = BufferSyncUsage::GPU_WRITE_TO_CPU_READ,
            ._buffer = cullBuffer->getBufferImpl()
        });

        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );

        if ( queryPerformanceStats() && countCulledNodes )
        {
            GFX::ReadBufferDataCommand readAtomicCounter;
            readAtomicCounter._buffer = cullBuffer;
            readAtomicCounter._target = { &_lastCullCount, 4 * sizeof( U32 ) };
            readAtomicCounter._offsetElementCount = 0;
            readAtomicCounter._elementCount = 1;
            GFX::EnqueueCommand( bufferInOut, readAtomicCounter );

            cullBuffer->incQueue();

            GFX::ClearBufferDataCommand clearAtomicCounter{};
            clearAtomicCounter._buffer = cullBuffer;
            clearAtomicCounter._offsetElementCount = 0;
            clearAtomicCounter._elementCount = 1;
            GFX::EnqueueCommand( bufferInOut, clearAtomicCounter );
        }
    }
#pragma endregion

#pragma region Drawing functions
    void GFXDevice::drawTextureInViewport( const ImageView& texture, const SamplerDescriptor sampler, const Rect<I32>& viewport, const bool convertToSrgb, const bool drawToDepthOnly, bool drawBlend, GFX::CommandBuffer& bufferInOut )
    {
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Draw Texture In Viewport";
        GFX::EnqueueCommand( bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot() } );
        GFX::EnqueueCommand( bufferInOut, drawToDepthOnly ? _drawFSDepthPipelineCmd : drawBlend ? _drawFSTexturePipelineBlendCmd : _drawFSTexturePipelineCmd );

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, texture, sampler );

        GFX::EnqueueCommand( bufferInOut, GFX::PushViewportCommand{ viewport } );

        if ( !drawToDepthOnly )
        {
            PushConstantsStruct pushData{};
            pushData.data[0]._vec[0].x = convertToSrgb ? 1.f : 0.f;
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut)->_constants.set(pushData);
        }

        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::PopViewportCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
    }
#pragma endregion

#pragma region Debug utilities
    void GFXDevice::renderDebugUI( const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        constexpr I32 padding = 5;

        // Early out if we didn't request the preview
        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            GFX::EnqueueCommand( bufferInOut, GFX::BeginDebugScopeCommand{ "Render Debug Views" } );

            renderDebugViews(
                Rect<I32>( targetViewport.x + padding,
                           targetViewport.y + padding,
                           targetViewport.z - padding,
                           targetViewport.w - padding ),
                padding,
                bufferInOut,
                memCmdInOut);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>( bufferInOut );
        }
    }

    void GFXDevice::initDebugViews()
    {
        // Lazy-load preview shader
        if ( !_previewDepthMapShader )
        {
            ShaderModuleDescriptor vertModule = {};
            vertModule._moduleType = ShaderType::VERTEX;
            vertModule._sourceFile = "baseVertexShaders.glsl";
            vertModule._variant = "FullScreenQuad";

            ShaderModuleDescriptor fragModule = {};
            fragModule._moduleType = ShaderType::FRAGMENT;
            fragModule._sourceFile = "fbPreview.glsl";
            fragModule._variant = "LinearDepth";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            // The LinearDepth variant converts the depth values to linear values between the 2 scene z-planes
            ResourceDescriptor fbPreview( "fbPreviewLinearDepth" );
            fbPreview.propertyDescriptor( shaderDescriptor );
            _previewDepthMapShader = CreateResource<ShaderProgram>( context().kernel().resourceCache(), fbPreview );
            assert( _previewDepthMapShader != nullptr );

            DebugView_ptr HiZ = std::make_shared<DebugView>();
            HiZ->_shader = _renderTargetDraw;
            HiZ->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::HI_Z )->getAttachment( RTAttachmentType::COLOUR )->texture();
            HiZ->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::HI_Z )->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
            HiZ->_name = "Hierarchical-Z";
            HiZ->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.f );
            HiZ->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            HiZ->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            HiZ->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 1u );
            HiZ->_cycleMips = true;

            DebugView_ptr DepthPreview = std::make_shared<DebugView>();
            DepthPreview->_shader = _previewDepthMapShader;
            DepthPreview->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::DEPTH )->texture();
            DepthPreview->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::DEPTH )->_descriptor._sampler;
            DepthPreview->_name = "Depth Buffer";
            DepthPreview->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            DepthPreview->_shaderData.set( _ID( "_zPlanes" ), PushConstantType::VEC2, vec2<F32>( Camera::s_minNearZ, _context.config().runtime.cameraViewDistance ) );

            DebugView_ptr NormalPreview = std::make_shared<DebugView>();
            NormalPreview->_shader = _renderTargetDraw;
            NormalPreview->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::NORMALS_RESOLVED )->getAttachment( RTAttachmentType::COLOUR )->texture();
            NormalPreview->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::NORMALS_RESOLVED )->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
            NormalPreview->_name = "Normals";
            NormalPreview->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            NormalPreview->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, true );
            NormalPreview->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            NormalPreview->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 2u );
            NormalPreview->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            DebugView_ptr VelocityPreview = std::make_shared<DebugView>();
            VelocityPreview->_shader = _renderTargetDraw;
            VelocityPreview->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::VELOCITY )->texture();
            VelocityPreview->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::SCREEN )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::VELOCITY )->_descriptor._sampler;
            VelocityPreview->_name = "Velocity Map";
            VelocityPreview->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            VelocityPreview->_shaderData.set( _ID( "scaleAndBias" ), PushConstantType::BOOL, true );
            VelocityPreview->_shaderData.set( _ID( "normalizeOutput" ), PushConstantType::BOOL, true );
            VelocityPreview->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            VelocityPreview->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            VelocityPreview->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 2u );
            VelocityPreview->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 5.0f );

            DebugView_ptr SSAOPreview = std::make_shared<DebugView>();
            SSAOPreview->_shader = _renderTargetDraw;
            SSAOPreview->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::SSAO_RESULT )->getAttachment( RTAttachmentType::COLOUR )->texture();
            SSAOPreview->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::SSAO_RESULT )->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
            SSAOPreview->_name = "SSAO Map";
            SSAOPreview->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            SSAOPreview->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            SSAOPreview->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            SSAOPreview->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 1u );
            SSAOPreview->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            DebugView_ptr AlphaAccumulationHigh = std::make_shared<DebugView>();
            AlphaAccumulationHigh->_shader = _renderTargetDraw;
            AlphaAccumulationHigh->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::OIT )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::ALBEDO )->texture();
            AlphaAccumulationHigh->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::OIT )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::ALBEDO )->_descriptor._sampler;
            AlphaAccumulationHigh->_name = "Alpha Accumulation High";
            AlphaAccumulationHigh->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            AlphaAccumulationHigh->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            AlphaAccumulationHigh->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            AlphaAccumulationHigh->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 4u );
            AlphaAccumulationHigh->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            DebugView_ptr AlphaRevealageHigh = std::make_shared<DebugView>();
            AlphaRevealageHigh->_shader = _renderTargetDraw;
            AlphaRevealageHigh->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::OIT )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::REVEALAGE )->texture();
            AlphaRevealageHigh->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::OIT )->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::REVEALAGE )->_descriptor._sampler;
            AlphaRevealageHigh->_name = "Alpha Revealage High";
            AlphaRevealageHigh->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            AlphaRevealageHigh->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            AlphaRevealageHigh->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            AlphaRevealageHigh->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 1u );
            AlphaRevealageHigh->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            SamplerDescriptor lumaSampler = {};
            lumaSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
            lumaSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
            lumaSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
            lumaSampler._minFilter = TextureFilter::NEAREST;
            lumaSampler._magFilter = TextureFilter::NEAREST;
            lumaSampler._mipSampling = TextureMipSampling::NONE;
            lumaSampler._anisotropyLevel = 0u;

            DebugView_ptr Luminance = std::make_shared<DebugView>();
            Luminance->_shader = _renderTargetDraw;
            Luminance->_texture = getRenderer().postFX().getFilterBatch().luminanceTex();
            Luminance->_sampler = lumaSampler;
            Luminance->_name = "Luminance";
            Luminance->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            Luminance->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            Luminance->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            Luminance->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 1u );
            Luminance->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            const RenderTargetHandle& edgeRTHandle = getRenderer().postFX().getFilterBatch().edgesRT();

            DebugView_ptr Edges = std::make_shared<DebugView>();
            Edges->_shader = _renderTargetDraw;
            Edges->_texture = renderTargetPool().getRenderTarget( edgeRTHandle._targetID )->getAttachment( RTAttachmentType::COLOUR )->texture();
            Edges->_sampler = renderTargetPool().getRenderTarget( edgeRTHandle._targetID )->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
            Edges->_name = "Edges";
            Edges->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, 0.0f );
            Edges->_shaderData.set( _ID( "channelsArePacked" ), PushConstantType::BOOL, false );
            Edges->_shaderData.set( _ID( "startChannel" ), PushConstantType::UINT, 0u );
            Edges->_shaderData.set( _ID( "channelCount" ), PushConstantType::UINT, 4u );
            Edges->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

            addDebugView( HiZ );
            addDebugView( DepthPreview );
            addDebugView( NormalPreview );
            addDebugView( VelocityPreview );
            addDebugView( SSAOPreview );
            addDebugView( AlphaAccumulationHigh );
            addDebugView( AlphaRevealageHigh );
            addDebugView( Luminance );
            addDebugView( Edges );
            WAIT_FOR_CONDITION( _previewDepthMapShader->getState() == ResourceState::RES_LOADED );
        }
    }

    void GFXDevice::renderDebugViews( const Rect<I32> targetViewport, const I32 padding, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        static size_t labelStyleHash = TextLabelStyle( Font::DROID_SERIF_BOLD, UColour4( 196 ), 96 ).getHash();

        thread_local vector_fast<std::tuple<string, I32, Rect<I32>>> labelStack;

        initDebugViews();

        constexpr I32 columnCount = 6u;
        I32 viewCount = to_I32( _debugViews.size() );
        for ( const auto& view : _debugViews )
        {
            if ( !view->_enabled )
            {
                --viewCount;
            }
        }

        if ( viewCount == 0 )
        {
            return;
        }

        labelStack.resize( 0 );

        const I32 screenWidth = targetViewport.z - targetViewport.x;
        const I32 screenHeight = targetViewport.w - targetViewport.y;
        const F32 aspectRatio = to_F32( screenWidth ) / screenHeight;

        const I32 viewportWidth = (screenWidth / columnCount) - (padding * (columnCount - 1u));
        const I32 viewportHeight = to_I32( viewportWidth / aspectRatio ) - padding;
        Rect<I32> viewport( targetViewport.z - viewportWidth, targetViewport.y, viewportWidth, viewportHeight );

        const I32 initialOffsetX = viewport.x;

        PipelineDescriptor pipelineDesc{};
        pipelineDesc._stateBlock = _state2DRendering;
        pipelineDesc._shaderProgramHandle = SHADER_INVALID_HANDLE;
        pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;

        const Rect<I32> previousViewport = activeViewport();

        Pipeline* crtPipeline = nullptr;
        U16 idx = 0u;
        const I32 mipTimer = to_I32( std::ceil( Time::App::ElapsedMilliseconds() / 750.0f ) );
        for ( U16 i = 0; i < to_U16( _debugViews.size() ); ++i )
        {
            if ( !_debugViews[i]->_enabled )
            {
                continue;
            }

            const DebugView_ptr& view = _debugViews[i];

            if ( view->_cycleMips )
            {
                const F32 lodLevel = to_F32( mipTimer % view->_texture->mipCount() );
                view->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, lodLevel );
                labelStack.emplace_back( Util::StringFormat( "Mip level: %d", to_U8( lodLevel ) ), viewport.sizeY * 4, viewport );
            }
            const ShaderProgramHandle crtShader = pipelineDesc._shaderProgramHandle;
            const ShaderProgramHandle newShader = view->_shader->handle();
            if ( crtShader != newShader )
            {
                pipelineDesc._shaderProgramHandle = view->_shader->handle();
                crtPipeline = newPipeline( pipelineDesc );
            }

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = crtPipeline;
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_constants = view->_shaderData;
            GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport.set( viewport );

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, view->_textureBindSlot, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, view->_texture->getView(), view->_sampler );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut );

            if ( !view->_name.empty() )
            {
                labelStack.emplace_back( view->_name, viewport.sizeY, viewport );
            }
            if ( idx > 0 && idx % (columnCount - 1) == 0 )
            {
                viewport.y += viewportHeight + targetViewport.y;
                viewport.x = initialOffsetX;
                idx = 0u;
            }
            else
            {
                viewport.x -= viewportWidth + targetViewport.x;
                ++idx;
            }
        }

        GFX::EnqueueCommand( bufferInOut, GFX::PushCameraCommand{ Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot() } );
        // Draw labels at the end to reduce number of state changes
        TextElement text( labelStyleHash, RelativePosition2D( RelativeValue( 0.1f, 0.0f ), RelativeValue( 0.1f, 0.0f ) ) );
        for ( const auto& [labelText, viewportOffsetY, viewportIn] : labelStack )
        {
            GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport.set( viewportIn );

            text.position()._y._offset = to_F32( viewportOffsetY );
            text.text( labelText.c_str(), false );
            _context.gui().drawText( TextElementBatch{ text }, viewportIn, bufferInOut, memCmdInOut, false );
        }
        GFX::EnqueueCommand<GFX::PopCameraCommand>( bufferInOut );
        GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport.set( previousViewport );
    }

    DebugView* GFXDevice::addDebugView( const std::shared_ptr<DebugView>& view )
    {
        LockGuard<Mutex> lock( _debugViewLock );

        _debugViews.push_back( view );

        if ( _debugViews.back()->_sortIndex == -1 )
        {
            _debugViews.back()->_sortIndex = to_I16( _debugViews.size() );
        }

        eastl::sort( eastl::begin( _debugViews ),
                     eastl::end( _debugViews ),
                     []( const std::shared_ptr<DebugView>& a, const std::shared_ptr<DebugView>& b ) noexcept -> bool
                     {
                         if ( a->_groupID == b->_groupID )
                         {
                             return a->_sortIndex < b->_sortIndex;
                         }
                         if ( a->_sortIndex == b->_sortIndex )
                         {
                             return a->_groupID < b->_groupID;
                         }

                         return a->_groupID < b->_groupID&& a->_sortIndex < b->_sortIndex;
                     } );

        return view.get();
    }

    bool GFXDevice::removeDebugView( DebugView* view )
    {
        return dvd_erase_if( _debugViews,
                             [view]( const std::shared_ptr<DebugView>& entry ) noexcept
                             {
                                 return view != nullptr && view->getGUID() == entry->getGUID();
                             } );
    }

    void GFXDevice::toggleDebugView( const I16 index, const bool state )
    {
        LockGuard<Mutex> lock( _debugViewLock );
        for ( auto& view : _debugViews )
        {
            if ( view->_sortIndex == index )
            {
                view->_enabled = state;
                break;
            }
        }
    }

    void GFXDevice::toggleDebugGroup( I16 group, const bool state )
    {
        LockGuard<Mutex> lock( _debugViewLock );
        for ( auto& view : _debugViews )
        {
            if ( view->_groupID == group )
            {
                view->_enabled = state;
            }
        }
    }

    bool GFXDevice::getDebugGroupState( I16 group ) const
    {
        LockGuard<Mutex> lock( _debugViewLock );
        for ( const auto& view : _debugViews )
        {
            if ( view->_groupID == group )
            {
                if ( !view->_enabled )
                {
                    return false;
                }
            }
        }

        return true;
    }

    void GFXDevice::getDebugViewNames( vector<std::tuple<string, I16, I16, bool>>& namesOut )
    {
        namesOut.resize( 0 );

        LockGuard<Mutex> lock( _debugViewLock );
        for ( auto& view : _debugViews )
        {
            namesOut.emplace_back( view->_name, view->_groupID, view->_sortIndex, view->_enabled );
        }
    }

    PipelineDescriptor& GFXDevice::getDebugPipeline( const IM::BaseDescriptor& descriptor ) noexcept
    {
        if ( descriptor.noDepth )
        {
            return (descriptor.noCull ? _debugGizmoPipelineNoCullNoDepth : _debugGizmoPipelineNoDepth);
        }
        else if ( descriptor.noCull )
        {
            return _debugGizmoPipelineNoCull;
        }

        return _debugGizmoPipeline;
    }

    void GFXDevice::debugDrawLines( const I64 ID, const IM::LineDescriptor descriptor ) noexcept
    {
        _debugLines.add( ID, descriptor );
    }

    void GFXDevice::debugDrawLines( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugLines._dataLock );

        const size_t lineCount = _debugLines.size();
        for ( size_t f = 0u; f < lineCount; ++f )
        {
            auto& data = _debugLines._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& linePrimitive = _debugLines._debugPrimitives[f];
            if ( linePrimitive == nullptr )
            {
                linePrimitive = newIMP( Util::StringFormat( "DebugLine_%d", f ).c_str() );
                linePrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            linePrimitive->forceWireframe( data._descriptor.wireframe ); //? Uhm, not gonna do much -Ionut
            linePrimitive->fromLines( data._descriptor );
            linePrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    void GFXDevice::debugDrawBox( const I64 ID, const IM::BoxDescriptor descriptor ) noexcept
    {
        _debugBoxes.add( ID, descriptor );
    }

    void GFXDevice::debugDrawBoxes( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugBoxes._dataLock );
        const size_t boxesCount = _debugBoxes.size();
        for ( U32 f = 0u; f < boxesCount; ++f )
        {
            auto& data = _debugBoxes._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& boxPrimitive = _debugBoxes._debugPrimitives[f];
            if ( boxPrimitive == nullptr )
            {
                boxPrimitive = newIMP( Util::StringFormat( "DebugBox_%d", f ).c_str() );
                boxPrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            boxPrimitive->forceWireframe( data._descriptor.wireframe );
            boxPrimitive->fromBox( data._descriptor );
            boxPrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    void GFXDevice::debugDrawOBB( const I64 ID, const IM::OBBDescriptor descriptor ) noexcept
    {
        _debugOBBs.add( ID, descriptor );
    }

    void GFXDevice::debugDrawOBBs( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugOBBs._dataLock );
        const size_t boxesCount = _debugOBBs.size();
        for ( U32 f = 0u; f < boxesCount; ++f )
        {
            auto& data = _debugOBBs._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& boxPrimitive = _debugOBBs._debugPrimitives[f];
            if ( boxPrimitive == nullptr )
            {
                boxPrimitive = newIMP( Util::StringFormat( "DebugOBB_%d", f ).c_str() );
                boxPrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            boxPrimitive->forceWireframe( data._descriptor.wireframe );
            boxPrimitive->fromOBB( data._descriptor );
            boxPrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }
    void GFXDevice::debugDrawSphere( const I64 ID, const IM::SphereDescriptor descriptor ) noexcept
    {
        _debugSpheres.add( ID, descriptor );
    }

    void GFXDevice::debugDrawSpheres( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugSpheres._dataLock );
        const size_t spheresCount = _debugSpheres.size();
        for ( size_t f = 0u; f < spheresCount; ++f )
        {
            auto& data = _debugSpheres._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& spherePrimitive = _debugSpheres._debugPrimitives[f];
            if ( spherePrimitive == nullptr )
            {
                spherePrimitive = newIMP( Util::StringFormat( "DebugSphere_%d", f ).c_str() );
                spherePrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            spherePrimitive->forceWireframe( data._descriptor.wireframe );
            spherePrimitive->fromSphere( data._descriptor );
            spherePrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    void GFXDevice::debugDrawCone( const I64 ID, const IM::ConeDescriptor descriptor ) noexcept
    {
        _debugCones.add( ID, descriptor );
    }

    void GFXDevice::debugDrawCones( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugCones._dataLock );

        const size_t conesCount = _debugCones.size();
        for ( size_t f = 0u; f < conesCount; ++f )
        {
            auto& data = _debugCones._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& conePrimitive = _debugCones._debugPrimitives[f];
            if ( conePrimitive == nullptr )
            {
                conePrimitive = newIMP( Util::StringFormat( "DebugCone_%d", f ).c_str() );
                conePrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            conePrimitive->forceWireframe( data._descriptor.wireframe );
            conePrimitive->fromCone( data._descriptor );
            conePrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    void GFXDevice::debugDrawFrustum( const I64 ID, const IM::FrustumDescriptor descriptor ) noexcept
    {
        _debugFrustums.add( ID, descriptor );
    }

    void GFXDevice::debugDrawFrustums( GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        LockGuard<Mutex> r_lock( _debugFrustums._dataLock );

        const size_t frustumCount = _debugFrustums.size();
        for ( size_t f = 0u; f < frustumCount; ++f )
        {
            auto& data = _debugFrustums._debugData[f];
            if ( data._frameLifeTime == 0u )
            {
                continue;
            }

            IMPrimitive*& frustumPrimitive = _debugFrustums._debugPrimitives[f];
            if ( frustumPrimitive == nullptr )
            {
                frustumPrimitive = newIMP( Util::StringFormat( "DebugFrustum_%d", f ).c_str() );
                frustumPrimitive->setPipelineDescriptor( getDebugPipeline( data._descriptor ) );
            }

            frustumPrimitive->forceWireframe( data._descriptor.wireframe );
            frustumPrimitive->fromFrustum( data._descriptor );
            frustumPrimitive->getCommandBuffer( data._descriptor.worldMatrix, bufferInOut, memCmdInOut );
        }
    }

    /// Render all of our immediate mode primitives. This isn't very optimised and most are recreated per frame!
    void GFXDevice::debugDraw( [[maybe_unused]] const SceneRenderState& sceneRenderState, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        debugDrawFrustums( bufferInOut, memCmdInOut );
        debugDrawLines( bufferInOut, memCmdInOut );
        debugDrawBoxes( bufferInOut, memCmdInOut );
        debugDrawOBBs( bufferInOut, memCmdInOut );
        debugDrawSpheres( bufferInOut, memCmdInOut );
        debugDrawCones( bufferInOut, memCmdInOut );
    }
#pragma endregion

#pragma region GPU Object instantiation
    RenderTarget_uptr GFXDevice::newRT( const RenderTargetDescriptor& descriptor )
    {
        RenderTarget_uptr ret = _api->newRT(descriptor);

        if ( ret != nullptr )
        {
            const bool valid = ret->create();
            DIVIDE_ASSERT( valid );
            return ret;
        }

        return nullptr;
    }

    IMPrimitive* GFXDevice::newIMP( const Str<64>& name )
    {
        LockGuard<Mutex> w_lock( _imprimitiveMutex );
        return s_IMPrimitivePool.newElement( *this, name );
    }

    bool GFXDevice::destroyIMP( IMPrimitive*& primitive )
    {
        if ( primitive != nullptr )
        {
            LockGuard<Mutex> w_lock( _imprimitiveMutex );
            s_IMPrimitivePool.deleteElement( primitive );
            primitive = nullptr;
            return true;
        }

        return false;
    }

    Pipeline* GFXDevice::newPipeline( const PipelineDescriptor& descriptor )
    {
        // Pipeline with no shader is no pipeline at all
        DIVIDE_ASSERT( descriptor._shaderProgramHandle != SHADER_INVALID_HANDLE, "Missing shader handle during pipeline creation!" );
        DIVIDE_ASSERT( descriptor._primitiveTopology != PrimitiveTopology::COUNT, "Missing primitive topology during pipeline creation!" );

        const size_t hash = GetHash( descriptor );

        LockGuard<Mutex> lock( _pipelineCacheLock );
        const hashMap<size_t, Pipeline, NoHash<size_t>>::iterator it = _pipelineCache.find( hash );
        if ( it == std::cend( _pipelineCache ) )
        {
            return &insert( _pipelineCache, hash, Pipeline( descriptor ) ).first->second;
        }

        return &it->second;
    }

    void DestroyIMP(IMPrimitive*& primitive)
    {
        if (primitive != nullptr) {
            primitive->context().destroyIMP(primitive);
        }
        primitive = nullptr;
    }

    VertexBuffer_ptr GFXDevice::newVB(const bool renderIndirect, const Str<256>& name )
    {
        return std::make_shared<VertexBuffer>( *this, renderIndirect, name );
    }
#pragma endregion

    ShaderComputeQueue& GFXDevice::shaderComputeQueue() noexcept
    {
        assert( _shaderComputeQueue != nullptr );
        return *_shaderComputeQueue;
    }

    const ShaderComputeQueue& GFXDevice::shaderComputeQueue() const noexcept
    {
        assert( _shaderComputeQueue != nullptr );
        return *_shaderComputeQueue;
    }

    /// Extract the pixel data from the main render target's first colour attachment and save it as a TGA image
    void GFXDevice::screenshot( const ResourcePath& filename, GFX::CommandBuffer& bufferInOut ) const
    {
        const RenderTarget* screenRT = _rtPool->getRenderTarget( RenderTargetNames::BACK_BUFFER );
        auto readTextureCmd = GFX::EnqueueCommand<GFX::ReadTextureCommand>( bufferInOut );
        readTextureCmd->_texture = screenRT->getAttachment(RTAttachmentType::COLOUR, ScreenTargets::ALBEDO )->texture().get();
        readTextureCmd->_pixelPackAlignment._alignment = 1u;
        readTextureCmd->_callback = [filename]( const ImageReadbackData& data )
        {
            if ( !data._data.empty() )
            {
                DIVIDE_ASSERT(data._bpp > 0u && data._numComponents > 0u);
                // Make sure we have a valid target directory
                if ( !createDirectory( Paths::g_screenshotPath ) )
                {
                    NOP();
                }

                // Save to file
                if ( !ImageTools::SaveImage( ResourcePath( Util::StringFormat( "%s/%s_Date_%s.png", Paths::g_screenshotPath.c_str(), filename.c_str(), CurrentDateTimeString().c_str() )),
                                             data._width,
                                             data._height,
                                             data._numComponents,
                                             data._bpp,
                                             data._sourceIsBGR,
                                             data._data.data(),
                                             ImageTools::SaveImageFormat::PNG ) )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
        };
    }
};
