﻿

#include "Headers/GLWrapper.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Configuration.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"

#include "GUI/Headers/GUI.h"

#include "Platform/Video/Headers/DescriptorSets.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"

#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glSamplerObject.h"

#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glShaderBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glGenericVertexData.h"
#include <glbinding/Binding.h>

namespace Divide
{

    NO_DESTROY GLStateTracker GL_API::s_stateTracker;
    std::atomic_bool GL_API::s_glFlushQueued{false};
    NO_DESTROY GLUtil::glTextureViewCache GL_API::s_textureViewCache{};
    U32 GL_API::s_fenceSyncCounter[GL_API::s_LockFrameLifetime]{};
    SharedMutex GL_API::s_samplerMapLock;
    NO_DESTROY GL_API::SamplerObjectMap GL_API::s_samplerMap{};
    std::unique_ptr<glHardwareQueryPool> GL_API::s_hardwareQueryPool = nullptr;
    NO_DESTROY eastl::fixed_vector<GL_API::TexBindEntry, GLStateTracker::MAX_BOUND_TEXTURE_UNITS, false> GL_API::s_TexBindQueue;

    NO_DESTROY std::array<GLUtil::GLMemory::DeviceAllocator, to_base( GLUtil::GLMemory::GLMemoryType::COUNT )> GL_API::s_memoryAllocators = 
    {
        GLUtil::GLMemory::DeviceAllocator( GLUtil::GLMemory::GLMemoryType::SHADER_BUFFER ),
        GLUtil::GLMemory::DeviceAllocator( GLUtil::GLMemory::GLMemoryType::UNIFORM_BUFFER ),
        GLUtil::GLMemory::DeviceAllocator( GLUtil::GLMemory::GLMemoryType::VERTEX_BUFFER ),
        GLUtil::GLMemory::DeviceAllocator( GLUtil::GLMemory::GLMemoryType::INDEX_BUFFER ),
        GLUtil::GLMemory::DeviceAllocator( GLUtil::GLMemory::GLMemoryType::OTHER )
    };

    NO_DESTROY std::array<size_t, to_base( GLUtil::GLMemory::GLMemoryType::COUNT )> GL_API::s_memoryAllocatorSizes
    {{
        Bytes::Mega(1 << 10),
        Bytes::Mega(1 << 8),
        Bytes::Mega(1 << 9),
        Bytes::Mega(1 << 7),
        Bytes::Mega(1 << 7 )
    }};

    constexpr bool g_breakOnGLCall = false;

    namespace
    {
        struct SDLContextEntry
        {
            SDL_GLContext _context = nullptr;
            bool _inUse = false;
        };

