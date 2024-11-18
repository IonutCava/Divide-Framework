

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
#include "Managers/Headers/ProjectManager.h"

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


        struct RenderThread
        {
            std::thread _thread;
            std::condition_variable _cv;
            std::atomic_bool _running{false};
            bool _hasWork{false};
            Mutex _lock;
            DELEGATE<void> _work;

            eastl::queue<DisplayWindow*> _windows;
        };

        static RenderThread s_renderThread;
    };

    D64 GFXDevice::s_interpolationFactor = 1.0;
    U64 GFXDevice::s_frameCount = 0u;

    DeviceInformation GFXDevice::s_deviceInformation{};
    GFXDevice::IMPrimitivePool GFXDevice::s_IMPrimitivePool{};

    PerPassUtils RenderTargetNames::UTILS = {};
    PerPassUtils RenderTargetNames::REFLECT::UTILS = {};
    PerPassUtils RenderTargetNames::REFRACT::UTILS = {};

    RenderTargetID RenderTargetNames::BACK_BUFFER = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SCREEN = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SCREEN_PREV = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::NORMALS_RESOLVED = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SSAO_RESULT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::BLOOM_RESULT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::SSR_RESULT = INVALID_RENDER_TARGET_ID;
    RenderTargetID RenderTargetNames::OIT = INVALID_RENDER_TARGET_ID;

    std::array<RenderTargetID, Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW>  RenderTargetNames::REFLECT::PLANAR     = create_array<Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);
    std::array<RenderTargetID, Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW>  RenderTargetNames::REFLECT::PLANAR_OIT = create_array<Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);
    std::array<RenderTargetID, Config::MAX_REFLECTIVE_CUBE_NODES_IN_VIEW>    RenderTargetNames::REFLECT::CUBE       = create_array<Config::MAX_REFLECTIVE_CUBE_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);

    std::array<RenderTargetID, Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW> RenderTargetNames::REFRACT::PLANAR     = create_array<Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);
    std::array<RenderTargetID, Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW> RenderTargetNames::REFRACT::PLANAR_OIT = create_array<Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);
    std::array<RenderTargetID, Config::MAX_REFRACTIVE_CUBE_NODES_IN_VIEW>   RenderTargetNames::REFRACT::CUBE       = create_array<Config::MAX_REFRACTIVE_CUBE_NODES_IN_VIEW, RenderTargetID>(INVALID_RENDER_TARGET_ID);

    ImShaders::ImShaders()
    {
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
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imShader = CreateResource( immediateModeShader );
        }
        {
            shaderDescriptor._globalDefines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation-NoTexture", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imShaderNoTexture = CreateResource( immediateModeShader );
        }
        {
            efficient_clear( shaderDescriptor._globalDefines );
            shaderDescriptor._modules.back()._defines.emplace_back( "WORLD_PASS" );
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation-World", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imWorldShader = CreateResource( immediateModeShader );
        }
        {
            shaderDescriptor._globalDefines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation-World-NoTexture", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imWorldShaderNoTexture = CreateResource( immediateModeShader );
        }


        {
            efficient_clear( shaderDescriptor._globalDefines );
            shaderDescriptor._modules.back()._defines.emplace_back( "OIT_PASS" );
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation-OIT", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imWorldOITShader = CreateResource( immediateModeShader );
        }
        {
            shaderDescriptor._modules.back()._defines.emplace_back( "NO_TEXTURE" );
            ResourceDescriptor<ShaderProgram> immediateModeShader( "ImmediateModeEmulation-OIT-NoTexture", shaderDescriptor );
            immediateModeShader.waitForReady( true );
            _imWorldOITShaderNoTexture = CreateResource( immediateModeShader );
        }
    }

    ImShaders::~ImShaders()
    {
        DestroyResource( _imShader );
        DestroyResource( _imShaderNoTexture );
        DestroyResource( _imWorldShader );
        DestroyResource( _imWorldShaderNoTexture );
        DestroyResource( _imWorldOITShader );
        DestroyResource( _imWorldOITShaderNoTexture );
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
        ShaderProgram::RegisterSetLayoutBinding( DescriptorSetUsage::PER_FRAME, 3,  DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER, ShaderStageVisibility::FRAGMENT );             // UNUSED;
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
        : PlatformContextComponent( context )
        , FrameListener( "GFXDevice", context.kernel().frameListenerMgr(), 1u)
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
                _api = std::make_unique<GL_API>( *this );
            } break;
            case RenderAPI::Vulkan:
            {
                _api = std::make_unique<VK_API>( *this );
            } break;
            case RenderAPI::None:
            {
                _api = std::make_unique<NONE_API>( *this );
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
                        refreshRates.append( Util::StringFormat( ", {}", it._maxRefreshRate ) );
                    }
                }
            }
            if ( !refreshRates.empty() )
            {
                printMode();
            }
        }

        _rtPool = std::make_unique<GFXRTPool>( *this );

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


        hardwareState = ShaderProgram::OnStartup( context() );
        if ( hardwareState == ErrorCode::NO_ERR )
        {
            hardwareState = initDescriptorSets();
        }

        if ( hardwareState == ErrorCode::NO_ERR)
        {
            s_renderThread._running = true;
            s_renderThread._thread  = std::thread( &GFXDevice::renderThread, this );
        }

        return hardwareState;
    }

    void GFXDevice::renderThread()
    {
        SetThreadName("Main render thread");
        onThreadCreated( std::this_thread::get_id(), true );

        while ( s_renderThread._running )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
            UniqueLock<Mutex> lock( s_renderThread._lock );
            while( !s_renderThread._hasWork && s_renderThread._running )
            {
                s_renderThread._cv.wait( lock, []() { return s_renderThread._hasWork || !s_renderThread._running; } );
            }

            if ( s_renderThread._running )
            {
                s_renderThread._work();
            }

            s_renderThread._work = {};
            s_renderThread._hasWork = false;
        }
    }

    void GFXDevice::addRenderWork( DELEGATE<void>&& work )
    {
        {
            LockGuard<Mutex> lck( s_renderThread._lock );
            s_renderThread._work = MOV(work);
            s_renderThread._hasWork = true;
        }
        s_renderThread._cv.notify_one();
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
            bufferDescriptor._bufferParams._usageType = BufferUsageType::CONSTANT_BUFFER;
            bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
            bufferDescriptor._ringBufferLength = to_U16( targetSizeCam );
            bufferDescriptor._bufferParams._elementSize = sizeof( GFXShaderData::CamData );
            bufferDescriptor._initialData = { (Byte*)&_gpuBlock._camData, bufferDescriptor._bufferParams._elementSize };

            for ( U8 i = 0u; i < GFXBuffers::PER_FRAME_BUFFER_COUNT; ++i )
            {
                Util::StringFormat( bufferDescriptor._name, "DVD_GPU_CAM_DATA_{}", i );
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
            bufferDescriptor._bufferParams._elementSize = 4 * sizeof( U32 );
            bufferDescriptor._bufferParams._usageType = BufferUsageType::UNBOUND_BUFFER;
            bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
            bufferDescriptor._bufferParams._hostVisible = true;
            bufferDescriptor._separateReadWrite = true;
            bufferDescriptor._initialData = { (bufferPtr)&VECTOR4_ZERO._v[0], 4 * sizeof( U32 ) };
            for ( U8 i = 0u; i < GFXBuffers::PER_FRAME_BUFFER_COUNT; ++i )
            {
                Util::StringFormat( bufferDescriptor._name, "CULL_COUNTER_{}", i );
                _gfxBuffers._perFrameBuffers[i]._cullCounter = newSB( bufferDescriptor );
            }
        }
        _gpuBlock._camNeedsUpload = true;
    }

    ErrorCode GFXDevice::postInitRenderingAPI( const vec2<U16> renderResolution )
    {
        std::atomic_uint loadTasks = 0;
        const Configuration& config = context().config();

        IMPrimitive::InitStaticData();
        ShaderProgram::InitStaticData();
        Texture::OnStartup( *this );
        RenderPassExecutor::OnStartup( *this );

        resizeGPUBlocks( TargetBufferSizeCam, Config::MAX_FRAMES_IN_FLIGHT + 1u );

        _shaderComputeQueue = std::make_unique<ShaderComputeQueue>();

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
        TextureDescriptor depthDescriptor{};
        depthDescriptor._dataType = GFXDataFormat::FLOAT_32;
        depthDescriptor._baseFormat = GFXImageFormat::RED;
        depthDescriptor._packing = GFXImagePacking::DEPTH;
        depthDescriptor._mipMappingState = MipMappingState::OFF;

        TextureDescriptor velocityDescriptor{};
        velocityDescriptor._dataType = GFXDataFormat::FLOAT_16;
        velocityDescriptor._baseFormat = GFXImageFormat::RG;
        velocityDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        velocityDescriptor._mipMappingState = MipMappingState::OFF;

        //RG - packed normal, B - roughness, A - unused
        TextureDescriptor normalsDescriptor{};
        normalsDescriptor._dataType = GFXDataFormat::FLOAT_16;
        normalsDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        normalsDescriptor._mipMappingState =  MipMappingState::OFF;

        //MainPass
        TextureDescriptor screenDescriptor{};
        screenDescriptor._dataType = GFXDataFormat::FLOAT_16;
        screenDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        screenDescriptor._mipMappingState = MipMappingState::OFF;
        AddImageUsageFlag( screenDescriptor, ImageUsage::SHADER_READ);

        TextureDescriptor materialDescriptor{};
        materialDescriptor._dataType = GFXDataFormat::FLOAT_16;
        materialDescriptor._baseFormat = GFXImageFormat::RG;
        materialDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        materialDescriptor._mipMappingState = MipMappingState::OFF;

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
            RenderTargetNames::SCREEN = _rtPool->allocateRT(screenDesc)._targetID;

            auto& screenAttachment = attachments[to_base( ScreenTargets::ALBEDO )];
            screenAttachment._texDescriptor._mipMappingState = MipMappingState::MANUAL;
            AddImageUsageFlag( screenAttachment._texDescriptor, ImageUsage::SHADER_READ);
            screenAttachment._sampler = defaultSamplerMips;
            screenDesc._msaaSamples = 0u;
            screenDesc._name = "Screen Prev";
            screenDesc._attachments = { screenAttachment };

            RenderTargetNames::SCREEN_PREV = _rtPool->allocateRT( screenDesc )._targetID;

            screenDesc._name = "Screen Blur";
            RenderTargetNames::UTILS.BLUR  = _rtPool->allocateRT( screenDesc )._targetID;

            auto& normalAttachment = attachments[to_base( ScreenTargets::NORMALS )];
            normalAttachment._slot = RTColourAttachmentSlot::SLOT_0;
            normalAttachment._texDescriptor._mipMappingState = MipMappingState::OFF ;
            AddImageUsageFlag( normalAttachment._texDescriptor, ImageUsage::SHADER_READ );
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
            TextureDescriptor backBufferDescriptor{};
            backBufferDescriptor._mipMappingState = MipMappingState::OFF;
            AddImageUsageFlag( backBufferDescriptor, ImageUsage::SHADER_READ );
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
            // Need bilinear filtering with edge clamping for down/up sampling
            // No mip sampling as we handle this manually
            SamplerDescriptor bloomSampler{};

            bloomSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
            bloomSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
            bloomSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
            bloomSampler._minFilter = TextureFilter::LINEAR;
            bloomSampler._magFilter = TextureFilter::LINEAR;
            bloomSampler._mipSampling = TextureMipSampling::NONE;

            // Use a floating point RGB mip-chain for down/up sampling
            TextureDescriptor bloomDescriptor{};
            bloomDescriptor._dataType = GFXDataFormat::FLOAT_16;
            bloomDescriptor._packing = GFXImagePacking::RGB_111110F;
            bloomDescriptor._baseFormat = GFXImageFormat::RGB;
            bloomDescriptor._mipMappingState = MipMappingState::MANUAL;

            RenderTargetDescriptor bloomDesc = {};
            bloomDesc._attachments =
            {
                InternalRTAttachmentDescriptor{ bloomDescriptor, bloomSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0}
            };

            bloomDesc._name = "Bloom Result";
            bloomDesc._resolution = renderResolution * 0.5f;
            bloomDesc._msaaSamples = 0u;
            RenderTargetNames::BLOOM_RESULT = _rtPool->allocateRT( bloomDesc )._targetID;
        }  
        {
            TextureDescriptor ssaoDescriptor{};
            ssaoDescriptor._dataType = GFXDataFormat::FLOAT_16;
            ssaoDescriptor._baseFormat = GFXImageFormat::RED;
            ssaoDescriptor._packing = GFXImagePacking::UNNORMALIZED;
            ssaoDescriptor._mipMappingState = MipMappingState::OFF;

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
            TextureDescriptor ssrDescriptor{};
            ssrDescriptor._dataType = GFXDataFormat::FLOAT_16;
            ssrDescriptor._packing = GFXImagePacking::UNNORMALIZED;
            ssrDescriptor._mipMappingState = MipMappingState::OFF;

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
        const U32 refractRes = nextPOW2( CLAMPED( to_U32( config.rendering.reflectionPlaneResolution ), 16u, 4096u ) - 1u );

        TextureDescriptor hiZDescriptor{};
        hiZDescriptor._dataType = GFXDataFormat::FLOAT_32;
        hiZDescriptor._baseFormat = GFXImageFormat::RED;
        hiZDescriptor._packing = GFXImagePacking::UNNORMALIZED;
        hiZDescriptor._mipMappingState = MipMappingState::MANUAL;
        AddImageUsageFlag( hiZDescriptor, ImageUsage::SHADER_WRITE );

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
            RenderTargetNames::UTILS.HI_Z = _rtPool->allocateRT( hizRTDesc )._targetID;

            hizRTDesc._resolution.set( reflectRes, reflectRes );
            hizRTDesc._name = "HiZ_Reflect";
            RenderTargetNames::REFLECT::UTILS.HI_Z = _rtPool->allocateRT( hizRTDesc )._targetID;

            hizRTDesc._resolution.set(refractRes, refractRes);
            hizRTDesc._name = "HiZ_Refract";
            RenderTargetNames::REFRACT::UTILS.HI_Z = _rtPool->allocateRT(hizRTDesc)._targetID;
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
            TextureDescriptor environmentDescriptorPlanar{};
            environmentDescriptorPlanar._mipMappingState = MipMappingState::MANUAL;

            TextureDescriptor depthDescriptorPlanar{};
            depthDescriptorPlanar._dataType = GFXDataFormat::UNSIGNED_INT;
            depthDescriptorPlanar._baseFormat = GFXImageFormat::RED;
            depthDescriptorPlanar._packing = GFXImagePacking::DEPTH;
            depthDescriptorPlanar._mipMappingState = MipMappingState::OFF;

            {
                RenderTargetDescriptor refDesc = {};
                refDesc._attachments = 
                {
                    InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
                    InternalRTAttachmentDescriptor{ depthDescriptorPlanar,       reflectionSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
                };

                refDesc._resolution = vec2<U16>( reflectRes );
                for ( U32 i = 0; i < Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW; ++i )
                {
                    Util::StringFormat( refDesc._name, "Reflection_Planar_{}", i );
                    RenderTargetNames::REFLECT::PLANAR[i] = _rtPool->allocateRT( refDesc )._targetID;
                }

                refDesc._resolution = vec2<U16>(refractRes);
                for ( U32 i = 0; i < Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW; ++i )
                {
                    Util::StringFormat( refDesc._name, "Refraction_Planar_{}", i );
                    RenderTargetNames::REFRACT::PLANAR[i] = _rtPool->allocateRT( refDesc )._targetID;
                }

                environmentDescriptorPlanar._mipMappingState = MipMappingState::OFF;
                refDesc._attachments =
                {//skip depth
                    InternalRTAttachmentDescriptor{ environmentDescriptorPlanar, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 }
                };

                refDesc._name = "Reflection_blur";
                refDesc._resolution = vec2<U16>(reflectRes);
                RenderTargetNames::REFLECT::UTILS.BLUR = _rtPool->allocateRT(refDesc)._targetID;

                refDesc._resolution = vec2<U16>(refractRes);
                refDesc._name = "Refraction_blur";
                RenderTargetNames::REFRACT::UTILS.BLUR = _rtPool->allocateRT(refDesc)._targetID;
            }
        }
        {
            TextureDescriptor environmentDescriptorCube{};
            environmentDescriptorCube._texType = TextureType::TEXTURE_CUBE_ARRAY;
            environmentDescriptorCube._mipMappingState = MipMappingState::OFF;

            TextureDescriptor depthDescriptorCube{};
            depthDescriptorCube._texType = TextureType::TEXTURE_CUBE_ARRAY;
            depthDescriptorCube._dataType = GFXDataFormat::UNSIGNED_INT;
            depthDescriptorCube._baseFormat = GFXImageFormat::RED;
            depthDescriptorCube._packing = GFXImagePacking::DEPTH;

            depthDescriptorCube._mipMappingState = MipMappingState::OFF;

            RenderTargetDescriptor refDesc = {};
            refDesc._attachments =
            {
                InternalRTAttachmentDescriptor{ environmentDescriptorCube, reflectionSampler, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0 },
                InternalRTAttachmentDescriptor{ depthDescriptorCube,       reflectionSampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 },
            };

            refDesc._resolution = vec2<U16>(reflectRes);
            for (U32 i = 0; i < Config::MAX_REFLECTIVE_CUBE_NODES_IN_VIEW; ++i)
            {
                Util::StringFormat(refDesc._name, "Reflection_Cube_{}", i);
                RenderTargetNames::REFLECT::CUBE[i] = _rtPool->allocateRT(refDesc)._targetID;
            }

            refDesc._resolution = vec2<U16>(refractRes);
            for (U32 i = 0; i < Config::MAX_REFRACTIVE_CUBE_NODES_IN_VIEW; ++i)
            {
                Util::StringFormat(refDesc._name, "Refraction_Cube_{}", i);
                RenderTargetNames::REFRACT::CUBE[i] = _rtPool->allocateRT(refDesc)._targetID;
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

            TextureDescriptor accumulationDescriptor{};
            accumulationDescriptor._dataType = GFXDataFormat::FLOAT_16;
            accumulationDescriptor._packing = GFXImagePacking::UNNORMALIZED;
            accumulationDescriptor._mipMappingState = MipMappingState::OFF;

            //R = revealage
            TextureDescriptor revealageDescriptor{};
            revealageDescriptor._dataType = GFXDataFormat::FLOAT_16;
            revealageDescriptor._baseFormat = GFXImageFormat::RED;
            revealageDescriptor._packing = GFXImagePacking::UNNORMALIZED;
            revealageDescriptor._mipMappingState = MipMappingState::OFF;

            InternalRTAttachmentDescriptors oitAttachments
            {
                InternalRTAttachmentDescriptor{ accumulationDescriptor, accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::ACCUMULATION, false },
                InternalRTAttachmentDescriptor{ revealageDescriptor,    accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::REVEALAGE, false },
                InternalRTAttachmentDescriptor{ normalsDescriptor,      accumulationSampler, RTAttachmentType::COLOUR, ScreenTargets::NORMALS, false },
            };

            RenderTargetDescriptor oitDesc = {};
            oitDesc._attachments = oitAttachments;

            {
                const RenderTarget* screenTarget    = _rtPool->getRenderTarget( RenderTargetNames::SCREEN );
                RTAttachment* screenAttachment      = screenTarget->getAttachment( RTAttachmentType::COLOUR, ScreenTargets::ALBEDO );
                RTAttachment* screenDepthAttachment = screenTarget->getAttachment( RTAttachmentType::DEPTH );

                ExternalRTAttachmentDescriptors externalAttachments
                {
                    ExternalRTAttachmentDescriptor{ screenAttachment,  screenAttachment->_descriptor._sampler, RTAttachmentType::COLOUR, ScreenTargets::MODULATE },
                    ExternalRTAttachmentDescriptor{ screenDepthAttachment, screenDepthAttachment->_descriptor._sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0, false }
                };

                oitDesc._name = "OIT";
                oitDesc._resolution = renderResolution;
                oitDesc._externalAttachments = externalAttachments;
                oitDesc._msaaSamples = config.rendering.MSAASamples;
                RenderTargetNames::OIT = _rtPool->allocateRT( oitDesc )._targetID;
            }
            {

                oitDesc._msaaSamples = 0;
                oitDesc._resolution = vec2<U16>(reflectRes);
                for ( U16 i = 0u; i < Config::MAX_REFLECTIVE_PLANAR_NODES_IN_VIEW; ++i )
                {
                    const RenderTarget* reflectTarget = _rtPool->getRenderTarget( RenderTargetNames::REFLECT::PLANAR[i] );
                    RTAttachment* screenAttachment = reflectTarget->getAttachment( RTAttachmentType::COLOUR );
                    RTAttachment* depthAttachment = reflectTarget->getAttachment( RTAttachmentType::DEPTH );

                    ExternalRTAttachmentDescriptors externalAttachments
                    {
                        ExternalRTAttachmentDescriptor{ screenAttachment, screenAttachment->_descriptor._sampler, RTAttachmentType::COLOUR, ScreenTargets::MODULATE },
                        ExternalRTAttachmentDescriptor{ depthAttachment,  depthAttachment->_descriptor._sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
                    };

                    Util::StringFormat( oitDesc._name, "OIT_REFLECT_PLANAR_{}", i );
                    oitDesc._externalAttachments = externalAttachments;
                    RenderTargetNames::REFLECT::PLANAR_OIT[i] = _rtPool->allocateRT( oitDesc )._targetID;
                }

                oitDesc._resolution = vec2<U16>(refractRes);
                for (U16 i = 0u; i < Config::MAX_REFRACTIVE_PLANAR_NODES_IN_VIEW; ++i)
                {
                    const RenderTarget* refractTarget = _rtPool->getRenderTarget(RenderTargetNames::REFRACT::PLANAR[i]);
                    RTAttachment* screenAttachment = refractTarget->getAttachment(RTAttachmentType::COLOUR);
                    RTAttachment* depthAttachment  = refractTarget->getAttachment(RTAttachmentType::DEPTH);

                    ExternalRTAttachmentDescriptors externalAttachments
                    {
                        ExternalRTAttachmentDescriptor{ screenAttachment, screenAttachment->_descriptor._sampler, RTAttachmentType::COLOUR, ScreenTargets::MODULATE },
                        ExternalRTAttachmentDescriptor{ depthAttachment,  depthAttachment->_descriptor._sampler, RTAttachmentType::DEPTH, RTColourAttachmentSlot::SLOT_0 }
                    };

                    Util::StringFormat(oitDesc._name, "OIT_REFRACT_PLANAR_{}", i);
                    oitDesc._externalAttachments = externalAttachments;
                    RenderTargetNames::REFRACT::PLANAR_OIT[i] = _rtPool->allocateRT(oitDesc)._targetID;
                }
            }
        }
        {
            ShaderModuleDescriptor compModule = {};
            compModule._moduleType = ShaderType::COMPUTE;
            compModule._defines.emplace_back( Util::StringFormat( "LOCAL_SIZE {}", DEPTH_REDUCE_LOCAL_SIZE ) );
            compModule._defines.emplace_back( "imageSizeIn PushData0[0].xy");
            compModule._defines.emplace_back( "imageSizeOut PushData0[0].zw");
            compModule._defines.emplace_back( "wasEven (uint(PushData0[1].x) == 1u)");

            compModule._sourceFile = "HiZConstruct.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( compModule );

            // Initialized our HierarchicalZ construction shader (takes a depth attachment and down-samples it for every mip level)
            ResourceDescriptor<ShaderProgram> descriptor1( "HiZConstruct", shaderDescriptor );
            descriptor1.waitForReady( false );
            _hIZConstructProgram = CreateResource( descriptor1, loadTasks );

            PipelineDescriptor pipelineDesc{};
            pipelineDesc._shaderProgramHandle = _hIZConstructProgram;
            pipelineDesc._primitiveTopology = PrimitiveTopology::COMPUTE;
            pipelineDesc._stateBlock = getNoDepthTestBlock();
            _hIZPipeline = newPipeline( pipelineDesc );
        }
        {
            ShaderModuleDescriptor compModule = {};
            compModule._moduleType = ShaderType::COMPUTE;
            compModule._defines.emplace_back( Util::StringFormat( "WORK_GROUP_SIZE {}", GROUP_SIZE_AABB ) );
            compModule._sourceFile = "HiZOcclusionCull.glsl";

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( compModule );

            ResourceDescriptor<ShaderProgram> descriptor2( "HiZOcclusionCull", shaderDescriptor );
            descriptor2.waitForReady( false );

            _hIZCullProgram = CreateResource( descriptor2, loadTasks );

            PipelineDescriptor pipelineDescriptor = {};
            pipelineDescriptor._shaderProgramHandle = _hIZCullProgram;
            pipelineDescriptor._primitiveTopology = PrimitiveTopology::COMPUTE;
            _hIZCullPipeline = newPipeline( pipelineDescriptor );
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

            ResourceDescriptor<ShaderProgram> previewRTShader( "fbPreview", shaderDescriptor );
            previewRTShader.waitForReady( true );

            _renderTargetDraw = CreateResource( previewRTShader, loadTasks );
            _previewRenderTargetColour = _renderTargetDraw;
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

            ResourceDescriptor<ShaderProgram> previewReflectionRefractionDepth( "fbPreviewLinearDepthScenePlanes", shaderDescriptor );
            previewReflectionRefractionDepth.waitForReady( false );
            _previewRenderTargetDepth = CreateResource( previewReflectionRefractionDepth, loadTasks );
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

                ResourceDescriptor<ShaderProgram> blur( "BoxBlur_Single", shaderDescriptorSingle );
                _blurBoxShaderSingle = CreateResource( blur, loadTasks );

                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateBlock = get2DStateBlock();
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                pipelineDescriptor._shaderProgramHandle = _blurBoxShaderSingle;
                _blurBoxPipelineSingleCmd._pipeline = newPipeline( pipelineDescriptor );
            }
            {
                ShaderProgramDescriptor shaderDescriptorLayered = {};
                shaderDescriptorLayered._modules.push_back( blurVertModule );
                shaderDescriptorLayered._modules.push_back( fragModule );
                shaderDescriptorLayered._modules.back()._variant += ".Layered";
                shaderDescriptorLayered._modules.back()._defines.emplace_back( "LAYERED" );

                ResourceDescriptor<ShaderProgram> blur( "BoxBlur_Layered", shaderDescriptorLayered );
                _blurBoxShaderLayered = CreateResource( blur, loadTasks );

                PipelineDescriptor pipelineDescriptor;
                pipelineDescriptor._stateBlock = get2DStateBlock();
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                pipelineDescriptor._shaderProgramHandle = _blurBoxShaderLayered;
                _blurBoxPipelineLayeredCmd._pipeline = newPipeline( pipelineDescriptor );
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

                    ResourceDescriptor<ShaderProgram> blur( "GaussBlur_Single", shaderDescriptorSingle );
                    _blurGaussianShaderSingle = CreateResource( blur, loadTasks );

                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateBlock = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = _blurGaussianShaderSingle;
                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;
                    _blurGaussianPipelineSingleCmd._pipeline = newPipeline( pipelineDescriptor );
                }
                {
                    ShaderProgramDescriptor shaderDescriptorLayered = {};
                    shaderDescriptorLayered._modules.push_back( blurVertModule );
                    shaderDescriptorLayered._modules.push_back( geomModule );
                    shaderDescriptorLayered._modules.push_back( fragModule );
                    shaderDescriptorLayered._modules.back()._variant += ".Layered";
                    shaderDescriptorLayered._modules.back()._defines.emplace_back( "LAYERED" );
                    shaderDescriptorLayered._globalDefines.emplace_back( Util::StringFormat( "GS_MAX_INVOCATIONS {}", MAX_INVOCATIONS_BLUR_SHADER_LAYERED ) );

                    ResourceDescriptor<ShaderProgram> blur( "GaussBlur_Layered", shaderDescriptorLayered );
                    _blurGaussianShaderLayered = CreateResource( blur, loadTasks );

                    PipelineDescriptor pipelineDescriptor;
                    pipelineDescriptor._stateBlock = get2DStateBlock();
                    pipelineDescriptor._shaderProgramHandle = _blurGaussianShaderLayered;
                    pipelineDescriptor._primitiveTopology = PrimitiveTopology::POINTS;

                    _blurGaussianPipelineLayeredCmd._pipeline = newPipeline( pipelineDescriptor );
                }
            }
            // Create an immediate mode rendering shader that simulates the fixed function pipeline
            _imShaders = std::make_unique<ImShaders>();
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

            ShaderProgramDescriptor shaderDescriptor = {};
            shaderDescriptor._modules.push_back( vertModule );
            shaderDescriptor._modules.push_back( fragModule );

            {
                ResourceDescriptor<ShaderProgram> descriptor3( "display", shaderDescriptor );
                _displayShader = CreateResource( descriptor3, loadTasks );
            
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateBlock = get2DStateBlock();
                pipelineDescriptor._shaderProgramHandle = _displayShader;
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                _drawFSTexturePipelineCmd._pipeline = newPipeline( pipelineDescriptor );

                BlendingSettings& blendState = pipelineDescriptor._blendStates._settings[0];
                blendState.enabled( true );
                blendState.blendSrc( BlendProperty::SRC_ALPHA );
                blendState.blendDest( BlendProperty::INV_SRC_ALPHA );
                blendState.blendOp( BlendOperation::ADD );
                _drawFSTexturePipelineBlendCmd._pipeline = newPipeline( pipelineDescriptor );
            }
            {
                shaderDescriptor._modules.back()._defines.emplace_back( "DEPTH_ONLY" );
                ResourceDescriptor<ShaderProgram> descriptor3( "display_depth", shaderDescriptor );

                _depthShader = CreateResource( descriptor3, loadTasks );
                PipelineDescriptor pipelineDescriptor = {};
                pipelineDescriptor._stateBlock = _stateDepthOnlyRendering;
                pipelineDescriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
                pipelineDescriptor._shaderProgramHandle = _depthShader;

                _drawFSDepthPipelineCmd._pipeline = newPipeline( pipelineDescriptor );
            }
        }

        context().paramHandler().setParam<bool>( _ID( "rendering.previewDebugViews" ), false );
        {
            // Create general purpose render state blocks
            RenderStateBlock primitiveStateBlock{};

            PipelineDescriptor pipelineDesc;
            pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;
            pipelineDesc._shaderProgramHandle = _imShaders->imWorldShaderNoTexture();
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

        _renderer = std::make_unique<Renderer>( context() );

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

        _sceneData = std::make_unique<SceneShaderData>( *this );

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

        if ( s_renderThread._running )
        {
            {
                LockGuard<Mutex> lock( s_renderThread._lock );
                s_renderThread._hasWork = false;
                s_renderThread._running = false;
            }
            s_renderThread._cv.notify_one();
            s_renderThread._thread.join();
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

        _rtPool.reset();

        DestroyResource(_previewDepthMapShader );
        DestroyResource(_previewRenderTargetColour );
        DestroyResource(_previewRenderTargetDepth );
        DestroyResource(_renderTargetDraw );
        DestroyResource(_hIZConstructProgram );
        DestroyResource(_hIZCullProgram );
        DestroyResource(_displayShader );
        DestroyResource(_depthShader );
        DestroyResource(_blurBoxShaderSingle );
        DestroyResource(_blurBoxShaderLayered );
        DestroyResource(_blurGaussianShaderSingle );
        DestroyResource(_blurGaussianShaderLayered );

        _imShaders.reset();
        _gfxBuffers.reset( true, true );
        _sceneData.reset();
        // Close the shader manager
        _shaderComputeQueue.reset();
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
            for ( const auto& [type, guid, nameHash] : _graphicResources )
            {
                list.append( Util::StringFormat("\n{}_{}_{},", TypeUtil::GraphicResourceTypeToName( type ), guid, nameHash) );
            }
            list.pop_back();
            list.append("\n ]");

            Console::errorfn( LOCALE_STR( "ERROR_GFX_LEAKED_RESOURCES" ), _graphicResources.size() );
            Console::errorfn( list.c_str() );
        }
        _graphicResources.clear();
    }

    void GFXDevice::onThreadCreated( const std::thread::id& threadID, const bool isMainRenderThread ) const
    {
        _api->onThreadCreated( threadID, isMainRenderThread );
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
        if ( !_api->drawToWindow( window ) )
        {
            NOP();
        }
    }

    void GFXDevice::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        _api->prepareFlushWindow( window );
        const vec2<U16> windowDimensions = window.getDrawableSize();
        setViewport( { 0, 0, windowDimensions.width, windowDimensions.height } );
        setScissor( { 0, 0, windowDimensions.width, windowDimensions.height } );

        {
            LockGuard<Mutex> w_lock( _queuedCommandbufferLock );
            GFX::CommandBufferQueue& queue = window.getCurrentCommandBufferQueue();
            for ( const GFX::CommandBufferQueue::Entry& entry : queue._commandBuffers )
            {
                flushCommandBufferInternal( entry._buffer );
            }
            ResetCommandBufferQueue(queue);
        }
        _api->flushWindow( window );
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

        ShaderProgram::OnBeginFrame( *this );

        if ( _api->frameStarted() )
        {
            _context.app().windowManager().drawToWindow(context().mainWindow());
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

            Handle<GFX::CommandBuffer> bufferHandle = GFX::AllocateCommandBuffer("Blit Backbuffer");
            GFX::CommandBuffer* buffer = GFX::Get(bufferHandle);

            GFX::BeginRenderPassCommand beginRenderPassCmd{};
            beginRenderPassCmd._target = SCREEN_TARGET_ID;
            beginRenderPassCmd._name = "Blit Backbuffer";
            beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { DefaultColours::BLACK, true };
            beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
            GFX::EnqueueCommand( *buffer, beginRenderPassCmd );

            const auto& screenAtt = renderTargetPool().getRenderTarget( RenderTargetNames::BACK_BUFFER )->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO );
            const auto& texData = Get(screenAtt->texture())->getView();

            drawTextureInViewport( texData, screenAtt->_descriptor._sampler, context().mainWindow().renderingViewport(), false, false, false, *buffer );

            GFX::EnqueueCommand<GFX::EndRenderPassCommand>( *buffer );
            flushCommandBuffer( MOV(bufferHandle) );
        }

        context().app().windowManager().flushWindow();


        /*while ( !s_renderThread._windows.empty() )
        {
            DisplayWindow* window = s_renderThread._windows.front();

            _api->onRenderThreadLoopStart();

            ///addRenderWork( [&]()
            {
                _api->prepareFlushWindow( *window );
                {
                    LockGuard<Mutex> w_lock( _queuedCommandbufferLock );
                    GFX::CommandBufferQueue& queue = window->getCurrentCommandBufferQueue();
                    for ( const GFX::CommandBufferQueue::Entry& entry : queue._commandBuffers )
                    {
                        flushCommandBufferInternal( *window, entry._buffer );
                    }
                    ResetCommandBufferQueue( queue );
                }
                _api->flushWindow( *window );
            }
            ///);

            s_renderThread._windows.pop();
        }*/

        frameDrawCallsPrev( frameDrawCalls() );
        frameDrawCalls( 0u );

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
        
        if (!_api->frameEnded())
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        {
            PROFILE_SCOPE( "Lifetime updates", Profiler::Category::Graphics );

            DecrementPrimitiveLifetime( _debugLines );
            DecrementPrimitiveLifetime( _debugBoxes );
            DecrementPrimitiveLifetime( _debugOBBs );
            DecrementPrimitiveLifetime( _debugFrustums );
            DecrementPrimitiveLifetime( _debugCones );
            DecrementPrimitiveLifetime( _debugSpheres );

            _performanceMetrics._scratchBufferQueueUsage[0] = to_U32( _gfxBuffers.crtBuffers()._camWritesThisFrame );
            _performanceMetrics._scratchBufferQueueUsage[1] = to_U32( _gfxBuffers.crtBuffers()._renderWritesThisFrame );
            ++s_frameCount;
        }
 
        return true;
    }