        struct ContextPool
        {
            bool init( const size_t size, const DisplayWindow& window )
            {
                SDL_Window* raw = window.getRawWindow();
                _contexts.resize( size );
                GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ) );
                for ( SDLContextEntry& contextEntry : _contexts )
                {
                    contextEntry._context = SDL_GL_CreateContext( raw );
                }
                GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), nullptr ) );
                return true;
            }

            bool destroy() noexcept
            {
                for ( const SDLContextEntry& contextEntry : _contexts )
                {
                    SDL_GL_DeleteContext( contextEntry._context );
                }
                _contexts.clear();
                return true;
            }

            bool getAvailableContext( SDL_GLContext& ctx ) noexcept
            {
                assert( !_contexts.empty() );
                for ( SDLContextEntry& contextEntry : _contexts )
                {
                    if ( !contextEntry._inUse )
                    {
                        ctx = contextEntry._context;
                        contextEntry._inUse = true;
                        return true;
                    }
                }

                return false;
            }

            vector<SDLContextEntry> _contexts;
        } g_ContextPool;
    }

    GL_API::GL_API( GFXDevice& context )
        : RenderAPIWrapper(),
        _context( context ),
        _swapBufferTimer( Time::ADD_TIMER( "Swap Buffer Timer" ) )
    {
    }

    /// Try and create a valid OpenGL context taking in account the specified resolution and command line arguments
    ErrorCode GL_API::initRenderingAPI( [[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, Configuration& config )
    {
        const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();
        GLUtil::s_glMainRenderWindow = &window;

        g_ContextPool.init( Kernel::TotalThreadCount( TaskPoolType::RENDERER ) + Kernel::TotalThreadCount( TaskPoolType::ASSET_LOADER ), window );

        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ) );
        glbinding::Binding::initialize( []( const char* proc ) noexcept
        {
            return (glbinding::ProcAddress)SDL_GL_GetProcAddress( proc );
        }, false );

        // OpenGL has a nifty error callback system, available in every build configuration if required
        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            if ( (config.debug.renderer.enableRenderAPIDebugging || config.debug.renderer.enableRenderAPIBestPractices) )
            {
                // GL_DEBUG_OUTPUT_SYNCHRONOUS is essential for debugging gl commands in the IDE
                gl46core::glEnable( gl46core::GL_DEBUG_OUTPUT );
                gl46core::glEnable( gl46core::GL_DEBUG_OUTPUT_SYNCHRONOUS );
                // hard-wire our debug callback function with OpenGL's implementation
                gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_MARKER,     gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_PUSH_GROUP, gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_POP_GROUP,  gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                if ( !config.debug.renderer.enableRenderAPIBestPractices )
                {
                    gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                    gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_PORTABILITY,         gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                    gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_PERFORMANCE,         gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
                }
                gl46core::glDebugMessageCallback( (gl46core::GLDEBUGPROC)GLUtil::DebugCallback, nullptr );
            }
        }


        if ( SDL_GL_GetCurrentContext() == nullptr )
        {
            return ErrorCode::GLBINGING_INIT_ERROR;
        }

        if constexpr ( g_breakOnGLCall )
        {
            glbinding::Binding::setCallbackMaskExcept( glbinding::CallbackMask::Before, { "glGetError" } );
            glbinding::Binding::setBeforeCallback( [&]( const glbinding::FunctionCall& )
            {
                DebugBreak();
            });
        }

        GLUtil::OnStartup();

        // Query GPU vendor to enable/disable vendor specific features
        GPUVendor vendor = GPUVendor::COUNT;
        const char* gpuVendorStr = reinterpret_cast<const char*>(gl46core::glGetString( gl46core::GL_VENDOR ));
        if ( gpuVendorStr != nullptr )
        {
            if ( strstr( gpuVendorStr, "Intel" ) != nullptr )
            {
                vendor = GPUVendor::INTEL;
            }
            else if ( strstr( gpuVendorStr, "NVIDIA" ) != nullptr )
            {
                vendor = GPUVendor::NVIDIA;
            }
            else if ( strstr( gpuVendorStr, "ATI" ) != nullptr || strstr( gpuVendorStr, "AMD" ) != nullptr )
            {
                vendor = GPUVendor::AMD;
            }
            else if ( strstr( gpuVendorStr, "Microsoft" ) != nullptr )
            {
                vendor = GPUVendor::MICROSOFT;
            }
            else if ( strstr( gpuVendorStr, "Mesa" ) != nullptr )
            {
                vendor = GPUVendor::MESA;
            }
            else
            {
                vendor = GPUVendor::OTHER;
            }
        }
        else
        {
            gpuVendorStr = "Unknown GPU Vendor";
            vendor = GPUVendor::OTHER;
        }
        GPURenderer renderer = GPURenderer::COUNT;
        const char* gpuRendererStr = reinterpret_cast<const char*>(gl46core::glGetString( gl46core::GL_RENDERER ));
        if ( gpuRendererStr != nullptr )
        {
            if ( strstr( gpuRendererStr, "Tegra" ) || strstr( gpuRendererStr, "GeForce" ) || strstr( gpuRendererStr, "NV" ) )
            {
                renderer = GPURenderer::GEFORCE;
            }
            else if ( strstr( gpuRendererStr, "PowerVR" ) || strstr( gpuRendererStr, "Apple" ) )
            {
                renderer = GPURenderer::POWERVR;
                vendor = GPUVendor::IMAGINATION_TECH;
            }
            else if ( strstr( gpuRendererStr, "Mali" ) )
            {
                renderer = GPURenderer::MALI;
                vendor = GPUVendor::ARM;
            }
            else if ( strstr( gpuRendererStr, "Adreno" ) )
            {
                renderer = GPURenderer::ADRENO;
                vendor = GPUVendor::QUALCOMM;
            }
            else if ( strstr( gpuRendererStr, "AMD" ) || strstr( gpuRendererStr, "ATI" ) )
            {
                renderer = GPURenderer::RADEON;
            }
            else if ( strstr( gpuRendererStr, "Intel" ) )
            {
                renderer = GPURenderer::INTEL;
            }
            else if ( strstr( gpuRendererStr, "Vivante" ) )
            {
                renderer = GPURenderer::VIVANTE;
                vendor = GPUVendor::VIVANTE;
            }
            else if ( strstr( gpuRendererStr, "VideoCore" ) )
            {
                renderer = GPURenderer::VIDEOCORE;
                vendor = GPUVendor::ALPHAMOSAIC;
            }
            else if ( strstr( gpuRendererStr, "WebKit" ) || strstr( gpuRendererStr, "Mozilla" ) || strstr( gpuRendererStr, "ANGLE" ) )
            {
                renderer = GPURenderer::WEBGL;
                vendor = GPUVendor::WEBGL;
            }
            else if ( strstr( gpuRendererStr, "GDI Generic" ) )
            {
                renderer = GPURenderer::GDI;
            }
            else if ( strstr( gpuRendererStr, "Mesa" ) )
            {
                renderer = GPURenderer::SOFTWARE;
            }
            else
            {
                renderer = GPURenderer::UNKNOWN;
            }
        }
        else
        {
            gpuRendererStr = "Unknown GPU Renderer";
            renderer = GPURenderer::UNKNOWN;
        }
        // GPU info, including vendor, gpu and driver
        Console::printfn( LOCALE_STR( "GL_VENDOR_STRING" ), gpuVendorStr, gpuRendererStr, reinterpret_cast<const char*>(gl46core::glGetString( gl46core::GL_VERSION )) );

        DeviceInformation deviceInformation{};
        deviceInformation._vendor = vendor;
        deviceInformation._renderer = renderer;

        if ( s_hardwareQueryPool == nullptr )
        {
            s_hardwareQueryPool = std::make_unique<glHardwareQueryPool>( _context );
        }

        // If we got here, let's figure out what capabilities we have available
        // Maximum addressable texture image units in the fragment shader
        deviceInformation._maxTextureUnits = CLAMPED( GLUtil::getGLValue( gl46core::GL_MAX_TEXTURE_IMAGE_UNITS ), 16, 255 );
        DIVIDE_ASSERT( deviceInformation._maxTextureUnits >= GLStateTracker::MAX_BOUND_TEXTURE_UNITS );

        GLUtil::getGLValue( gl46core::GL_MAX_VERTEX_ATTRIB_BINDINGS, deviceInformation._maxVertAttributeBindings );

        GLUtil::getGLValue( gl46core::GL_MAX_TEXTURE_SIZE, deviceInformation._maxTextureSize );

        deviceInformation._versionInfo._major = to_U8( GLUtil::getGLValue( gl46core::GL_MAJOR_VERSION ) );
        deviceInformation._versionInfo._minor = to_U8( GLUtil::getGLValue( gl46core::GL_MINOR_VERSION ) );
        Console::printfn( LOCALE_STR( "GL_MAX_VERSION" ), deviceInformation._versionInfo._major, deviceInformation._versionInfo._minor );

        if ( deviceInformation._versionInfo._major < 4 || (deviceInformation._versionInfo._major == 4 && deviceInformation._versionInfo._minor < 6) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_OPENGL_VERSION_TO_OLD" ) );
            return ErrorCode::GFX_NOT_SUPPORTED;
        }

        // Maximum number of colour attachments per framebuffer
        GLUtil::getGLValue( gl46core::GL_MAX_COLOR_ATTACHMENTS, deviceInformation._maxRTColourAttachments );

        deviceInformation._shaderCompilerThreads = GLUtil::getGLValue( gl::GL_MAX_SHADER_COMPILER_THREADS_ARB );
        Console::printfn( LOCALE_STR( "GL_SHADER_THREADS" ), deviceInformation._shaderCompilerThreads );
        gl::glMaxShaderCompilerThreadsARB( deviceInformation._shaderCompilerThreads );

        gl46core::glEnable( gl46core::GL_MULTISAMPLE );
        // Line smoothing should almost always be used
        gl46core::glEnable( gl46core::GL_LINE_SMOOTH );

        // GL_FALSE causes a conflict here. Thanks glbinding ...
        gl46core::glClampColor( gl46core::GL_CLAMP_READ_COLOR, gl46core::GL_FALSE );

        // Match Vulkan's depth range
        gl46core::glClipControl( gl46core::GL_LOWER_LEFT, gl46core::GL_ZERO_TO_ONE );

        // Cap max anisotropic level to what the hardware supports
        CLAMP( config.rendering.maxAnisotropicFilteringLevel,
               U8_ZERO,
               to_U8( GLUtil::getGLValue( gl46core::GL_MAX_TEXTURE_MAX_ANISOTROPY ) ) );

        deviceInformation._maxAnisotropy = config.rendering.maxAnisotropicFilteringLevel;

        // Number of sample buffers associated with the framebuffer & MSAA sample count
        const U8 maxGLSamples = to_U8( std::min( 254, GLUtil::getGLValue( gl46core::GL_MAX_SAMPLES ) ) );
        // If we do not support MSAA on a hardware level for whatever reason, override user set MSAA levels
        config.rendering.MSAASamples = std::min( config.rendering.MSAASamples, maxGLSamples );

        config.rendering.shadowMapping.csm.MSAASamples = std::min( config.rendering.shadowMapping.csm.MSAASamples, maxGLSamples );
        config.rendering.shadowMapping.spot.MSAASamples = std::min( config.rendering.shadowMapping.spot.MSAASamples, maxGLSamples );
        Attorney::DisplayManagerRenderingAPI::MaxMSAASamples( maxGLSamples );

        // Print all of the OpenGL functionality info to the console and log
        // How many uniforms can we send to fragment shaders
        Console::printfn( LOCALE_STR( "GL_MAX_UNIFORM" ), GLUtil::getGLValue( gl46core::GL_MAX_FRAGMENT_UNIFORM_COMPONENTS ) );
        // How many uniforms can we send to vertex shaders
        Console::printfn( LOCALE_STR( "GL_MAX_VERT_UNIFORM" ), GLUtil::getGLValue( gl46core::GL_MAX_VERTEX_UNIFORM_COMPONENTS ) );
        // How many uniforms can we send to vertex + fragment shaders at the same time
        Console::printfn( LOCALE_STR( "GL_MAX_FRAG_AND_VERT_UNIFORM" ), GLUtil::getGLValue( gl46core::GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS ) );
        // How many attributes can we send to a vertex shader
        deviceInformation._maxVertAttributes = GLUtil::getGLValue( gl46core::GL_MAX_VERTEX_ATTRIBS );
        Console::printfn( LOCALE_STR( "GL_MAX_VERT_ATTRIB" ), deviceInformation._maxVertAttributes );

        // How many workgroups can we have per compute dispatch
        for ( U8 i = 0u; i < 3u; ++i )
        {
            GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_COUNT, deviceInformation._maxWorgroupCount[i], i );
            GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_SIZE, deviceInformation._maxWorgroupSize[i], i );

            GLUtil::getGLValue(gl::GL_MAX_MESH_WORK_GROUP_SIZE_NV, deviceInformation._maxMeshWorgroupSize[i], i);
            GLUtil::getGLValue(gl::GL_MAX_TASK_WORK_GROUP_SIZE_NV, deviceInformation._maxTaskWorgroupSize[i], i);

            // ToDo: This is wrong so needs fixing once mesh shaders in GL reach EXT status! -Ionut
            deviceInformation._maxMeshWorgroupCount[i] = GLUtil::getGLValue(gl::GL_MAX_DRAW_MESH_TASKS_COUNT_NV);
            deviceInformation._maxTaskWorgroupCount[i] = GLUtil::getGLValue(gl::GL_MAX_DRAW_MESH_TASKS_COUNT_NV);
        }

        deviceInformation._maxWorgroupInvocations = GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS );
        deviceInformation._maxComputeSharedMemoryBytes = GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_SHARED_MEMORY_SIZE );

        Console::printfn( LOCALE_STR( "MAX_COMPUTE_WORK_GROUP_INFO" ) ,
                          deviceInformation._maxWorgroupCount[0], deviceInformation._maxWorgroupCount[1], deviceInformation._maxWorgroupCount[2],
                          deviceInformation._maxWorgroupSize[0], deviceInformation._maxWorgroupSize[1], deviceInformation._maxWorgroupSize[2],
                          deviceInformation._maxWorgroupInvocations );
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_SHARED_MEMORY_SIZE" ), deviceInformation._maxComputeSharedMemoryBytes / 1024 );

        deviceInformation._maxMeshShaderOutputVertices = GLUtil::getGLValue(gl::GL_MAX_MESH_OUTPUT_VERTICES_NV);
        deviceInformation._maxMeshShaderOutputPrimitives = GLUtil::getGLValue(gl::GL_MAX_MESH_OUTPUT_PRIMITIVES_NV);
        deviceInformation._maxMeshWorgroupInvocations = GLUtil::getGLValue(gl::GL_MAX_MESH_WORK_GROUP_INVOCATIONS_NV);
        deviceInformation._maxTaskWorgroupInvocations = GLUtil::getGLValue(gl::GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);

        Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_VERTICES"), deviceInformation._maxMeshShaderOutputVertices);
        Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_PRIMITIVES"), deviceInformation._maxMeshShaderOutputPrimitives);

        Console::printfn(LOCALE_STR("MAX_MESH_SHADER_WORGROUP_INFO"),
                                    deviceInformation._maxMeshWorgroupCount[0], deviceInformation._maxMeshWorgroupCount[1], deviceInformation._maxMeshWorgroupCount[2],
                                    deviceInformation._maxMeshWorgroupSize[0], deviceInformation._maxMeshWorgroupSize[1], deviceInformation._maxMeshWorgroupSize[2],
                                    deviceInformation._maxMeshWorgroupInvocations);
                                    
        Console::printfn(LOCALE_STR("MAX_TASK_SHADER_WORGROUP_INFO"),
                                    deviceInformation._maxTaskWorgroupCount[0], deviceInformation._maxTaskWorgroupCount[1], deviceInformation._maxTaskWorgroupCount[2],
                                    deviceInformation._maxTaskWorgroupSize[0], deviceInformation._maxTaskWorgroupSize[1], deviceInformation._maxTaskWorgroupSize[2],
                                    deviceInformation._maxTaskWorgroupInvocations);

        // Maximum number of texture units we can address in shaders
        Console::printfn( LOCALE_STR( "GL_MAX_TEX_UNITS" ),
                          GLUtil::getGLValue( gl46core::GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS ),
                          deviceInformation._maxTextureUnits );
        // Maximum number of varying components supported as outputs in the vertex shader
        deviceInformation._maxVertOutputComponents = GLUtil::getGLValue( gl46core::GL_MAX_VERTEX_OUTPUT_COMPONENTS );
        Console::printfn( LOCALE_STR( "MAX_VERTEX_OUTPUT_COMPONENTS" ), deviceInformation._maxVertOutputComponents );

        // Query shading language version support
        Console::printfn( LOCALE_STR( "GL_GLSL_SUPPORT" ),
                          reinterpret_cast<const char*>( gl46core::glGetString( gl46core::GL_SHADING_LANGUAGE_VERSION ) ) );
        // In order: Maximum number of uniform buffer binding points,
        //           maximum size in basic machine units of a uniform block and
        //           minimum required alignment for uniform buffer sizes and offset
        GLUtil::getGLValue( gl46core::GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, deviceInformation._offsetAlignmentBytesUBO );
        GLUtil::getGLValue( gl46core::GL_MAX_UNIFORM_BLOCK_SIZE, deviceInformation._maxSizeBytesUBO );
        const bool UBOSizeOver1Mb = deviceInformation._maxSizeBytesUBO / 1024 > 1024;
        Console::printfn( LOCALE_STR( "GL_VK_UBO_INFO" ),
                          GLUtil::getGLValue( gl46core::GL_MAX_UNIFORM_BUFFER_BINDINGS ),
                          (deviceInformation._maxSizeBytesUBO / 1024) / (UBOSizeOver1Mb ? 1024 : 1),
                          UBOSizeOver1Mb ? "Mb" : "Kb",
                          deviceInformation._offsetAlignmentBytesUBO );

        // In order: Maximum number of shader storage buffer binding points,
        //           maximum size in basic machine units of a shader storage block, maximum total number of active shader storage blocks that may
        //           be accessed by all active shaders and minimum required alignment for shader storage buffer sizes and offset.
        GLUtil::getGLValue( gl46core::GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, deviceInformation._offsetAlignmentBytesSSBO );
        GLUtil::getGLValue( gl46core::GL_MAX_SHADER_STORAGE_BLOCK_SIZE, deviceInformation._maxSizeBytesSSBO );
        deviceInformation._maxSSBOBufferBindings = GLUtil::getGLValue( gl46core::GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS );
        Console::printfn( LOCALE_STR( "GL_VK_SSBO_INFO" ),
                          deviceInformation._maxSSBOBufferBindings,
                          deviceInformation._maxSizeBytesSSBO / 1024 / 1024,
                          GLUtil::getGLValue( gl46core::GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS ),
                          deviceInformation._offsetAlignmentBytesSSBO );

        // Maximum number of subroutines and maximum number of subroutine uniform
        // locations usable in a shader
        Console::printfn( LOCALE_STR( "GL_SUBROUTINE_INFO" ),
                          GLUtil::getGLValue( gl46core::GL_MAX_SUBROUTINES ),
                          GLUtil::getGLValue( gl46core::GL_MAX_SUBROUTINE_UNIFORM_LOCATIONS ) );

        gl46core::GLint range[2];
        GLUtil::getGLValue( gl46core::GL_SMOOTH_LINE_WIDTH_RANGE, range );
        Console::printfn( LOCALE_STR( "GL_LINE_WIDTH_INFO" ), range[0], range[1] );

        const I32 clipDistanceCount = std::max( GLUtil::getGLValue( gl46core::GL_MAX_CLIP_DISTANCES ), 0 );
        const I32 cullDistanceCount = std::max( GLUtil::getGLValue( gl46core::GL_MAX_CULL_DISTANCES ), 0 );

        deviceInformation._maxClipAndCullDistances = GLUtil::getGLValue( gl46core::GL_MAX_COMBINED_CLIP_AND_CULL_DISTANCES );
        deviceInformation._maxClipDistances = to_U32( clipDistanceCount );
        deviceInformation._maxCullDistances = to_U32( cullDistanceCount );

        GFXDevice::OverrideDeviceInformation( deviceInformation );
        // Seamless cubemaps are a nice feature to have enabled (core since 3.2)
        gl46core::glEnable( gl46core::GL_TEXTURE_CUBE_MAP_SEAMLESS  );
        gl46core::glEnable( gl46core::GL_FRAMEBUFFER_SRGB );
        // Culling is enabled by default, but RenderStateBlocks can toggle it on a per-draw call basis
        gl46core::glEnable( gl46core::GL_CULL_FACE );

        // Enable all clip planes, I guess
        for ( U8 i = 0u; i < Config::MAX_CLIP_DISTANCES; ++i )
        {
            gl46core::glEnable( static_cast<gl46core::GLenum>(static_cast<U32>(gl46core::GL_CLIP_DISTANCE0) + i) );
        }

        for ( U8 i = 0u; i < to_base( GLUtil::GLMemory::GLMemoryType::COUNT ); ++i )
        {
            s_memoryAllocators[i].init( s_memoryAllocatorSizes[i] );
        }

        s_textureViewCache.init( 256 );

        // Initialize our query pool
        s_hardwareQueryPool->init(
            {
                { gl46core::GL_TIME_ELAPSED, 9 },
                { gl46core::GL_TRANSFORM_FEEDBACK_OVERFLOW, 6 },
                { gl46core::GL_VERTICES_SUBMITTED, 6 },
                { gl46core::GL_PRIMITIVES_SUBMITTED, 6 },
                { gl46core::GL_VERTEX_SHADER_INVOCATIONS, 6 },
                { gl46core::GL_SAMPLES_PASSED, 6 },
                { gl46core::GL_ANY_SAMPLES_PASSED, 6 },
                { gl46core::GL_PRIMITIVES_GENERATED, 6 },
                { gl46core::GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, 6 },
                { gl46core::GL_ANY_SAMPLES_PASSED_CONSERVATIVE, 6 },
                { gl46core::GL_TESS_CONTROL_SHADER_PATCHES, 6},
                { gl46core::GL_TESS_EVALUATION_SHADER_INVOCATIONS, 6}
            }
        );

        gl46core::glClearColor( DefaultColours::BLACK.r,
                                DefaultColours::BLACK.g,
                                DefaultColours::BLACK.b,
                                DefaultColours::BLACK.a );

        gl46core::glCreateVertexArrays( 1, &_dummyVAO );
        DIVIDE_ASSERT( _dummyVAO != GL_NULL_HANDLE, LOCALE_STR( "ERROR_VAO_INIT" ) );

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            gl46core::glObjectLabel( gl46core::GL_VERTEX_ARRAY,
                                     _dummyVAO,
                                     -1,
                                     "GENERIC_VAO");
        }

        gl46core::glBindVertexArray( _dummyVAO );
        s_stateTracker.setDefaultState();

        constexpr U16 ringLength = 6u;

        _performanceQueries[to_base( GlobalQueryTypes::VERTICES_SUBMITTED )]            = std::make_unique<glHardwareQueryRing>( _context, gl46core::GL_VERTICES_SUBMITTED,                 ringLength );
        _performanceQueries[to_base( GlobalQueryTypes::PRIMITIVES_GENERATED )]          = std::make_unique<glHardwareQueryRing>( _context, gl46core::GL_PRIMITIVES_GENERATED,               ringLength );
        _performanceQueries[to_base( GlobalQueryTypes::TESSELLATION_PATCHES )]          = std::make_unique<glHardwareQueryRing>( _context, gl46core::GL_TESS_CONTROL_SHADER_PATCHES,        ringLength );
        _performanceQueries[to_base( GlobalQueryTypes::TESSELLATION_EVAL_INVOCATIONS )] = std::make_unique<glHardwareQueryRing>( _context, gl46core::GL_TESS_EVALUATION_SHADER_INVOCATIONS, ringLength );
        _performanceQueries[to_base( GlobalQueryTypes::GPU_TIME )]                      = std::make_unique<glHardwareQueryRing>( _context, gl46core::GL_TIME_ELAPSED,                       ringLength );

        s_stateTracker._enabledAPIDebugging = &config.debug.renderer.enableRenderAPIDebugging;
        s_stateTracker._assertOnAPIError = &config.debug.renderer.assertOnRenderAPIError;

        // That's it. Everything should be ready for draw calls
        Console::printfn( LOCALE_STR( "START_OGL_API_OK" ) );
        return ErrorCode::NO_ERR;
    }

    /// Clear everything that was setup in initRenderingAPI()
    void GL_API::closeRenderingAPI()
    {
        glShaderProgram::DestroyStaticData();

        if ( _dummyVAO != GL_NULL_HANDLE )
        {
            gl46core::glBindVertexArray(0u);
            gl46core::glDeleteVertexArrays(1, &_dummyVAO );
            _dummyVAO = GL_NULL_HANDLE;
        }

        // Destroy sampler objects
        {
            for ( auto& sampler : s_samplerMap )
            {
                glSamplerObject::Destruct( sampler._glHandle );
            }
            s_samplerMap.clear();
        }

        s_textureViewCache.destroy();
        if ( s_hardwareQueryPool != nullptr )
        {
            s_hardwareQueryPool->destroy();
            s_hardwareQueryPool.reset();
        }
        for ( GLUtil::GLMemory::DeviceAllocator& allocator : s_memoryAllocators )
        {
            allocator.deallocate();
        }
        g_ContextPool.destroy();

        glLockManager::Clear();
        s_stateTracker.setDefaultState();
    }

    void GL_API::endPerformanceQueries()
    {
        if ( _runQueries )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

            // End the timing query started in beginFrame() in debug builds
            for ( U8 i = 0; i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                _performanceQueries[i]->end();
            }
        }
    }

    /// Prepare the GPU for rendering a frame
    bool GL_API::drawToWindow( DisplayWindow& window )
    {
        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ) );
        GLUtil::s_glMainRenderWindow = &window;
        return true;
    }

    void GL_API::onRenderThreadLoopStart( )
    {
       //GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), nullptr );
    }

    void GL_API::onRenderThreadLoopEnd()
    {
    }

    void GL_API::prepareFlushWindow( [[maybe_unused]] DisplayWindow& window )
    {
        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ));
    }

    void GL_API::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool mainWindow = window.getGUID() == _context.context().mainWindow().getGUID();
        if ( mainWindow )
        {
            endPerformanceQueries();
            _swapBufferTimer.start();
        }
        {
            PROFILE_SCOPE( "GL_API: Swap Buffers", Profiler::Category::Graphics );
            SDL_GL_SwapWindow( window.getRawWindow() );
        }
        
        if ( mainWindow ) 
        {
            _swapBufferTimer.stop();
        }
    }

    bool GL_API::frameStarted()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // Start a duration query in debug builds
        if ( _runQueries )
        {
            for ( U8 i = 0u; i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                _performanceQueries[i]->begin();
            }
        }

        while ( !s_stateTracker._endFrameFences.empty() )
        {
            auto& sync = s_stateTracker._endFrameFences.front();
            const gl46core::GLenum waitRet = gl46core::glClientWaitSync( sync.first, gl46core::SyncObjectMask::GL_NONE_BIT, 0u );
            DIVIDE_ASSERT( waitRet != gl46core::GL_WAIT_FAILED, "GL_API::beginFrame error: Not sure what to do here. Probably raise an exception or something." );
            if ( waitRet == gl46core::GL_ALREADY_SIGNALED || waitRet == gl46core::GL_CONDITION_SATISFIED )
            {
                s_stateTracker._lastSyncedFrameNumber = sync.second;
                DestroyFenceSync( sync.first );
                s_stateTracker._endFrameFences.pop();
            }
            else
            {
                break;
            }
        }

        LockManager::CleanExpiredSyncObjects( RenderAPI::OpenGL, s_stateTracker._lastSyncedFrameNumber );

        return true;
    }

    bool GL_API::frameEnded()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        GLUtil::GLMemory::OnFrameEnd( GFXDevice::FrameCount() );

        for ( U32 i = 0u; i < GL_API::s_LockFrameLifetime - 1; ++i )
        {
            s_fenceSyncCounter[i] = s_fenceSyncCounter[i + 1];
        }

        PROFILE_SCOPE( "GL_API: Post-Swap cleanup", Profiler::Category::Graphics );
        s_textureViewCache.onFrameEnd();
        s_glFlushQueued.store( false );

        if ( _runQueries )
        {
            PROFILE_SCOPE( "GL_API: Time Query", Profiler::Category::Graphics );

            thread_local std::array<I64, to_base( GlobalQueryTypes::COUNT )> results{};
            for ( U8 i = 0u; i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                results[i] = _performanceQueries[i]->getResultNoWait();
                _performanceQueries[i]->incQueue();
            }

            _context.getPerformanceMetrics()._gpuTimeInMS             = Time::NanosecondsToMilliseconds<F32>( results[to_base( GlobalQueryTypes::GPU_TIME )] );
            _context.getPerformanceMetrics()._verticesSubmitted       = to_U64( results[to_base( GlobalQueryTypes::VERTICES_SUBMITTED )] );
            _context.getPerformanceMetrics()._primitivesGenerated     = to_U64( results[to_base( GlobalQueryTypes::PRIMITIVES_GENERATED )] );
            _context.getPerformanceMetrics()._tessellationPatches     = to_U64( results[to_base( GlobalQueryTypes::TESSELLATION_PATCHES )] );
            _context.getPerformanceMetrics()._tessellationInvocations = to_U64( results[to_base( GlobalQueryTypes::TESSELLATION_EVAL_INVOCATIONS )] );
        }

        const size_t fenceSize = std::size( s_fenceSyncCounter );

        for ( size_t i = 0u; i < std::size( _context.getPerformanceMetrics()._syncObjectsInFlight ); ++i )
        {
            _context.getPerformanceMetrics()._syncObjectsInFlight[i] = i < fenceSize ? s_fenceSyncCounter[i] : 0u;
        }

        _context.getPerformanceMetrics()._generatedRenderTargetCount = to_U32( _context.renderTargetPool().getRenderTargets().size() );
        _runQueries = _context.queryPerformanceStats();

        s_stateTracker._endFrameFences.push( std::make_pair( CreateFenceSync(), GFXDevice::FrameCount() ) );
        _context.getPerformanceMetrics()._queuedGPUFrames = s_stateTracker._endFrameFences.size();

        if ( gl46core::glGetGraphicsResetStatus() != gl46core::GL_NO_ERROR )
        {
            DIVIDE_UNEXPECTED_CALL_MSG( "OpenGL Reset Status raised!" );
        }

        clearStates(s_stateTracker);
        return true;
    }

    void GL_API::idle( [[maybe_unused]] const bool fast )
    {
        glShaderProgram::Idle( _context.context() );
    }

    bool GL_API::Draw( const GenericDrawCommand& cmd )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        DIVIDE_ASSERT(cmd._drawCount < GFXDevice::GetDeviceInformation()._maxDrawIndirectCount);

        if ( cmd._sourceBuffer._id == 0u )
        {
            DIVIDE_ASSERT(cmd._cmd.indexCount == 0u);

            if (cmd._cmd.vertexCount == 0u )
            {
                GenericDrawCommand drawCmd = cmd;
                switch ( GL_API::s_stateTracker._activeTopology )
                {
                    case PrimitiveTopology::POINTS: drawCmd._cmd.vertexCount = 1u; break;
                    case PrimitiveTopology::LINES:
                    case PrimitiveTopology::LINE_STRIP:
                    case PrimitiveTopology::LINE_STRIP_ADJACENCY:
                    case PrimitiveTopology::LINES_ADJANCENCY: drawCmd._cmd.vertexCount = 2u; break;
                    case PrimitiveTopology::TRIANGLES:
                    case PrimitiveTopology::TRIANGLE_STRIP:
                    case PrimitiveTopology::TRIANGLE_FAN:
                    case PrimitiveTopology::TRIANGLES_ADJACENCY:
                    case PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY: drawCmd._cmd.vertexCount = 3u; break;
                    case PrimitiveTopology::PATCH: drawCmd._cmd.vertexCount = 4u; break;
                    default : return false;
                }
                GLUtil::SubmitRenderCommand( drawCmd );
            }
            else
            {
                GLUtil::SubmitRenderCommand( cmd );
            }
        }
        else [[likely]]
        {
            // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
            thread_local VertexDataInterface::Handle s_lastID = VertexDataInterface::INVALID_VDI_HANDLE;
            thread_local VertexDataInterface* s_lastBuffer = nullptr;

            if ( s_lastID != cmd._sourceBuffer )
            {
                s_lastID = cmd._sourceBuffer;
                s_lastBuffer = VertexDataInterface::s_VDIPool.find( s_lastID );
            }

            DIVIDE_ASSERT( s_lastBuffer != nullptr );
            s_lastBuffer->draw( cmd, nullptr );
        }

        return true;
    }

    void GL_API::flushTextureBindQueue()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        thread_local std::array<TexBindEntry, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> s_textureCache;
        thread_local std::array<gl46core::GLuint, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> s_textureHandles;
        thread_local std::array<gl46core::GLuint, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> s_textureSamplers;

        if ( s_TexBindQueue.empty() )
        {
            return;
        }

        if ( s_TexBindQueue.size() == 1u )
        {
            const TexBindEntry& texEntry = s_TexBindQueue.front();
            if ( s_stateTracker.bindTexture( texEntry._slot, texEntry._handle, texEntry._sampler ) == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        else
        {
            constexpr bool s_useBatchBindTextures = true;

            // Sort by slot number
            eastl::sort( eastl::begin( s_TexBindQueue ),
                         eastl::end( s_TexBindQueue ),
                         []( const  TexBindEntry& lhs, const TexBindEntry& rhs )
                         {
                             return lhs._slot < rhs._slot;
                         } );

            if ( s_useBatchBindTextures )
            {
                // Reset our cache
                for ( auto& it : s_textureCache )
                {
                    it._handle = GL_NULL_HANDLE;
                }

                // Grab min and max slot and fill in the cache with all of the available data
                U8 minSlot = U8_MAX, maxSlot = 0u;
                for ( const TexBindEntry& texEntry : s_TexBindQueue )
                {
                    s_textureCache[texEntry._slot] = texEntry;
                    minSlot = std::min( texEntry._slot, minSlot );
                    maxSlot = std::max( texEntry._slot, maxSlot );
                }

                U8 idx = 0u, bindOffset = minSlot, texCount = maxSlot - minSlot;
                for ( U8 i = 0u; i < texCount + 1; ++i )
                {
                    const U8 slot = i + minSlot;
                    const TexBindEntry& it = s_textureCache[slot];

                    if ( it._handle != GL_NULL_HANDLE )
                    {
                        s_textureHandles[idx] = it._handle;
                        s_textureSamplers[idx] = it._sampler;

                        if ( idx++ == 0u )
                        {
                            // Start a new range so remember the starting offset
                            bindOffset = slot;
                        }
                    }
                    else if ( idx > 0u )
                    {
                        // We found a hole in the texture range. Flush the current range and start a new one
                        if ( s_stateTracker.bindTextures( bindOffset, idx, s_textureHandles.data(), s_textureSamplers.data() ) == GLStateTracker::BindResult::FAILED )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                        idx = 0u;
                    }
                }
                // Handle the final range (if any)
                if ( idx > 0u )
                {
                    if ( s_stateTracker.bindTextures( bindOffset, idx, s_textureHandles.data(), s_textureSamplers.data() ) == GLStateTracker::BindResult::FAILED )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }
                }
            }
            else
            {
                for ( const TexBindEntry& texEntry : s_TexBindQueue )
                {
                    if ( s_stateTracker.bindTexture( texEntry._slot, texEntry._handle, texEntry._sampler ) == GLStateTracker::BindResult::FAILED )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }
                }
            }
        }

        efficient_clear(s_TexBindQueue);
    }

    gl46core::GLuint GL_API::getGLTextureView( const ImageView srcView, const size_t srcViewHash, const U8 lifetimeInFrames ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        auto [handle, cacheHit] = s_textureViewCache.allocate( srcViewHash );

        if ( !cacheHit )
        {
            const gl46core::GLuint srcHandle = static_cast<const glTexture*>(srcView._srcTexture)->textureHandle();
            
            if ( srcHandle == GL_NULL_HANDLE )
            {
                return srcHandle;
            }

            const gl46core::GLenum glInternalFormat = GLUtil::InternalFormatAndDataType( srcView._descriptor._baseFormat,
                                                                                         srcView._descriptor._dataType,
                                                                                         srcView._descriptor._packing )._format;

            const bool isCube = IsCubeTexture( TargetType( srcView ) );

            PROFILE_SCOPE( "GL: cache miss  - Image", Profiler::Category::Graphics );
            gl46core::glTextureView( handle,
                                     GLUtil::internalTextureType( TargetType( srcView ), srcView._descriptor._msaaSamples ),
                                     srcHandle,
                                     glInternalFormat,
                                     static_cast<gl46core::GLuint>(srcView._subRange._mipLevels._offset),
                                     static_cast<gl46core::GLuint>(srcView._subRange._mipLevels._count),
                                     srcView._subRange._layerRange._offset * (isCube ? 6 : 1),
                                     srcView._subRange._layerRange._count * (isCube ? 6 : 1));
        }

        s_textureViewCache.deallocate( handle, lifetimeInFrames );

        return handle;
    }

    void GL_API::flushPushConstantsLocks()
    {
        if ( _uniformsNeedLock )
        {
            _uniformsNeedLock = false;
            flushCommand( &_uniformsMemCommand );
            _uniformsMemCommand._bufferLocks.clear();
        }
    }

    void GL_API::preFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
        GetStateTracker()._activeRenderTargetID = SCREEN_TARGET_ID;
        GetStateTracker()._activeRenderTargetDimensions = _context.context().mainWindow().getDrawableSize();
    }

    void GL_API::flushCommand( GFX::CommandBase* cmd )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( GFXDevice::IsSubmitCommand( cmd->type() ) )
        {
            flushTextureBindQueue();
        }

        if ( s_stateTracker._activeRenderTargetID == INVALID_RENDER_TARGET_ID )
        {
            flushPushConstantsLocks();
        }

        switch ( cmd->type() )
        {
            case GFX::CommandType::BEGIN_RENDER_PASS:
            {
                PROFILE_SCOPE( "BEGIN_RENDER_PASS", Profiler::Category::Graphics );

                const GFX::BeginRenderPassCommand* crtCmd = cmd->As<GFX::BeginRenderPassCommand>();

                s_stateTracker._activeRenderTargetID = crtCmd->_target;

                if ( crtCmd->_target == SCREEN_TARGET_ID )
                {
                    PROFILE_SCOPE( "Begin Screen Target", Profiler::Category::Graphics );

                    if ( s_stateTracker.setActiveFB( RenderTarget::Usage::RT_WRITE_ONLY, 0u ) == GLStateTracker::BindResult::FAILED )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }

                    s_stateTracker._activeRenderTarget = nullptr;
                    s_stateTracker._activeRenderTargetDimensions = _context.context().mainWindow().getDrawableSize();

                    if ( crtCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0)]._enabled)
                    {
                        PROFILE_SCOPE( "Clear Screen Target", Profiler::Category::Graphics );

                        gl46core::ClearBufferMask mask = gl46core::ClearBufferMask::GL_COLOR_BUFFER_BIT;

                        s_stateTracker.setClearColour( crtCmd->_clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )]._colour );
                        if ( crtCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX]._enabled )
                        {
                            s_stateTracker.setClearDepth( crtCmd->_clearDescriptor[RT_DEPTH_ATTACHMENT_IDX]._colour.r );
                            mask |= gl46core::ClearBufferMask::GL_DEPTH_BUFFER_BIT;
                        }
                        gl46core::glClear( mask );
                    }
                    PushDebugMessage( crtCmd->_name.c_str(), SCREEN_TARGET_ID );
                }
                else
                {
                    PROFILE_SCOPE( "Begin Render Target", Profiler::Category::Graphics );

                    glFramebuffer* rt = static_cast<glFramebuffer*>(_context.renderTargetPool().getRenderTarget( crtCmd->_target ));

                    const GLStateTracker::BindResult bindResult = s_stateTracker.setActiveFB( RenderTarget::Usage::RT_WRITE_ONLY, rt->framebufferHandle() );
                    if ( bindResult == GLStateTracker::BindResult::FAILED )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }

                    Attorney::GLAPIRenderTarget::begin( *rt, crtCmd->_descriptor, crtCmd->_clearDescriptor );
                    s_stateTracker._activeRenderTarget = rt;
                    s_stateTracker._activeRenderTargetDimensions = { rt->getWidth(), rt->getHeight() };
                    PushDebugMessage(crtCmd->_name.c_str(), crtCmd->_target);
                    AddDebugMessage(rt->debugMessage().c_str(), crtCmd->_target );
                }
            }break;
            case GFX::CommandType::END_RENDER_PASS:
            {
                PROFILE_SCOPE( "END_RENDER_PASS", Profiler::Category::Graphics );

                PopDebugMessage();

                if ( GL_API::s_stateTracker._activeRenderTarget == nullptr )
                {
                    assert( GL_API::s_stateTracker._activeRenderTargetID == SCREEN_TARGET_ID );
                }
                else
                {
                    const GFX::EndRenderPassCommand* crtCmd = cmd->As<GFX::EndRenderPassCommand>();
                    Attorney::GLAPIRenderTarget::end( *s_stateTracker._activeRenderTarget, crtCmd->_transitionMask );
                    s_stateTracker._activeRenderTarget = nullptr;
                }
                s_stateTracker._activeRenderTargetID = SCREEN_TARGET_ID;
                s_stateTracker._activeRenderTargetDimensions = _context.context().mainWindow().getDrawableSize();
            }break;
            case GFX::CommandType::BLIT_RT:
            {
                PROFILE_SCOPE( "BLIT_RT", Profiler::Category::Graphics );

                const GFX::BlitRenderTargetCommand* crtCmd = cmd->As<GFX::BlitRenderTargetCommand>();
                glFramebuffer* source = static_cast<glFramebuffer*>(_context.renderTargetPool().getRenderTarget(crtCmd->_source));
                glFramebuffer* destination = static_cast<glFramebuffer*>(_context.renderTargetPool().getRenderTarget( crtCmd->_destination ));
                destination->blitFrom( source, crtCmd->_params );
            } break;
            case GFX::CommandType::BEGIN_GPU_QUERY:
            {
                PROFILE_SCOPE( "BEGIN_GPU_QUERY", Profiler::Category::Graphics );

                const GFX::BeginGPUQueryCommand* crtCmd = cmd->As<GFX::BeginGPUQueryCommand>();
                if ( crtCmd->_queryMask != 0u ) [[likely]]
                {
                    auto& queryContext = _queryContext.emplace();
                    for ( U8 i = 0u, j = 0u; i < to_base( QueryType::COUNT ); ++i )
                    {
                        const U32 typeFlag = toBit( i + 1u );
                        if ( crtCmd->_queryMask & typeFlag )
                        {
                            queryContext[i]._query = &GL_API::GetHardwareQueryPool()->allocate( GLUtil::glQueryTypeTable[i] );
                            queryContext[i]._type = static_cast<QueryType>(typeFlag);
                            queryContext[i]._index = j++;
                        }
                    }

                    for ( auto& queryEntry : queryContext )
                    {
                        if ( queryEntry._query != nullptr )
                        {
                            queryEntry._query->begin();
                        }
                    }
                }
            }break;
            case GFX::CommandType::END_GPU_QUERY:
            {
                PROFILE_SCOPE( "END_GPU_QUERY", Profiler::Category::Graphics );

                if ( !_queryContext.empty() ) [[likely]]
                {
                    const GFX::EndGPUQueryCommand* crtCmd = cmd->As<GFX::EndGPUQueryCommand>();

                    DIVIDE_ASSERT( crtCmd->_resultContainer != nullptr );

                    for ( glHardwareQueryEntry& queryEntry : _queryContext.top() )
                    {
                        if ( queryEntry._query != nullptr )
                        {
                            queryEntry._query->end();

                            const I64 qResult = crtCmd->_waitForResults ? queryEntry._query->getResult() : queryEntry._query->getResultNoWait();
                            (*crtCmd->_resultContainer)[queryEntry._index] = { queryEntry._type,  qResult };
                        }
                    }

                    _queryContext.pop();
                }
            }break;
            case GFX::CommandType::COPY_TEXTURE:
            {
                PROFILE_SCOPE( "COPY_TEXTURE", Profiler::Category::Graphics );

                const GFX::CopyTextureCommand* crtCmd = cmd->As<GFX::CopyTextureCommand>();
                glTexture::Copy( static_cast<glTexture*>(Get(crtCmd->_source)),
                                 crtCmd->_sourceMSAASamples,
                                 static_cast<glTexture*>(Get(crtCmd->_destination)),
                                 crtCmd->_destinationMSAASamples,
                                 crtCmd->_params );
            }break;
            case GFX::CommandType::CLEAR_TEXTURE:
            {
                PROFILE_SCOPE( "CLEAR_TEXTURE", Profiler::Category::Graphics );

                const GFX::ClearTextureCommand* crtCmd = cmd->As<GFX::ClearTextureCommand>();
                static_cast<glTexture*>(Get(crtCmd->_texture))->clearData( crtCmd->_clearColour, crtCmd->_layerRange, crtCmd->_mipLevel );
             }break;
            case GFX::CommandType::READ_TEXTURE:
            {
                PROFILE_SCOPE( "READ_TEXTURE", Profiler::Category::Graphics );

                const GFX::ReadTextureCommand* crtCmd = cmd->As<GFX::ReadTextureCommand>();
                glTexture* tex = static_cast<glTexture*>(Get(crtCmd->_texture));
                const ImageReadbackData readData = tex->readData( crtCmd->_mipLevel, crtCmd->_pixelPackAlignment );
                crtCmd->_callback( readData );
            }break;
            case GFX::CommandType::BIND_PIPELINE:
            {
                PROFILE_SCOPE( "BIND_PIPELINE", Profiler::Category::Graphics );

                const Pipeline* pipeline = cmd->As<GFX::BindPipelineCommand>()->_pipeline;
                assert( pipeline != nullptr );
                if ( BindPipeline(_context, *pipeline ) == ShaderResult::Failed )
                {
                    const auto handle = pipeline->descriptor()._shaderProgramHandle;
                    Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_BIND" ), handle._index, handle._generation );
                }
            } break;
            case GFX::CommandType::SEND_PUSH_CONSTANTS:
            {
                PROFILE_SCOPE( "SEND_PUSH_CONSTANTS", Profiler::Category::Graphics );

                const auto dumpLogs = [this]()
                {
                    Console::d_errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_PUSH_CONSTANTS" ) );
                    if ( Config::ENABLE_GPU_VALIDATION )
                    {
                        // Shader failed to compile probably. Dump all shader caches for inspection.
                        glShaderProgram::Idle( _context.context() );
                    }
                };

                const Pipeline* activePipeline = s_stateTracker._activePipeline;
                if ( activePipeline == nullptr )
                {
                    dumpLogs();
                    break;
                }

                if ( s_stateTracker._activeShaderProgram == nullptr )
                {
                    // Should we skip the upload?
                    dumpLogs();
                    break;
                }

                GFX::SendPushConstantsCommand* pushConstantsCmd = cmd->As<GFX::SendPushConstantsCommand>();
                UniformData* uniforms = pushConstantsCmd->_uniformData;
                if ( uniforms != nullptr )
                {
                    if ( s_stateTracker._activeShaderProgram->uploadUniformData( *uniforms, _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).impl(), _uniformsMemCommand ) )
                    {
                        _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).dirty( true );
                        _uniformsNeedLock = _uniformsNeedLock || _uniformsMemCommand._bufferLocks.empty();
                    }
                }
                Attorney::GLAPIShaderProgram::uploadPushConstants( *s_stateTracker._activeShaderProgram, pushConstantsCmd->_fastData );

            } break;
            case GFX::CommandType::BEGIN_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "BEGIN_DEBUG_SCOPE", Profiler::Category::Graphics );

                const auto& crtCmd = cmd->As<GFX::BeginDebugScopeCommand>();
                PushDebugMessage( crtCmd->_scopeName.c_str(), crtCmd->_scopeId );
            } break;
            case GFX::CommandType::END_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "END_DEBUG_SCOPE", Profiler::Category::Graphics );

                PopDebugMessage();
            } break;
            case GFX::CommandType::ADD_DEBUG_MESSAGE:
            {
                PROFILE_SCOPE( "ADD_DEBUG_MESSAGE", Profiler::Category::Graphics );

                const auto& crtCmd = cmd->As<GFX::AddDebugMessageCommand>();
                AddDebugMessage( crtCmd->_msg.c_str(), crtCmd->_msgId );
            }break;
            case GFX::CommandType::COMPUTE_MIPMAPS:
            {
                PROFILE_SCOPE( "COMPUTE_MIPMAPS", Profiler::Category::Graphics );

                const GFX::ComputeMipMapsCommand* crtCmd = cmd->As<GFX::ComputeMipMapsCommand>();

                ResourcePtr<Texture> tex = Get(crtCmd->_texture);
                const U16 texLayers = IsCubeTexture( tex->descriptor()._texType ) ? tex->depth() * 6u : tex->depth();
                const U16 layerCount = crtCmd->_layerRange._count == U16_MAX ? texLayers : crtCmd->_layerRange._count;

                if ( crtCmd->_layerRange._offset == 0 && layerCount >= texLayers )
                {
                    PROFILE_SCOPE( "GL: In-place computation - Full", Profiler::Category::Graphics );
                    gl46core::glGenerateTextureMipmap( static_cast<glTexture*>(tex)->textureHandle() );
                }
                else
                {
                    PROFILE_SCOPE( "GL: View-based computation", Profiler::Category::Graphics );
                    DIVIDE_ASSERT( crtCmd->_mipRange._count != 0u );

                    ImageView view = tex->getView();
                    view._subRange._layerRange = crtCmd->_layerRange;
                    view._subRange._mipLevels =  crtCmd->_mipRange;

                    const TextureType targetType = TargetType( view );
                    DIVIDE_ASSERT( targetType != TextureType::COUNT );

                    if ( IsArrayTexture( targetType ) && view._subRange._layerRange._count == 1 )
                    {
                        if ( targetType == TextureType::TEXTURE_1D_ARRAY )
                        {
                            view._targetType = TextureType::TEXTURE_1D;
                        }
                        else if ( targetType == TextureType::TEXTURE_2D_ARRAY )
                        {
                            view._targetType =TextureType::TEXTURE_2D;
                        }
                        else if ( targetType ==TextureType::TEXTURE_CUBE_ARRAY )
                        {
                            view._targetType = TextureType::TEXTURE_CUBE_MAP;
                        }
                        else
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    }

                    if ( view._subRange._mipLevels._count > view._subRange._mipLevels._offset &&
                         view._subRange._mipLevels._count - view._subRange._mipLevels._offset > 0 )
                    {
                        PROFILE_SCOPE( "GL: In-place computation - Image", Profiler::Category::Graphics );
                        gl46core::glGenerateTextureMipmap( getGLTextureView( view, GetHash( view ), 6u ) );
                    }
                }
            }break;
            case GFX::CommandType::DRAW_COMMANDS:
            {
                PROFILE_SCOPE( "DRAW_COMMANDS", Profiler::Category::Graphics );

                if ( s_stateTracker._activePipeline != nullptr )
                {
                    U32 drawCount = 0u;
                    const auto& drawCommands = cmd->As<GFX::DrawCommand>()->_drawCommands;

                    for ( const GenericDrawCommand& currentDrawCommand : drawCommands )
                    {
                        if ( isEnabledOption( currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY ))
                        {
                            Draw( currentDrawCommand );
                            ++drawCount;
                        }

                        if ( isEnabledOption( currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME ) )
                        {
                            PrimitiveTopology oldTopology = s_stateTracker._activeTopology;
                            s_stateTracker.setPrimitiveTopology(PrimitiveTopology::LINES);
                            Draw(currentDrawCommand);
                            s_stateTracker.setPrimitiveTopology(oldTopology);
                            ++drawCount;
                        }
                    }

                    _context.registerDrawCalls( drawCount );
                }
            }break;
            case GFX::CommandType::DISPATCH_SHADER_TASK:
            {
                PROFILE_SCOPE( "DISPATCH_SHADER_TASK", Profiler::Category::Graphics );

                if ( s_stateTracker._activePipeline != nullptr )
                {
                    const GFX::DispatchShaderTaskCommand* crtCmd = cmd->As<GFX::DispatchShaderTaskCommand>();

                    switch (s_stateTracker._activeTopology)
                    {
                        case PrimitiveTopology::COMPUTE:
                        {
                            gl46core::glDispatchCompute( crtCmd->_workGroupSize.x, crtCmd->_workGroupSize.y, crtCmd->_workGroupSize.z );
                        } break;

                        case PrimitiveTopology::MESHLET:
                        {
                            gl::glDrawMeshTasksNV( 0u, crtCmd->_workGroupSize.x );
                            _context.registerDrawCalls( 1u );
                        } break;
                        default:
                            DIVIDE_UNEXPECTED_CALL();
                            break;
                    };
                }
            }break;
            case GFX::CommandType::MEMORY_BARRIER:
            {
                PROFILE_SCOPE( "MEMORY_BARRIER", Profiler::Category::Graphics );

                const GFX::MemoryBarrierCommand* crtCmd = cmd->As<GFX::MemoryBarrierCommand>();

                gl46core::MemoryBarrierMask mask = gl46core::GL_NONE_BIT;

                SyncObjectHandle handle{};
                for ( const BufferLock& lock : crtCmd->_bufferLocks )
                {
                    if ( lock._buffer == nullptr || lock._range._length == 0u)
                    {
                        continue;
                    }

                    glBufferImpl* buffer = static_cast<glBufferImpl*>(lock._buffer);
                    const BufferParams& params = buffer->params()._bufferParams;

                    switch ( lock._type )
                    {
                        case BufferSyncUsage::CPU_WRITE_TO_GPU_READ:
                        {
                            if ( handle._id == SyncObjectHandle::INVALID_SYNC_ID )
                            {
                                handle = LockManager::CreateSyncObject( RenderAPI::OpenGL );
                            }

                            if ( !buffer->lockRange( lock._range, handle ) )
                            {
                                DIVIDE_UNEXPECTED_CALL();
                            }
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_CPU_READ:
                        {
                            if ( params._updateFrequency == BufferUpdateFrequency::ONCE )
                            {
                                mask |= gl46core::GL_BUFFER_UPDATE_BARRIER_BIT;
                            }
                            else
                            {
                                mask |= gl46core::GL_BUFFER_UPDATE_BARRIER_BIT | gl46core::GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT;
                            }
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_GPU_READ:
                        case BufferSyncUsage::GPU_READ_TO_GPU_WRITE:
                        {
                            switch ( params._usageType )
                            {
                                case BufferUsageType::CONSTANT_BUFFER:
                                {
                                    mask |= gl46core::GL_UNIFORM_BARRIER_BIT;
                                } break;
                                case BufferUsageType::UNBOUND_BUFFER:
                                {
                                    mask |= gl46core::GL_SHADER_STORAGE_BARRIER_BIT;
                                } break;
                                case BufferUsageType::COMMAND_BUFFER:
                                {
                                    mask |= gl46core::GL_COMMAND_BARRIER_BIT | gl46core::GL_SHADER_STORAGE_BARRIER_BIT;
                                } break;
                                case BufferUsageType::VERTEX_BUFFER:
                                {
                                    mask |= gl46core::GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
                                } break;
                                case BufferUsageType::INDEX_BUFFER:
                                {
                                    mask |= gl46core::GL_ELEMENT_ARRAY_BARRIER_BIT;
                                } break;
                                case BufferUsageType::STAGING_BUFFER:
                                {
                                    mask |= gl46core::GL_BUFFER_UPDATE_BARRIER_BIT | gl46core::GL_SHADER_STORAGE_BARRIER_BIT;
                                } break;
                                default: DIVIDE_UNEXPECTED_CALL(); break;
                            }
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE:
                        {
                            mask |= gl46core::GL_SHADER_STORAGE_BARRIER_BIT;
                        } break;
                        case BufferSyncUsage::CPU_WRITE_TO_CPU_READ:
                        case BufferSyncUsage::CPU_READ_TO_CPU_WRITE:
                        case BufferSyncUsage::CPU_WRITE_TO_CPU_WRITE:
                        {
                            NOP();
                        }break;
                        default: DIVIDE_UNEXPECTED_CALL(); break;
                    }
                }

                bool textureBarrierRequired = false;
                for ( const auto& it : crtCmd->_textureLayoutChanges )
                {
                    if ( it._sourceLayout == it._targetLayout )
                    {
                        continue;
                    }

                    switch ( it._targetLayout )
                    {
                        case ImageUsage::SHADER_WRITE:
                        {
                            mask |= gl46core::GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
                        } break;
                        case ImageUsage::SHADER_READ:
                        case ImageUsage::SHADER_READ_WRITE:
                        {
                            if ( it._sourceLayout != ImageUsage::SHADER_WRITE &&
                                    it._sourceLayout != ImageUsage::SHADER_READ_WRITE )
                            {
                                textureBarrierRequired = true;
                            }
                            else
                            {
                                mask |= gl46core::GL_TEXTURE_FETCH_BARRIER_BIT | gl46core::GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
                            }
                        } break;
                        case ImageUsage::CPU_READ:
                        {
                            mask |= gl46core::GL_TEXTURE_UPDATE_BARRIER_BIT;
                        } break;
                        case ImageUsage::RT_COLOUR_ATTACHMENT:
                        case ImageUsage::RT_DEPTH_ATTACHMENT:
                        case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:
                        {
                            mask |= gl46core::GL_FRAMEBUFFER_BARRIER_BIT;
                        } break;

                        default: DIVIDE_UNEXPECTED_CALL(); break;
                    }
                }

                if ( mask != gl46core::MemoryBarrierMask::GL_NONE_BIT )
                {
                    gl46core::glMemoryBarrier( mask );
                }
                if ( textureBarrierRequired )
                {
                    gl46core::glTextureBarrier();
                }
            } break;


            case GFX::CommandType::SET_VIEWPORT:
            case GFX::CommandType::PUSH_VIEWPORT:
            case GFX::CommandType::POP_VIEWPORT:
            case GFX::CommandType::SET_SCISSOR:
            case GFX::CommandType::SET_CAMERA:
            case GFX::CommandType::PUSH_CAMERA:
            case GFX::CommandType::POP_CAMERA:
            case GFX::CommandType::SET_CLIP_PLANES:
            case GFX::CommandType::BIND_SHADER_RESOURCES:
            case GFX::CommandType::READ_BUFFER_DATA:
            case GFX::CommandType::CLEAR_BUFFER_DATA: break;

            case GFX::CommandType::COUNT:
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    void GL_API::postFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        flushPushConstantsLocks();

        bool expected = true;
        if ( s_glFlushQueued.compare_exchange_strong( expected, false ) )
        {
            PROFILE_SCOPE( "GL_FLUSH", Profiler::Category::Graphics );
            gl46core::glFlush();
        }
        GetStateTracker()._activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        GetStateTracker()._activeRenderTargetDimensions = _context.context().mainWindow().getDrawableSize();
    }

    void GL_API::initDescriptorSets()
    {
        NOP();
    }

    void GL_API::onThreadCreated( [[maybe_unused]] const std::thread::id& threadID, const bool isMainRenderThread )
    {
        if ( isMainRenderThread )
        {
            // We'll try and use the same context from the main thread
            return;
        }

        // Double check so that we don't run into a race condition!
        LockGuard<Mutex> lock( GLUtil::s_glSecondaryContextMutex );
        assert( SDL_GL_GetCurrentContext() == NULL );

        // This also makes the context current
        assert( GLUtil::s_glSecondaryContext == nullptr && "GL_API::syncToThread: double init context for current thread!" );
        [[maybe_unused]] const bool ctxFound = g_ContextPool.getAvailableContext( GLUtil::s_glSecondaryContext );
        assert( ctxFound && "GL_API::syncToThread: context not found for current thread!" );

        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( GLUtil::s_glMainRenderWindow->getRawWindow(), GLUtil::s_glSecondaryContext ) );
        glbinding::Binding::initialize( []( const char* proc ) noexcept
                                        {
                                            return (glbinding::ProcAddress)SDL_GL_GetProcAddress( proc );
                                        } );

        // Enable OpenGL debug callbacks for this context as well
        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            gl46core::glEnable( gl46core::GL_DEBUG_OUTPUT );
            gl46core::glEnable( gl46core::GL_DEBUG_OUTPUT_SYNCHRONOUS );
            gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_MARKER,     gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
            gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_PUSH_GROUP, gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
            gl46core::glDebugMessageControl( gl46core::GL_DONT_CARE, gl46core::GL_DEBUG_TYPE_POP_GROUP,  gl46core::GL_DONT_CARE, 0, NULL, gl46core::GL_FALSE );
            // Debug callback in a separate thread requires a flag to distinguish it from the main thread's callbacks
            gl46core::glDebugMessageCallback( (gl46core::GLDEBUGPROC)GLUtil::DebugCallback, GLUtil::s_glSecondaryContext );
        }

        gl::glMaxShaderCompilerThreadsARB( 0xFFFFFFFF );
    }

    /// Reset as much of the GL default state as possible within the limitations given
    void GL_API::clearStates(GLStateTracker& stateTracker) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( !stateTracker.unbindTextures() )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        if ( stateTracker.setActiveBuffer( gl46core::GL_ELEMENT_ARRAY_BUFFER, 0 ) == GLStateTracker::BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        if ( stateTracker.setActiveFB( RenderTarget::Usage::RT_READ_WRITE, 0 ) == GLStateTracker::BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        const U8 blendCount = to_U8( stateTracker._blendEnabled.size() );
        for ( U8 i = 0u; i < blendCount; ++i )
        {
            stateTracker.setBlending( i, {} );
        }
        stateTracker.setBlendColour( { 0u, 0u, 0u, 0u } );

        const vec2<U16> drawableSize = _context.context().mainWindow().getDrawableSize();
        stateTracker.setScissor( { 0, 0, drawableSize.width, drawableSize.height } );

        stateTracker._activePipeline = nullptr;
        stateTracker._activeRenderTarget = nullptr;
        stateTracker._activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        stateTracker._activeRenderTargetDimensions = drawableSize;
        if ( stateTracker.setActiveProgram( 0u ) == GLStateTracker::BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        if ( stateTracker.setActiveShaderPipeline( 0u ) == GLStateTracker::BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        if ( stateTracker.setStateBlock({}) == GLStateTracker::BindResult::FAILED )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    bool GL_API::bindShaderResources( const DescriptorSetEntries& descriptorSetEntries )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics );

        for ( const DescriptorSetEntry& entry : descriptorSetEntries )
        {
            if ( !entry._isDirty )
            {
                // We don't need to keep track of descriptor set to layout compatibility in OpenGL
                continue;
            }

            for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
            {
                const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];
                const gl46core::GLubyte glBindingSlot = ShaderProgram::GetGLBindingForDescriptorSlot( entry._usage, srcBinding._slot );

                switch ( srcBinding._data._type )
                {
                    case DescriptorSetBindingType::UNIFORM_BUFFER:
                    case DescriptorSetBindingType::SHADER_STORAGE_BUFFER:
                    {
                        const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;
                        if ( bufferEntry._buffer == nullptr || 
                             bufferEntry._range._length == 0u ) [[unlikely]]
                        {
                            continue;
                        }

                        glShaderBuffer* glBuffer = static_cast<glShaderBuffer*>(bufferEntry._buffer);

                        if ( !glBuffer->bindByteRange(
                            glBindingSlot,
                            {
                                bufferEntry._range._startOffset * glBuffer->getPrimitiveSize(),
                                bufferEntry._range._length      * glBuffer->getPrimitiveSize(),
                            },
                            bufferEntry._queueReadIndex
                            ) )
                        {
                            //ToDo: maybe warn and skip the next draw instead? -Ionut
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                    {
                        if ( srcBinding._slot == INVALID_TEXTURE_BINDING ) [[unlikely]]
                        {
                            continue;
                        }

                        const DescriptorCombinedImageSampler& imageSampler = srcBinding._data._sampledImage;
                        if ( !makeTextureViewResident( glBindingSlot, imageSampler._image, imageSampler._imageHash, imageSampler._sampler, imageSampler._samplerHash ) )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    case DescriptorSetBindingType::IMAGE:
                    {
                        const DescriptorImageView& imageView = srcBinding._data._imageView;
                        DIVIDE_ASSERT( TargetType( imageView._image ) != TextureType::COUNT );
                        DIVIDE_ASSERT( imageView._image._subRange._layerRange._count > 0u );

                        gl46core::GLenum access = gl46core::GL_NONE;
                        switch ( imageView._usage )
                        {
                            case ImageUsage::SHADER_READ:       access = gl46core::GL_READ_ONLY; break;
                            case ImageUsage::SHADER_WRITE:      access = gl46core::GL_WRITE_ONLY; break;
                            case ImageUsage::SHADER_READ_WRITE: access = gl46core::GL_READ_WRITE; break;

                            case ImageUsage::UNDEFINED:
                            case ImageUsage::CPU_READ:
                            case ImageUsage::RT_COLOUR_ATTACHMENT:
                            case ImageUsage::RT_DEPTH_ATTACHMENT:
                            case ImageUsage::RT_DEPTH_STENCIL_ATTACHMENT:

                            default:
                            case ImageUsage::COUNT: DIVIDE_UNEXPECTED_CALL();  break;
                        }

                        DIVIDE_ASSERT( imageView._image._subRange._mipLevels._count == 1u );

                        const gl46core::GLenum glInternalFormat = GLUtil::InternalFormatAndDataType( imageView._image._descriptor._baseFormat,
                                                                                                     imageView._image._descriptor._dataType,
                                                                                                     imageView._image._descriptor._packing )._format;

                        const gl46core::GLuint handle = static_cast<const glTexture*>(imageView._image._srcTexture)->textureHandle();
                        if ( handle != GL_NULL_HANDLE &&
                             GL_API::s_stateTracker.bindTextureImage( glBindingSlot,
                                                                      handle,
                                                                      imageView._image._subRange._mipLevels._offset,
                                                                      imageView._image._subRange._layerRange._count > 1u,
                                                                      imageView._image._subRange._layerRange._offset,
                                                                      access,
                                                                      glInternalFormat ) == GLStateTracker::BindResult::FAILED )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    default:
                    case DescriptorSetBindingType::COUNT:
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    } break;
                };
            }
        }

        return true;
    }

    bool GL_API::makeTextureViewResident( const gl46core::GLubyte bindingSlot, const ImageView& imageView, const size_t imageViewHash, const SamplerDescriptor sampler, const size_t samplerHash ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( imageView._srcTexture == nullptr )
        {
            //unbind request;
            s_TexBindQueue.emplace_back(TexBindEntry
            {
                ._handle = 0u,
                ._sampler = 0u,
                ._slot = bindingSlot
            });

            return true;
        }

        TexBindEntry entry{};
        entry._slot = bindingSlot;

        if ( imageView._srcTexture != nullptr && imageView != imageView._srcTexture->getView())
        {
            entry._handle = getGLTextureView(imageView, imageViewHash, 3u);
        }
        else
        {
            entry._handle = static_cast<const glTexture*>(imageView._srcTexture)->textureHandle();
        }

        if ( entry._handle == GL_NULL_HANDLE )
        {
            return false;
        }

        size_t tempSampler = samplerHash;
        entry._sampler = GetSamplerHandle( sampler, tempSampler );

        for ( TexBindEntry& it : s_TexBindQueue )
        {
            if ( it._slot == bindingSlot )
            {
                it = entry;
                return true;
            }
        }
        
        s_TexBindQueue.push_back( MOV( entry ) );
        return true;
    }

    bool GL_API::setViewportInternal( const Rect<I32>& viewport )
    {
        return s_stateTracker.setViewport( viewport );
    }

    bool GL_API::setScissorInternal( const Rect<I32>& scissor )
    {
        return s_stateTracker.setScissor( scissor );
    }

    ShaderResult GL_API::BindPipeline(GFXDevice& context, const Pipeline& pipeline )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( s_stateTracker._activePipeline && *s_stateTracker._activePipeline == pipeline )
        {
            return ShaderResult::OK;
        }

        s_stateTracker._activePipeline = &pipeline;

        s_stateTracker.setAlphaToCoverage(pipeline.descriptor()._alphaToCoverage);

        const PipelineDescriptor& pipelineDescriptor = pipeline.descriptor();
        {
            PROFILE_SCOPE( "Set Raster State", Profiler::Category::Graphics );
            if ( s_stateTracker.setStateBlock( pipelineDescriptor._stateBlock ) == GLStateTracker::BindResult::FAILED )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        {
            PROFILE_SCOPE( "Set Blending", Profiler::Category::Graphics );
            U16 i = 0u;
            s_stateTracker.setBlendColour( pipelineDescriptor._blendStates._blendColour );
            for ( const BlendingSettings& blendState : pipelineDescriptor._blendStates._settings )
            {
                s_stateTracker.setBlending( i++, blendState );
            }
        }

        ShaderResult ret = ShaderResult::Failed;
        ResourcePtr<ShaderProgram> program = Get( pipelineDescriptor._shaderProgramHandle );
        glShaderProgram* glProgram = static_cast<glShaderProgram*>(program);
        if ( glProgram != nullptr )
        {
            {
                PROFILE_SCOPE( "Set Vertex Format", Profiler::Category::Graphics );
                s_stateTracker.setPrimitiveTopology( pipelineDescriptor._primitiveTopology );
                s_stateTracker.setVertexFormat( pipelineDescriptor._vertexFormat, pipeline.vertexFormatHash() );
            }
            {
                PROFILE_SCOPE( "Set Shader Program", Profiler::Category::Graphics );
                // We need a valid shader as no fixed function pipeline is available
                // Try to bind the shader program. If it failed to load, or isn't loaded yet, cancel the draw request for this frame
                ret = Attorney::GLAPIShaderProgram::bind( *glProgram );
            }

            if ( ret != ShaderResult::OK )
            {
                if ( s_stateTracker.setActiveProgram( 0u ) == GLStateTracker::BindResult::FAILED )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
                if ( s_stateTracker.setActiveShaderPipeline( 0u ) == GLStateTracker::BindResult::FAILED )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
                s_stateTracker._activePipeline = nullptr;
            }
            else
            {
                s_stateTracker._activeShaderProgram = glProgram;
            }
            context.descriptorSet( DescriptorSetUsage::PER_DRAW ).dirty(true);
        }
        else
        {
            const auto handle = pipelineDescriptor._shaderProgramHandle;
            Console::errorfn( LOCALE_STR( "ERROR_GLSL_INVALID_HANDLE" ), handle._index, handle._generation );
            s_stateTracker._activePipeline = nullptr;
        }

        return ret;
    }

    GLStateTracker& GL_API::GetStateTracker() noexcept
    {
        return s_stateTracker;
    }

    GLUtil::GLMemory::GLMemoryType GL_API::GetMemoryTypeForUsage( const gl46core::GLenum usage ) noexcept
    {
        assert( usage != gl46core::GL_NONE );
        if ( usage == gl46core::GL_SHADER_STORAGE_BUFFER )
        {
             return GLUtil::GLMemory::GLMemoryType::SHADER_BUFFER;
        }
        if ( usage == gl46core::GL_UNIFORM_BUFFER )
        {
            return GLUtil::GLMemory::GLMemoryType::UNIFORM_BUFFER;
        }
        if (usage == gl46core::GL_ELEMENT_ARRAY_BUFFER)
        {
            return GLUtil::GLMemory::GLMemoryType::INDEX_BUFFER;
        }

        if (usage == gl46core::GL_ARRAY_BUFFER)
        {
            return GLUtil::GLMemory::GLMemoryType::VERTEX_BUFFER;
        }

        return GLUtil::GLMemory::GLMemoryType::OTHER;
    }

    GLUtil::GLMemory::DeviceAllocator& GL_API::GetMemoryAllocator( const GLUtil::GLMemory::GLMemoryType memoryType ) noexcept
    {
        return s_memoryAllocators[to_base( memoryType )];
    }

    void GL_API::QueueFlush() noexcept
    {
        if ( Runtime::isMainThread() )
        {
            gl46core::glFlush();
        }
        else
        {
            s_glFlushQueued.store( true );
        }
    }

    void GL_API::AddDebugMessage( const char* message, const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            gl46core::glPushDebugGroup( gl46core::GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
            gl46core::glPopDebugGroup();
        }
        s_stateTracker._lastInsertedDebugMessage = {message, id};
    }

    void GL_API::PushDebugMessage( const char* message, const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            gl46core::glPushDebugGroup( gl46core::GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
        }
        assert( s_stateTracker._debugScopeDepth < Config::MAX_DEBUG_SCOPE_DEPTH );
        s_stateTracker._debugScope[s_stateTracker._debugScopeDepth++] = { message, id };
    }

    void GL_API::PopDebugMessage()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            gl46core::glPopDebugGroup();
        }
        s_stateTracker._debugScope[s_stateTracker._debugScopeDepth--] = {};
    }

    bool GL_API::DeleteShaderPrograms( const gl46core::GLuint count, gl46core::GLuint* programs )
    {
        if ( count > 0 && programs != nullptr )
        {
            for ( gl46core::GLuint i = 0; i < count; ++i )
            {
                if ( s_stateTracker._activeShaderProgramHandle == programs[i] )
                {
                    if ( s_stateTracker.setActiveProgram( 0u ) == GLStateTracker::BindResult::FAILED )
                    {
                        DIVIDE_UNEXPECTED_CALL();
                    }
                }
                gl46core::glDeleteProgram( programs[i] );
            }

            memset( programs, 0, count * sizeof( gl46core::GLuint ) );
            return true;
        }
        return false;
    }

    bool GL_API::DeleteSamplers( const gl46core::GLuint count, gl46core::GLuint* samplers )
    {
        if ( count > 0 && samplers != nullptr )
        {

            for ( gl46core::GLuint i = 0; i < count; ++i )
            {
                const gl46core::GLuint crtSampler = samplers[i];
                if ( crtSampler != 0 )
                {
                    for ( gl46core::GLuint& boundSampler : s_stateTracker._samplerBoundMap )
                    {
                        if ( boundSampler == crtSampler )
                        {
                            boundSampler = 0;
                        }
                    }
                }
            }
            gl46core::glDeleteSamplers( count, samplers );
            memset( samplers, 0, count * sizeof( gl46core::GLuint ) );
            return true;
        }

        return false;
    }


    bool GL_API::DeleteBuffers( const gl46core::GLuint count, gl46core::GLuint* buffers )
    {
        if ( count > 0 && buffers != nullptr )
        {
            for ( gl46core::GLuint i = 0; i < count; ++i )
            {
                const gl46core::GLuint crtBuffer = buffers[i];

                for ( gl46core::GLuint& boundBuffer : s_stateTracker._activeBufferID )
                {
                    if ( boundBuffer == crtBuffer )
                    {
                        boundBuffer = GL_NULL_HANDLE;
                    }
                }
                if ( s_stateTracker._activeVAOIB == crtBuffer )
                {
                    s_stateTracker._activeVAOIB = GL_NULL_HANDLE;
                }
            }

            gl46core::glDeleteBuffers( count, buffers );
            memset( buffers, 0, count * sizeof( gl46core::GLuint ) );
            return true;
        }

        return false;
    }

    bool GL_API::DeleteFramebuffers( const gl46core::GLuint count, gl46core::GLuint* framebuffers )
    {
        if ( count > 0 && framebuffers != nullptr )
        {
            for ( gl46core::GLuint i = 0; i < count; ++i )
            {
                const gl46core::GLuint crtFB = framebuffers[i];
                for ( gl46core::GLuint& activeFB : s_stateTracker._activeFBID )
                {
                    if ( activeFB == crtFB )
                    {
                        activeFB = GL_NULL_HANDLE;
                    }
                }
            }
            gl46core::glDeleteFramebuffers( count, framebuffers );
            memset( framebuffers, 0, count * sizeof( gl46core::GLuint ) );
            return true;
        }
        return false;
    }

    /// Return the OpenGL sampler object's handle for the given hash value
    gl46core::GLuint GL_API::GetSamplerHandle( const SamplerDescriptor sampler, size_t& samplerHashInOut )
    {
        thread_local size_t cached_hash = 0u;
        thread_local gl46core::GLuint cached_handle = GL_NULL_HANDLE;

        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( samplerHashInOut == SamplerDescriptor::INVALID_SAMPLER_HASH )
        {
            samplerHashInOut = GetHash(sampler);
        }
        DIVIDE_ASSERT( samplerHashInOut != 0u );

        if ( cached_hash == samplerHashInOut )
        {
            return cached_handle;
        }

        cached_hash = samplerHashInOut;

        {
            SharedLock<SharedMutex> r_lock( s_samplerMapLock );
            // If we fail to find the sampler object for the given hash, we print an error and return the default OpenGL handle
            for ( const auto& sampler : s_samplerMap )
            {
                if ( sampler._hash == cached_hash )
                {
                    // Return the OpenGL handle for the sampler object matching the specified hash value
                    cached_handle = sampler._glHandle;
                    return cached_handle;
                }
            }
        }

        LockGuard<SharedMutex> w_lock( s_samplerMapLock );
        // Check again
        for ( const auto& sampler : s_samplerMap )
        {
            if ( sampler._hash == cached_hash )
            {
                // Return the OpenGL handle for the sampler object matching the specified hash value
                cached_handle = sampler._glHandle;
                return cached_handle;
            }
        }

        // Create and store the newly created sample object. GL_API is responsible for deleting these!
        cached_handle = glSamplerObject::Construct( sampler );
        s_samplerMap.emplace_back(cached_hash, cached_handle );
        return cached_handle;
    }

    glHardwareQueryPool* GL_API::GetHardwareQueryPool() noexcept
    {
        return s_hardwareQueryPool.get();
    }

    gl46core::GLsync GL_API::CreateFenceSync()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( s_fenceSyncCounter[s_LockFrameLifetime - 1u] < U32_MAX );

        ++s_fenceSyncCounter[s_LockFrameLifetime - 1u];
        return gl46core::glFenceSync( gl46core::GL_SYNC_GPU_COMMANDS_COMPLETE, gl46core::UnusedMask::GL_UNUSED_BIT );
    }

    void GL_API::DestroyFenceSync( gl46core::GLsync& sync )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_ASSERT( s_fenceSyncCounter[s_LockFrameLifetime - 1u] > 0u );

        --s_fenceSyncCounter[s_LockFrameLifetime - 1u];
        gl46core::glDeleteSync( sync );
        sync = nullptr;
    }

    RenderTarget_uptr GL_API::newRT( const RenderTargetDescriptor& descriptor ) const
    {
        return std::make_unique<glFramebuffer>( _context, descriptor );
    }

    GenericVertexData_ptr GL_API::newGVD( U32 ringBufferLength, const std::string_view name ) const
    {
        return std::make_shared<glGenericVertexData>( _context, ringBufferLength, name );
    }

    ShaderBuffer_uptr GL_API::newSB( const ShaderBufferDescriptor& descriptor ) const
    {
        return std::make_unique<glShaderBuffer>( _context, descriptor );
    }

};