#pragma endregion

#pragma region Utility functions
    /// Generate a cube texture and store it in the provided RenderTarget
    void GFXDevice::generateCubeMap( RenderPassParams& params,
                                     const U16 arrayOffset,
                                     const float3& pos,
                                     const float2 zPlanes,
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
            isValidFB = IsCubeTexture( Get(cubeMapTarget->getAttachment( RTAttachmentType::COLOUR )->texture())->descriptor()._texType );
        }
        else if ( cubeMapTarget->hasAttachment( RTAttachmentType::DEPTH ) )
        {
            // We don't have a colour attachment, so we require a cube map depth attachment
            isValidFB = IsCubeTexture( Get(cubeMapTarget->getAttachment( RTAttachmentType::DEPTH )->texture())->descriptor()._texType );
        }

        // Make sure we have a proper render target to draw to
        if ( !isValidFB )
        {
            // Future formats must be added later (e.g. cube map arrays)
            Console::errorfn( LOCALE_STR( "ERROR_GFX_DEVICE_INVALID_FB_CUBEMAP" ) );
            return;
        }

        static const float3 CameraDirections[] = { WORLD_X_AXIS,  WORLD_X_NEG_AXIS, WORLD_Y_AXIS,  WORLD_Y_NEG_AXIS,  WORLD_Z_AXIS,  WORLD_Z_NEG_AXIS };
        static const float3 CameraUpVectors[] =  { WORLD_Y_AXIS,  WORLD_Y_AXIS,     WORLD_Z_AXIS,  WORLD_Z_NEG_AXIS,  WORLD_Y_AXIS,  WORLD_Y_AXIS     };
        constexpr const char* PassNames[] =         { "CUBEMAP_X+",  "CUBEMAP_X-",     "CUBEMAP_Y+",  "CUBEMAP_Y-",      "CUBEMAP_Z+",  "CUBEMAP_Z-"     };

        DIVIDE_ASSERT( cubeMapTarget->getWidth() == cubeMapTarget->getHeight());

        auto& passMgr = context().kernel().renderPassManager();

        Camera* camera = Camera::GetUtilityCamera( Camera::UtilityCamera::CUBE );

        // For each of the environment's faces (TOP, DOWN, NORTH, SOUTH, EAST, WEST)
        for ( U8 i = 0u; i < 6u; ++i )
        {
            params._passName = PassNames[i];
            params._stagePass._pass = static_cast<RenderStagePass::PassIndex>(i);

            const DrawLayerEntry layer
            {
                ._layer = {
                    ._offset = arrayOffset,
                    ._count = 1
                },
                ._cubeFace = i
            };

            // Draw to the current cubemap face
            params._targetDescriptorPrePass._writeLayers[RT_DEPTH_ATTACHMENT_IDX] = layer;
            params._targetDescriptorPrePass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )] = layer;
            params._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX] = layer;
            params._targetDescriptorMainPass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )] = layer;

            // Set a 90 degree horizontal FoV perspective projection
            camera->setProjection( 1.f, Angle::to_VerticalFoV( Angle::DEGREES_F( 90.f ), 1.f ), zPlanes );
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
                                               const float3& pos,
                                               const float2 zPlanes,
                                               GFX::CommandBuffer& bufferInOut,
                                               GFX::MemoryBarrierCommand& memCmdInOut,
                                               mat4<F32>* viewProjectionOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

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
            isValidFB = IsArrayTexture( Get(colourAttachment->texture())->descriptor()._texType );
        }
        else
        {
            RTAttachment* depthAttachment = paraboloidTarget->getAttachment( RTAttachmentType::DEPTH );
            // We don't have a colour attachment, so we require a cube map depth attachment
            isValidFB = hasDepth && IsArrayTexture( Get(depthAttachment->texture())->descriptor()._texType );
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
        auto& passMgr = context().kernel().renderPassManager();

        Camera* camera = Camera::GetUtilityCamera( Camera::UtilityCamera::DUAL_PARABOLOID );

        for ( U8 i = 0u; i < 2u; ++i )
        {
            const U16 layer = arrayOffset + i;

            params._targetDescriptorPrePass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset = layer;
            params._targetDescriptorPrePass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )]._layer._offset = layer;
            params._targetDescriptorMainPass._writeLayers[RT_DEPTH_ATTACHMENT_IDX]._layer._offset = layer;
            params._targetDescriptorMainPass._writeLayers[to_base( RTColourAttachmentSlot::SLOT_0 )]._layer._offset = layer;

            // Point our camera to the correct face
            camera->lookAt( pos, pos + (i == 0 ? WORLD_Z_NEG_AXIS : WORLD_Z_AXIS) * zPlanes.y );
            // Set a 180 degree vertical FoV perspective projection
            camera->setProjection( to_F32( aspect ), Angle::to_VerticalFoV( Angle::DEGREES_F( 180.0f ), aspect ), zPlanes );
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
            pushData.data[0]._vec[0].yz = float2( blurBuffer._rt->getResolution() );
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
            Set( binding._data, inputAttachment->texture(), inputAttachment->_descriptor._sampler );

    
            if ( !gaussian && layerCount > 1 )
            {
                pushData.data[0]._vec[0].x = 0.f;
            }

            for ( U8 loop = 0u; loop < loopCount; ++loop )
            {
                if ( !gaussian && loop > 0u )
                {
                    pushData.data[0]._vec[0].x = to_F32( loop );
                    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut)->_fastData = pushData;
                }
                GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            }

            GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        }
        {// Blur vertically
            pushData.data[0]._vec[0].x = 0.f;
            pushData.data[0]._vec[1].x = 1.f;
            pushData.data[0]._vec[0].yz = float2( blurTarget._rt->getResolution() );
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
            Set( binding._data, bufferAttachment->texture(), bufferAttachment->_descriptor._sampler );

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData = pushData;

            for ( U8 loop = 0u; loop < loopCount; ++loop )
            {
                if ( !gaussian && loop > 0u )
                {
                    pushData.data[0]._vec[0].x = to_F32( loop );
                    GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_fastData = pushData;
                }
                GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
            }

            GFX::EnqueueCommand( bufferInOut, GFX::EndRenderPassCommand{} );
        }
    }
#pragma endregion

#pragma region Resolution, viewport and window management

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
        if ( params.isMainWindow && !fitViewportInWindow( params.width, params.height ) )
        {
            NOP();
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
        const Angle::DEGREES_F vFoV = Angle::to_VerticalFoV( Angle::DEGREES_F(config.runtime.horizontalFOV), to_D64( aspectRatio ) );
        const float2 zPlanes( Camera::s_minNearZ, config.runtime.cameraViewDistance );

        // Update the 2D camera so it matches our new rendering viewport
        if ( Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->setProjection( float4( 0, to_F32( w ), 0, to_F32( h ) ), float2( -1, 1 ) ) )
        {
            Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->updateLookAt();
        }
        if ( Camera::GetUtilityCamera( Camera::UtilityCamera::_2D_FLIP_Y )->setProjection( float4( 0, to_F32( w ), to_F32( h ), 0 ), float2( -1, 1 ) ) )
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
        _rtPool->getRenderTarget( RenderTargetNames::UTILS.HI_Z )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::UTILS.BLUR )->resize( w, h );
        _rtPool->getRenderTarget( RenderTargetNames::OIT )->resize( w, h );

        _rtPool->getRenderTarget( RenderTargetNames::BLOOM_RESULT )->resize( w / 2, h / 2);

        // Update post-processing render targets and buffers
        _renderer->updateResolution( w, h );
        _renderingResolution.set( w, h );

        if (!fitViewportInWindow( w, h ))
        {
            NOP();
        }
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

        return COMPARE(newAspectRatio, currentAspectRatio);
    }
#pragma endregion

#pragma region GPU State

    bool GFXDevice::uploadGPUBlock()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Put the viewport update here as it is the most common source of gpu data invalidation and not always
        // needed for rendering (e.g. changed by RenderTarget::End())

        const float4 tempViewport{ activeViewport() };
        if ( _gpuBlock._camData._viewPort != tempViewport )
        {
            _gpuBlock._camData._viewPort.set( tempViewport );
            const U32 clustersX = to_U32( CEIL( to_F32( tempViewport.sizeX ) / Config::Lighting::ClusteredForward::CLUSTERS_X ) );
            const U32 clustersY = to_U32( CEIL( to_F32( tempViewport.sizeY ) / Config::Lighting::ClusteredForward::CLUSTERS_Y ) );
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

    void GFXDevice::setDepthRange( const float2 depthRange )
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

    void GFXDevice::setPreviousViewProjectionMatrix( const PlayerIndex index, const mat4<F32>& prevViewMatrix, const mat4<F32> prevProjectionMatrix )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );


        bool projectionDirty = false, viewDirty = false;

        GFXShaderData::PrevFrameData& frameData = _gpuBlock._prevFrameData[index];

        if ( frameData._previousViewMatrix != prevViewMatrix )
        {
            frameData._previousViewMatrix = prevViewMatrix;
            viewDirty = true;
        }
        if ( frameData._previousProjectionMatrix != prevProjectionMatrix )
        {
            frameData._previousProjectionMatrix = prevProjectionMatrix;
            projectionDirty = true;
        }

        if ( projectionDirty || viewDirty )
        {
            mat4<F32>::Multiply( frameData._previousProjectionMatrix, frameData._previousViewMatrix, frameData._previousViewProjectionMatrix );
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

    const GFXShaderData::PrevFrameData& GFXDevice::previousFrameData( const PlayerIndex index ) const noexcept
    {
        return _gpuBlock._prevFrameData[index];
    }
#pragma endregion

#pragma region Command buffers, occlusion culling, etc
    void GFXDevice::validateAndUploadDescriptorSets()
    {
        if (!uploadGPUBlock())
        {
            NOP();
        }

        thread_local DescriptorSetEntries setEntries{};

        constexpr DescriptorSetUsage prioritySorted[to_base( DescriptorSetUsage::COUNT )]
        {
            DescriptorSetUsage::PER_FRAME,
            DescriptorSetUsage::PER_PASS,
            DescriptorSetUsage::PER_BATCH,
            DescriptorSetUsage::PER_DRAW
        };

        for ( const DescriptorSetUsage usage : prioritySorted )
        {
            GFXDescriptorSet& set = _descriptorSets[to_base( usage )];
            DescriptorSetEntry& entry = setEntries[to_base(usage)];
            entry._set = &set.impl();
            entry._usage = usage;
            entry._isDirty = set.dirty();
            set.dirty( false );
        }

        _api->bindShaderResources( setEntries );
    }

    void GFXDevice::flushCommandBuffer( Handle<GFX::CommandBuffer>&& commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        LockGuard<Mutex> w_lock( _queuedCommandbufferLock );
        GFX::CommandBufferQueue& queue = context().activeWindow().getCurrentCommandBufferQueue();
        AddCommandBufferToQueue( queue, MOV(commandBuffer) );
    }

    void GFXDevice::flushCommandBufferInternal( Handle<GFX::CommandBuffer> commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const Rect<I32> initialViewport = activeViewport();
        const Rect<I32> initialScissor = activeScissor();

        _api->preFlushCommandBuffer( commandBuffer );

        const GFX::CommandBuffer::CommandList& commands = GFX::Get(commandBuffer)->commands();
        for ( GFX::CommandBase* cmd : commands )
        {
            if ( IsSubmitCommand( cmd->type() ) )
            {
                validateAndUploadDescriptorSets();
            }

            switch ( cmd->type() )
            {
                case GFX::CommandType::READ_BUFFER_DATA:
                {
                    PROFILE_SCOPE( "READ_BUFFER_DATA", Profiler::Category::Graphics );

                    const GFX::ReadBufferDataCommand& crtCmd = *cmd->As<GFX::ReadBufferDataCommand>();
                    crtCmd._buffer->readData( { crtCmd._offsetElementCount, crtCmd._elementCount }, crtCmd._target );
                } break;
                case GFX::CommandType::CLEAR_BUFFER_DATA:
                {
                    PROFILE_SCOPE( "CLEAR_BUFFER_DATA", Profiler::Category::Graphics );

                    const GFX::ClearBufferDataCommand& crtCmd = *cmd->As<GFX::ClearBufferDataCommand>();
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

                    setViewport( cmd->As<GFX::SetViewportCommand>()->_viewport );
                } break;
                case GFX::CommandType::PUSH_VIEWPORT:
                {
                    PROFILE_SCOPE( "PUSH_VIEWPORT", Profiler::Category::Graphics );

                    const GFX::PushViewportCommand* crtCmd = cmd->As<GFX::PushViewportCommand>();
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
                    setScissor( cmd->As<GFX::SetScissorCommand>()->_rect );
                } break;
                case GFX::CommandType::SET_CAMERA:
                {
                    PROFILE_SCOPE( "SET_CAMERA", Profiler::Category::Graphics );

                    const GFX::SetCameraCommand* crtCmd = cmd->As<GFX::SetCameraCommand>();
                    // Tell the Rendering API to draw from our desired PoV
                    renderFromCamera( crtCmd->_cameraSnapshot );
                } break;
                case GFX::CommandType::PUSH_CAMERA:
                {
                    PROFILE_SCOPE( "PUSH_CAMERA", Profiler::Category::Graphics );

                    const GFX::PushCameraCommand* crtCmd = cmd->As<GFX::PushCameraCommand>();
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

                    setClipPlanes( cmd->As<GFX::SetClipPlanesCommand>()->_clippingPlanes );
                } break;
                case GFX::CommandType::BIND_SHADER_RESOURCES:
                {
                    PROFILE_SCOPE( "BIND_SHADER_RESOURCES", Profiler::Category::Graphics );

                    const auto resCmd = cmd->As<GFX::BindShaderResourcesCommand>();
                    descriptorSet( resCmd->_usage ).update( resCmd->_usage, resCmd->_set );

                } break;
                case GFX::CommandType::DRAW_COMMANDS:
                {
                    DIVIDE_ASSERT(!cmd->As<GFX::DrawCommand>()->_drawCommands.empty());
                } break;
                default: break;
            }

            _api->flushCommand( cmd );
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
    std::pair<Handle<Texture>, SamplerDescriptor> GFXDevice::constructHIZ( RenderTargetID depthBuffer, RenderTargetID HiZTarget, GFX::CommandBuffer& cmdBufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        assert( depthBuffer != HiZTarget );

        const RTAttachment* SrcAtt = _rtPool->getRenderTarget( depthBuffer )->getAttachment( RTAttachmentType::DEPTH );
        const RTAttachment* HiZAtt = _rtPool->getRenderTarget( HiZTarget )->getAttachment( RTAttachmentType::COLOUR );
        ResourcePtr<Texture> HiZTex = Get(HiZAtt->texture());
        DIVIDE_ASSERT( HiZTex->descriptor()._mipMappingState == MipMappingState::MANUAL );

        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( cmdBufferInOut )->_scopeName = "Construct Hi-Z";
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( cmdBufferInOut )->_pipeline = _hIZPipeline;

        U32 twidth = HiZTex->width();
        U32 theight = HiZTex->height();
        bool wasEven = false;
        U32 owidth = twidth;
        U32 oheight = theight;

        for ( U16 i = 0u; i < HiZTex->mipCount(); ++i )
        {
            twidth = twidth < 1u ? 1u : twidth;
            theight = theight < 1u ? 1u : theight;

            ImageView outImage = HiZTex->getView( { i, 1u } );

            const ImageView inImage = i == 0u ? Get(SrcAtt->texture())->getView( ) 
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

            PushConstantsStruct& pushConstants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( cmdBufferInOut )->_fastData;
            pushConstants.data[0]._vec[0].set( owidth, oheight, twidth, theight );
            pushConstants.data[0]._vec[1].x = wasEven ? 1.f : 0.f;

            GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( cmdBufferInOut )->_workGroupSize =
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

    void GFXDevice::occlusionCull( const RenderPass::PassData& passData,
                                   const Handle<Texture> hizBuffer,
                                   const SamplerDescriptor sampler,
                                   const CameraSnapshot& cameraSnapshot,
                                   const bool countCulledNodes,
                                   GFX::CommandBuffer& bufferInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const U32 cmdCount = *passData._lastCommandCount;
        const U32 threadCount = getGroupCount( cmdCount, GROUP_SIZE_AABB );

        if ( threadCount == 0u || !enableOcclusionCulling() )
        {
            GFX::EnqueueCommand<GFX::AddDebugMessageCommand>( bufferInOut )->_msg = "Occlusion Culling Skipped";
            return;
        }

        ResourcePtr<Texture> hizTex = Get(hizBuffer);

        ShaderBuffer* cullBuffer = _gfxBuffers.crtBuffers()._cullCounter.get();
        GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Occlusion Cull";

        // Not worth the overhead for a handful of items and the Pre-Z pass should handle overdraw just fine
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = _hIZCullPipeline;
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, hizTex->getView(), sampler );
        }
        {
            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_PASS;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, 7u, ShaderStageVisibility::COMPUTE );
            Set( binding._data, cullBuffer, { 0u, 1u });
        }

        passData._uniforms->set( _ID( "dvd_countCulledItems" ), PushConstantType::UINT, countCulledNodes ? 1u : 0u );
        passData._uniforms->set( _ID( "dvd_numEntities" ), PushConstantType::UINT, cmdCount );
        passData._uniforms->set( _ID( "dvd_viewSize" ), PushConstantType::VEC2, float2( hizTex->width(), hizTex->height() ) );
        passData._uniforms->set( _ID( "dvd_frustumPlanes" ), PushConstantType::VEC4, cameraSnapshot._frustumPlanes );

        auto pushConstantsCmd = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut );

        pushConstantsCmd->_uniformData = passData._uniforms;

        PushConstantsStruct& fastConstants = pushConstantsCmd->_fastData;
        mat4<F32>::Multiply( cameraSnapshot._projectionMatrix, cameraSnapshot._viewMatrix, fastConstants.data[0] );
        fastConstants.data[1] = cameraSnapshot._viewMatrix;

        GFX::EnqueueCommand<GFX::DispatchShaderTaskCommand>( bufferInOut )->_workGroupSize = { threadCount, 1, 1 };

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
        GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
        GFX::EnqueueCommand( bufferInOut, drawToDepthOnly ? _drawFSDepthPipelineCmd : drawBlend ? _drawFSTexturePipelineBlendCmd : _drawFSTexturePipelineCmd );

        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );
        Set( binding._data, texture, sampler );

        GFX::EnqueueCommand<GFX::PushViewportCommand>( bufferInOut )->_viewport = viewport;

        if ( !drawToDepthOnly )
        {
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut)->_fastData.data[0]._vec[0].x = convertToSrgb ? 1.f : 0.f;
        }

        GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();
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
            GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>( bufferInOut )->_scopeName = "Render Debug Views";

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
        if ( _previewDepthMapShader != INVALID_HANDLE<ShaderProgram>)
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
            _previewDepthMapShader = CreateResource( ResourceDescriptor<ShaderProgram>( "fbPreviewLinearDepth", shaderDescriptor ) );

            DebugView_ptr HiZ = std::make_shared<DebugView>();
            HiZ->_shader = _renderTargetDraw;
            HiZ->_texture = renderTargetPool().getRenderTarget( RenderTargetNames::UTILS.HI_Z )->getAttachment( RTAttachmentType::COLOUR )->texture();
            HiZ->_sampler = renderTargetPool().getRenderTarget( RenderTargetNames::UTILS.HI_Z )->getAttachment( RTAttachmentType::COLOUR )->_descriptor._sampler;
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
            DepthPreview->_shaderData.set( _ID( "_zPlanes" ), PushConstantType::VEC2, float2( Camera::s_minNearZ, _context.config().runtime.cameraViewDistance ) );

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
            
            DebugView_ptr BloomPreview = std::make_shared<DebugView>();
            BloomPreview->_shader = _renderTargetDraw;
            BloomPreview->_texture = renderTargetPool().getRenderTarget(RenderTargetNames::BLOOM_RESULT)->getAttachment(RTAttachmentType::COLOUR)->texture();
            BloomPreview->_sampler = renderTargetPool().getRenderTarget(RenderTargetNames::BLOOM_RESULT)->getAttachment(RTAttachmentType::COLOUR)->_descriptor._sampler;
            BloomPreview->_name = "BLOOM Map";
            BloomPreview->_shaderData.set(_ID("lodLevel"), PushConstantType::FLOAT, 0.0f);
            BloomPreview->_shaderData.set(_ID("channelsArePacked"), PushConstantType::BOOL, false);
            BloomPreview->_shaderData.set(_ID("startChannel"), PushConstantType::UINT, 0u);
            BloomPreview->_shaderData.set(_ID("channelCount"), PushConstantType::UINT, 3u);
            BloomPreview->_shaderData.set( _ID( "multiplier" ), PushConstantType::FLOAT, 1.0f );

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
            addDebugView( BloomPreview );
            addDebugView( AlphaAccumulationHigh );
            addDebugView( AlphaRevealageHigh );
            addDebugView( Luminance );
            addDebugView( Edges );

            WAIT_FOR_CONDITION( Get(_previewDepthMapShader)->getState() == ResourceState::RES_LOADED );
        }
    }

    void GFXDevice::renderDebugViews( const Rect<I32> targetViewport, const I32 padding, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        static size_t labelStyleHash = TextLabelStyle( Font::DROID_SERIF_BOLD, UColour4( 196 ), 96 ).getHash();

        thread_local vector<std::tuple<string, I32, Rect<I32>>> labelStack;

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
        pipelineDesc._shaderProgramHandle = INVALID_HANDLE<ShaderProgram>;
        pipelineDesc._primitiveTopology = PrimitiveTopology::TRIANGLES;

        const Rect<I32> previousViewport = activeViewport();

        Pipeline* crtPipeline = nullptr;
        U16 idx = 0u;
        const I32 mipTimer = to_I32( CEIL( Time::App::ElapsedMilliseconds() / 750.0f ) );
        for ( U16 i = 0; i < to_U16( _debugViews.size() ); ++i )
        {
            if ( !_debugViews[i]->_enabled )
            {
                continue;
            }

            const DebugView_ptr& view = _debugViews[i];

            if ( view->_cycleMips )
            {
                const F32 lodLevel = to_F32( mipTimer % Get(view->_texture)->mipCount() );
                view->_shaderData.set( _ID( "lodLevel" ), PushConstantType::FLOAT, lodLevel );
                labelStack.emplace_back( Util::StringFormat( "Mip level: {}", to_U8( lodLevel ) ), viewport.sizeY * 4, viewport );
            }
            const Handle<ShaderProgram> crtShader = pipelineDesc._shaderProgramHandle;
            const Handle<ShaderProgram> newShader = view->_shader;

            if ( crtShader != newShader )
            {
                pipelineDesc._shaderProgramHandle = view->_shader;
                crtPipeline = newPipeline( pipelineDesc );
            }

            GFX::EnqueueCommand<GFX::BindPipelineCommand>( bufferInOut )->_pipeline = crtPipeline;
            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( bufferInOut )->_uniformData = &view->_shaderData;
            GFX::EnqueueCommand<GFX::SetViewportCommand>( bufferInOut )->_viewport.set( viewport );

            auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>( bufferInOut );
            cmd->_usage = DescriptorSetUsage::PER_DRAW;
            DescriptorSetBinding& binding = AddBinding( cmd->_set, view->_textureBindSlot, ShaderStageVisibility::FRAGMENT );
            Set( binding._data, view->_texture, view->_sampler );

            GFX::EnqueueCommand<GFX::DrawCommand>( bufferInOut )->_drawCommands.emplace_back();

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

        GFX::EnqueueCommand<GFX::PushCameraCommand>( bufferInOut )->_cameraSnapshot = Camera::GetUtilityCamera( Camera::UtilityCamera::_2D )->snapshot();
        // Draw labels at the end to reduce number of state changes
        TextElement text( labelStyleHash, RelativePosition2D{ ._x = RelativeValue{ ._scale = 0.1f, ._offset = 0.0f }, ._y = RelativeValue{ ._scale = 0.1f, ._offset = 0.0f } } );
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
                linePrimitive = newIMP( Util::StringFormat( "DebugLine_{}", f ).c_str() );
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
                boxPrimitive = newIMP( Util::StringFormat( "DebugBox_{}", f ).c_str() );
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
                boxPrimitive = newIMP( Util::StringFormat( "DebugOBB_{}", f ).c_str() );
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
                spherePrimitive = newIMP( Util::StringFormat( "DebugSphere_{}", f ).c_str() );
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
                conePrimitive = newIMP( Util::StringFormat( "DebugCone_{}", f ).c_str() );
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
                frustumPrimitive = newIMP( Util::StringFormat( "DebugFrustum_{}", f ).c_str() );
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

    IMPrimitive* GFXDevice::newIMP( const std::string_view name )
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
        DIVIDE_ASSERT( descriptor._shaderProgramHandle != INVALID_HANDLE<ShaderProgram>, "Missing shader handle during pipeline creation!" );
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
        if (primitive != nullptr && !primitive->context().destroyIMP(primitive) )
        {
            DebugBreak();
        }
        primitive = nullptr;
    }

    VertexBuffer_ptr GFXDevice::newVB( const VertexBuffer::Descriptor& descriptor )
    {
        return std::make_shared<VertexBuffer>( *this, descriptor );
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
    void GFXDevice::screenshot( const std::string_view fileName, GFX::CommandBuffer& bufferInOut ) const
    {
        const RenderTarget* screenRT = _rtPool->getRenderTarget( RenderTargetNames::BACK_BUFFER );
        auto readTextureCmd = GFX::EnqueueCommand<GFX::ReadTextureCommand>( bufferInOut );
        readTextureCmd->_texture = screenRT->getAttachment(RTAttachmentType::COLOUR, ScreenTargets::ALBEDO )->texture();
        readTextureCmd->_pixelPackAlignment._alignment = 1u;
        readTextureCmd->_callback = [fileName]( const ImageReadbackData& data )
        {
            if ( !data._data.empty() )
            {
                DIVIDE_ASSERT(data._bpp > 0u && data._numComponents > 0u);
                // Make sure we have a valid target directory
                if ( createDirectory( Paths::g_screenshotPath ) != FileError::NONE )
                {
                    NOP();
                }

                // Save to file
                if ( !ImageTools::SaveImage( ResourcePath( Util::StringFormat( "{}/{}_Date_{}.png", Paths::g_screenshotPath, fileName, CurrentDateTimeString().c_str() )),
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
