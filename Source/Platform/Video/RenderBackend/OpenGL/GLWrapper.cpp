// Copyright (c) 2026 DIVIDE-Studio

#include "Headers/GLWrapper.h"
#include "Headers/glDescriptorUpdate.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/RenderSmokeTest.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Utility/Headers/Localization.h"
#include "Utility/Headers/ImageToolsFwd.h"

#include "GUI/Headers/GUI.h"

#include "Platform/Video/Headers/DescriptorSets.h"
#include "Platform/Video/Shaders/Headers/SampledImageResolver.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h"

#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h"
#include "Platform/Video/RenderBackend/OpenGL/Textures/Headers/glSamplerObject.h"

#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glShaderBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glGenericVertexData.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glUniformFences.h"
#include <glbinding/Binding.h>

namespace Divide
{

    NO_DESTROY GLStateTracker GL_API::s_stateTracker;
    std::atomic_bool GL_API::s_glFlushQueued{false};
    NO_DESTROY GLUtil::glTextureViewCache GL_API::s_textureViewCache{};
    std::atomic_uint GL_API::s_fenceSyncCounter[GL_API::s_LockFrameLifetime]{};
    SharedMutex GL_API::s_samplerMapLock;
    NO_DESTROY GL_API::SamplerObjectMap GL_API::s_samplerMap{};
    std::unique_ptr<glHardwareQueryPool> GL_API::s_hardwareQueryPool = nullptr;
    NO_DESTROY fixed_vector<GL_API::TexBindEntry, GLStateTracker::MAX_BOUND_TEXTURE_UNITS> GL_API::s_TexBindQueue;

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
        constexpr gl46core::GLuint64 kOneSecondInNanoSeconds = 1000000000u;

        [[nodiscard]] bool WaitForUniformFence( const gl46core::GLsync sync ) noexcept
        {
            if ( sync == nullptr )
            {
                Console::errorfn( "OpenGL: uniform upload completion fence is missing" );
                return false;
            }

            for ( U8 attempt = 0u; attempt < g_MaxLockWaitRetries; ++attempt )
            {
                const gl46core::GLenum result = gl46core::glClientWaitSync(
                    sync, gl46core::SyncObjectMask::GL_SYNC_FLUSH_COMMANDS_BIT, kOneSecondInNanoSeconds );
                if ( result == gl46core::GL_ALREADY_SIGNALED || result == gl46core::GL_CONDITION_SATISFIED )
                {
                    return true;
                }
                if ( result != gl46core::GL_TIMEOUT_EXPIRED )
                {
                    Console::errorfn( "OpenGL: uniform upload completion fence failed (status {})", to_U32( result ) );
                    return false;
                }
            }

            Console::errorfn( "OpenGL: timed out waiting for uniform upload completion" );
            return false;
        }

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
                    SDL_GL_DestroyContext( contextEntry._context );
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

        struct GLBufferActiveBindConfiguration : BufferBindConfig
        {
            GPUBuffer::Handle _handle{ GPUBuffer::INVALID_HANDLE };
            glGPUBuffer* _buffer{ nullptr };
            size_t _offset{ 0u };

            bool operator==(const GLBufferActiveBindConfiguration& rhs) const noexcept;
        };

        FORCE_INLINE bool GLBufferActiveBindConfiguration::operator==(const GLBufferActiveBindConfiguration& rhs) const noexcept
        {
            return BufferBindConfig::operator==(rhs) &&
                   _handle == rhs._handle &&
                   _offset == rhs._offset;
                   //&& _buffer == rhs._buffer; //Same handle results in same buffer
        }

        void CheckSmokeGLError( const char* operation )
        {
            const gl46core::GLenum error = gl46core::glGetError();
            if ( error != gl46core::GL_NO_ERROR )
            {
                char detail[160]{};
                std::snprintf( detail, sizeof( detail ), "OpenGL %s failed: 0x%x", operation, to_U32( error ) );
                RenderSmokeTest::Fail( "capture", detail, RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
        }

        void VerifySmokeGPU( const char* renderer )
        {
            gl46core::GLint extensionCount = 0;
            gl46core::glGetIntegerv( gl46core::GL_NUM_EXTENSIONS, &extensionCount );
            bool supportsUUID = false;
            for ( gl46core::GLint i = 0; i < extensionCount; ++i )
            {
                const char* extension = reinterpret_cast<const char*>( gl46core::glGetStringi( gl46core::GL_EXTENSIONS, to_U32( i ) ) );
                if ( extension != nullptr &&
                     ( std::strcmp( extension, "GL_EXT_memory_object" ) == 0 || std::strcmp( extension, "GL_EXT_semaphore" ) == 0 ) )
                {
                    supportsUUID = true;
                    break;
                }
            }
            CheckSmokeGLError( "GPU extension query" );
            if ( !supportsUUID )
            {
                RenderSmokeTest::Fail( "gpu_identity", "OpenGL does not expose a compatible device UUID",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            gl46core::GLint deviceCount = 0;
            gl46core::glGetIntegerv( gl46ext::GL_NUM_DEVICE_UUIDS_EXT, &deviceCount );
            CheckSmokeGLError( "GPU UUID count query" );
            if ( deviceCount <= 0 || deviceCount > 16 )
            {
                RenderSmokeTest::Fail( "gpu_identity", "OpenGL reported an invalid device UUID count",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            for ( gl46core::GLint i = 0; i < deviceCount; ++i )
            {
                std::array<U8, 16> uuid{};
                gl46ext::glGetUnsignedBytei_vEXT( gl46ext::GL_DEVICE_UUID_EXT, to_U32( i ), uuid.data() );
                CheckSmokeGLError( "GPU UUID query" );
                if ( RenderSmokeTest::MatchesGPUUUID( uuid, RenderSmokeTest::ExpectedGPU() ) )
                {
                    RenderSmokeTest::ReportGPU( RenderSmokeTest::ExpectedGPU().c_str(), renderer );
                    return;
                }
            }
            RenderSmokeTest::Fail( "gpu_identity", "The OpenGL context does not use the expected Vulkan GPU",
                                   RenderSmokeTest::CAPTURE_FAILURE_EXIT );
        }

        [[nodiscard]] ImageReadbackData CaptureSmokeScene( GFXDevice& context )
        {
            const RenderTarget* target = context.renderTargetPool().getRenderTarget( RenderTargetNames::BACK_BUFFER );
            const RTAttachment* attachment = target != nullptr
                                           ? target->getAttachment( RTAttachmentType::COLOUR, GFXDevice::ScreenTargets::ALBEDO )
                                           : nullptr;
            const Texture* texture = attachment != nullptr ? Get( attachment->texture() ) : nullptr;
            if ( texture == nullptr )
            {
                RenderSmokeTest::Fail( "capture", "Scene colour attachment unavailable", RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }

            CheckSmokeGLError( "before scene readback" );
            gl46core::GLint previousPackBuffer = 0;
            gl46core::glGetIntegerv( gl46core::GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
            GLStateTracker& stateTracker = GL_API::GetStateTracker();
            const PixelAlignment previousPackAlignment = stateTracker._packAlignment;
            gl46core::glBindBuffer( gl46core::GL_PIXEL_PACK_BUFFER, 0u );
            const ImageReadbackData data = texture->readData( 0u, {} );
            stateTracker.setPixelPackAlignment( previousPackAlignment );
            gl46core::glBindBuffer( gl46core::GL_PIXEL_PACK_BUFFER, to_U32( previousPackBuffer ) );
            CheckSmokeGLError( "scene readback" );
            return data;
        }

        [[nodiscard]] vector<Byte> CaptureSmokeEditor( const DisplayWindow& window )
        {
            const auto dimensions = window.getDrawableSize();
            size_t byteCount = 0u;
            if ( !RenderSmokeTest::RGBA8ReadbackSize( dimensions.width, dimensions.height, byteCount ) )
            {
                RenderSmokeTest::Fail( "window_capture", "Unsupported OpenGL framebuffer dimensions",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            vector<Byte> pixels( byteCount );
            CheckSmokeGLError( "before editor readback" );

            gl46core::GLint previousFramebuffer = 0;
            gl46core::GLint previousPackBuffer = 0;
            gl46core::GLint previousAlignment = 0;
            gl46core::GLint previousRowLength = 0;
            gl46core::GLint previousSkipRows = 0;
            gl46core::GLint previousSkipPixels = 0;
            gl46core::glGetIntegerv( gl46core::GL_READ_FRAMEBUFFER_BINDING, &previousFramebuffer );
            gl46core::glGetIntegerv( gl46core::GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
            gl46core::glGetIntegerv( gl46core::GL_PACK_ALIGNMENT, &previousAlignment );
            gl46core::glGetIntegerv( gl46core::GL_PACK_ROW_LENGTH, &previousRowLength );
            gl46core::glGetIntegerv( gl46core::GL_PACK_SKIP_ROWS, &previousSkipRows );
            gl46core::glGetIntegerv( gl46core::GL_PACK_SKIP_PIXELS, &previousSkipPixels );

            gl46core::glBindFramebuffer( gl46core::GL_READ_FRAMEBUFFER, 0u );
            gl46core::GLint previousReadBuffer = 0;
            gl46core::glGetIntegerv( gl46core::GL_READ_BUFFER, &previousReadBuffer );
            gl46core::glReadBuffer( gl46core::GL_BACK );
            gl46core::glBindBuffer( gl46core::GL_PIXEL_PACK_BUFFER, 0u );
            gl46core::glPixelStorei( gl46core::GL_PACK_ALIGNMENT, 1 );
            gl46core::glPixelStorei( gl46core::GL_PACK_ROW_LENGTH, 0 );
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_ROWS, 0 );
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_PIXELS, 0 );
            gl46core::glReadPixels( 0, 0, to_I32( dimensions.width ), to_I32( dimensions.height ),
                                    gl46core::GL_RGBA, gl46core::GL_UNSIGNED_BYTE, pixels.data() );

            gl46core::glPixelStorei( gl46core::GL_PACK_ALIGNMENT, previousAlignment );
            gl46core::glPixelStorei( gl46core::GL_PACK_ROW_LENGTH, previousRowLength );
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_ROWS, previousSkipRows );
            gl46core::glPixelStorei( gl46core::GL_PACK_SKIP_PIXELS, previousSkipPixels );
            gl46core::glBindBuffer( gl46core::GL_PIXEL_PACK_BUFFER, to_U32( previousPackBuffer ) );
            gl46core::glReadBuffer( static_cast<gl46core::GLenum>( previousReadBuffer ) );
            gl46core::glBindFramebuffer( gl46core::GL_READ_FRAMEBUFFER, to_U32( previousFramebuffer ) );
            CheckSmokeGLError( "editor readback" );
            return pixels;
        }
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
        if ( RenderSmokeTest::Enabled() && RenderSmokeTest::SelectedBackend() == RenderSmokeTest::Backend::OPENGL )
        {
            VerifySmokeGPU( gpuRendererStr );
        }

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
        DIVIDE_GPU_ASSERT( deviceInformation._maxTextureUnits >= GLStateTracker::MAX_BOUND_TEXTURE_UNITS );

        GLUtil::getGLValue( gl46core::GL_MAX_VERTEX_ATTRIB_BINDINGS, deviceInformation._maxVertAttributeBindings );

        GLUtil::getGLValue( gl46core::GL_MAX_TEXTURE_SIZE, deviceInformation._maxTextureSize );
        GLUtil::getGLValue( gl46core::GL_MAX_3D_TEXTURE_SIZE, deviceInformation._max3DTextureSize);

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

        if ( SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile") )
        {
            _supportsParallelShaderCompilation = true;
            deviceInformation._shaderCompilerThreads = GLUtil::getGLValue( gl::GL_MAX_SHADER_COMPILER_THREADS_ARB );
            gl::glMaxShaderCompilerThreadsARB( deviceInformation._shaderCompilerThreads );
        }
        Console::printfn( LOCALE_STR( "GL_SHADER_THREADS" ), deviceInformation._shaderCompilerThreads );

        gl46core::glEnable( gl46core::GL_MULTISAMPLE );
        // Line smoothing should almost always be used
        gl46core::glEnable( gl46core::GL_LINE_SMOOTH );

        // GL_FALSE causes a conflict here. Thanks glbinding ...
        //gl46core::glClampColor( gl46core::GL_CLAMP_READ_COLOR, gl46core::GL_FALSE );
        gl46core::glClampColor( gl46core::GL_CLAMP_READ_COLOR, gl46core::GL_NONE );

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
            GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_COUNT, deviceInformation._maxWorkgroupCount[i], i );
            GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_SIZE, deviceInformation._maxWorkgroupSize[i], i );
        }

        deviceInformation._maxWorkgroupInvocations = GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS );
        deviceInformation._maxComputeSharedMemoryBytes = GLUtil::getGLValue( gl46core::GL_MAX_COMPUTE_SHARED_MEMORY_SIZE );

        Console::printfn( LOCALE_STR( "MAX_COMPUTE_WORK_GROUP_INFO" ) ,
                          deviceInformation._maxWorkgroupCount[0], deviceInformation._maxWorkgroupCount[1], deviceInformation._maxWorkgroupCount[2],
                          deviceInformation._maxWorkgroupSize[0], deviceInformation._maxWorkgroupSize[1], deviceInformation._maxWorkgroupSize[2],
                          deviceInformation._maxWorkgroupInvocations );
        Console::printfn( LOCALE_STR( "MAX_COMPUTE_SHARED_MEMORY_SIZE" ), deviceInformation._maxComputeSharedMemoryBytes / 1024 );

        deviceInformation._meshShading._supported = SDL_GL_ExtensionSupported("GL_EXT_mesh_shader");
        if (deviceInformation._meshShading._supported)
        {
            constexpr gl46core::GLuint ignoredErrors[] = { 1002 };
            gl46core::glDebugMessageControl(gl46core::GL_DEBUG_SOURCE_API, gl46core::GL_DEBUG_TYPE_ERROR, gl46core::GL_DONT_CARE, std::size(ignoredErrors), &ignoredErrors[0], gl46core::GL_FALSE);

            for (U8 i = 0u; i < 3u; ++i)
            {
                GLUtil::getGLValue(gl46ext::GL_MAX_MESH_WORK_GROUP_SIZE_EXT,  deviceInformation._meshShading._mesh._maxWorkgroupSize[i], i);
                GLUtil::getGLValue(gl46ext::GL_MAX_MESH_WORK_GROUP_COUNT_EXT, deviceInformation._meshShading._mesh._maxWorkgroupCount[i], i);

                GLUtil::getGLValue(gl46ext::GL_MAX_TASK_WORK_GROUP_SIZE_EXT,  deviceInformation._meshShading._task._maxWorkgroupSize[i], i);
                GLUtil::getGLValue(gl46ext::GL_MAX_TASK_WORK_GROUP_COUNT_EXT, deviceInformation._meshShading._task._maxWorkgroupCount[i], i);
            }

            deviceInformation._meshShading._task._maxWorkgroupInvocations = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_WORK_GROUP_INVOCATIONS_EXT);
            deviceInformation._meshShading._task._maxUniformBlocks = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_UNIFORM_BLOCKS_EXT);
            deviceInformation._meshShading._task._maxTextureImageUnits = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_TEXTURE_IMAGE_UNITS_EXT);
            deviceInformation._meshShading._task._maxAtomicCounterBuffers = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_ATOMIC_COUNTER_BUFFERS_EXT);
            deviceInformation._meshShading._task._maxAtomicCounters = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_ATOMIC_COUNTERS_EXT);
            deviceInformation._meshShading._task._maxImageUniforms = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_IMAGE_UNIFORMS_EXT);
            deviceInformation._meshShading._task._maxShaderStorageBlocks = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_SHADER_STORAGE_BLOCKS_EXT);
            deviceInformation._meshShading._task._maxUniformComponents = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_UNIFORM_COMPONENTS_EXT);
            deviceInformation._meshShading._task._maxCombinedUniformComponents = GLUtil::getGLValue(gl46ext::GL_MAX_COMBINED_TASK_UNIFORM_COMPONENTS_EXT);
            deviceInformation._meshShading._task._maxPayloadSizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_PAYLOAD_SIZE_EXT);
            deviceInformation._meshShading._task._maxSharedMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_SHARED_MEMORY_SIZE_EXT);
            deviceInformation._meshShading._task._maxPayloadAndSharedMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_TASK_PAYLOAD_AND_SHARED_MEMORY_SIZE_EXT);

            deviceInformation._meshShading._mesh._maxWorkgroupInvocations = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_WORK_GROUP_INVOCATIONS_EXT);
            deviceInformation._meshShading._mesh._maxUniformBlocks = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_UNIFORM_BLOCKS_EXT);
            deviceInformation._meshShading._mesh._maxTextureImageUnits = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_TEXTURE_IMAGE_UNITS_EXT);
            deviceInformation._meshShading._mesh._maxAtomicCounterBuffers = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_ATOMIC_COUNTER_BUFFERS_EXT);
            deviceInformation._meshShading._mesh._maxAtomicCounters = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_ATOMIC_COUNTERS_EXT);
            deviceInformation._meshShading._mesh._maxImageUniforms = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_IMAGE_UNIFORMS_EXT);
            deviceInformation._meshShading._mesh._maxShaderStorageBlocks = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_SHADER_STORAGE_BLOCKS_EXT);
            deviceInformation._meshShading._mesh._maxUniformComponents = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_UNIFORM_COMPONENTS_EXT);
            deviceInformation._meshShading._mesh._maxCombinedUniformComponents = GLUtil::getGLValue(gl46ext::GL_MAX_COMBINED_MESH_UNIFORM_COMPONENTS_EXT);
            deviceInformation._meshShading._mesh._maxSharedMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_SHARED_MEMORY_SIZE_EXT);
            deviceInformation._meshShading._mesh._maxPayloadAndSharedMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_PAYLOAD_AND_SHARED_MEMORY_SIZE_EXT);
            deviceInformation._meshShading._mesh._maxOutputMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_OUTPUT_MEMORY_SIZE_EXT);
            deviceInformation._meshShading._mesh._maxPayloadAndOutputMemorySizeBytes = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_PAYLOAD_AND_OUTPUT_MEMORY_SIZE_EXT);
            deviceInformation._meshShading._mesh._maxOutputPrimitives = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_OUTPUT_PRIMITIVES_EXT);
            deviceInformation._meshShading._mesh._maxOutputVertices = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_OUTPUT_VERTICES_EXT);
            deviceInformation._meshShading._mesh._maxOutputComponents = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_OUTPUT_COMPONENTS_EXT);
            deviceInformation._meshShading._mesh._maxOutputLayers = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_OUTPUT_LAYERS_EXT);
            deviceInformation._meshShading._mesh._maxMultiviewViewCount = GLUtil::getGLValue(gl46ext::GL_MAX_MESH_MULTIVIEW_VIEW_COUNT_EXT);
            deviceInformation._meshShading._mesh._perVertexOutputGranularity = GLUtil::getGLValue(gl46ext::GL_MESH_OUTPUT_PER_VERTEX_GRANULARITY_EXT);
            deviceInformation._meshShading._mesh._perPrimitiveOutputGranularity = GLUtil::getGLValue(gl46ext::GL_MESH_OUTPUT_PER_PRIMITIVE_GRANULARITY_EXT);
            GLUtil::getGLValue(gl46ext::GL_MESH_PREFERS_LOCAL_INVOCATION_VERTEX_OUTPUT_EXT, deviceInformation._meshShading._mesh._prefersLocalInvocationVertexOutput);
            GLUtil::getGLValue(gl46ext::GL_MESH_PREFERS_LOCAL_INVOCATION_PRIMITIVE_OUTPUT_EXT, deviceInformation._meshShading._mesh._prefersLocalInvocationPrimitiveOutput);
            GLUtil::getGLValue(gl46ext::GL_MESH_PREFERS_COMPACT_VERTEX_OUTPUT_EXT, deviceInformation._meshShading._mesh._prefersCompactVertexOutput);
            GLUtil::getGLValue(gl46ext::GL_MESH_PREFERS_COMPACT_PRIMITIVE_OUTPUT_EXT, deviceInformation._meshShading._mesh._prefersCompactPrimitiveOutput);

            gl46core::glDebugMessageControl(gl46core::GL_DEBUG_SOURCE_API, gl46core::GL_DEBUG_TYPE_ERROR, gl46core::GL_DONT_CARE, std::size(ignoredErrors), &ignoredErrors[0], gl46core::GL_TRUE);


            Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_VERTICES"),   deviceInformation._meshShading._mesh._maxOutputVertices);
            Console::printfn(LOCALE_STR("MAX_MESH_OUTPUT_PRIMITIVES"), deviceInformation._meshShading._mesh._maxOutputPrimitives);

            Console::printfn(LOCALE_STR("MAX_MESH_SHADER_WORKGROUP_INFO"),
                                        deviceInformation._meshShading._mesh._maxWorkgroupCount[0], deviceInformation._meshShading._mesh._maxWorkgroupCount[1], deviceInformation._meshShading._mesh._maxWorkgroupCount[2],
                                        deviceInformation._meshShading._mesh._maxWorkgroupSize[0],  deviceInformation._meshShading._mesh._maxWorkgroupSize[1],  deviceInformation._meshShading._mesh._maxWorkgroupSize[2],
                                        deviceInformation._meshShading._mesh._maxWorkgroupInvocations);
                                    
            Console::printfn(LOCALE_STR("MAX_TASK_SHADER_WORKGROUP_INFO"),
                                        deviceInformation._meshShading._task._maxWorkgroupCount[0], deviceInformation._meshShading._task._maxWorkgroupCount[1], deviceInformation._meshShading._task._maxWorkgroupCount[2],
                                        deviceInformation._meshShading._task._maxWorkgroupSize[0],  deviceInformation._meshShading._task._maxWorkgroupSize[1],  deviceInformation._meshShading._task._maxWorkgroupSize[2],
                                        deviceInformation._meshShading._task._maxWorkgroupInvocations);
        }

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

        deviceInformation._maxBufferSizeBytes = std::max( deviceInformation._maxSizeBytesUBO, deviceInformation._maxSizeBytesSSBO );
        deviceInformation._maxClipAndCullDistances = GLUtil::getGLValue( gl46core::GL_MAX_COMBINED_CLIP_AND_CULL_DISTANCES );
        deviceInformation._maxClipDistances = to_U32( clipDistanceCount );
        deviceInformation._maxCullDistances = to_U32( cullDistanceCount );

        DIVIDE_GPU_ASSERT(deviceInformation._maxBufferSizeBytes > 0u);

        Console::printfn( LOCALE_STR( "GL_VK_BUFFER_MAX_SIZE" ), deviceInformation._maxBufferSizeBytes / 1024 / 1024);

        {
            // Query the actual encoding of the default framebuffer. We request an sRGB capable one, but the driver/pixel format may not honour it.
            gl46core::GLint colourEncoding = static_cast<gl46core::GLint>(gl46core::GL_LINEAR);
            gl46core::glGetNamedFramebufferAttachmentParameteriv( 0u, gl46core::GL_BACK_LEFT, gl46core::GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &colourEncoding );
            deviceInformation._screenSRGB = colourEncoding == static_cast<gl46core::GLint>(gl46core::GL_SRGB);
            if ( !deviceInformation._screenSRGB )
            {
                Console::warnfn( LOCALE_STR("WARN_GL_NO_SRGB_FRAMEBUFFER") );
            }
        }

        GFXDevice::OverrideDeviceInformation( deviceInformation );
        // Seamless cubemaps are a nice feature to have enabled (core since 3.2)
        gl46core::glEnable( gl46core::GL_TEXTURE_CUBE_MAP_SEAMLESS  );
        // Always on for render targets (sRGB attachments encode on write). Toggled off for the default framebuffer if it isn't sRGB capable (see BEGIN_RENDER_PASS)
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
        DIVIDE_GPU_ASSERT( _dummyVAO != GL_NULL_HANDLE, LOCALE_STR( "ERROR_VAO_INIT" ) );

        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            GLUtil::SetObjectLabel( gl46core::GL_VERTEX_ARRAY, _dummyVAO, "GENERIC_VAO" );
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

        _deviceInitialized = true;
        // That's it. Everything should be ready for draw calls
        Console::printfn( LOCALE_STR( "START_OGL_API_OK" ) );
        return ErrorCode::NO_ERR;
    }

    /// Clear everything that was setup in initRenderingAPI()
    void GL_API::closeRenderingAPI()
    {
        discardBufferReadbacks();
        glShaderProgram::DestroyStaticData();
        _stableSampledBindingCache.Clear();

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
        if ( _passTimestampQueriesCreated )
        {
            for ( auto& queries : _passTimestampQueries )
            {
                gl46core::glDeleteQueries( to_I32( queries.size() ), queries.data() );
                queries.fill( 0u );
            }
            _passTimestampCounts.fill( 0u );
            _passTimestampQueriesCreated = false;
        }
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
        _deviceInitialized = false;
    }

    void GL_API::endPerformanceQueries()
    {
        if ( _runQueries )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

            // End the timing query started in beginFrame() in debug builds
            for ( U8 i = firstPerformanceQuery(); i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                _performanceQueries[i]->end();
            }
        }
    }

    U8 GL_API::firstPerformanceQuery() const noexcept
    {
        static_assert( to_base( GlobalQueryTypes::GPU_TIME ) + 1u == to_base( GlobalQueryTypes::COUNT ) );
        return _runPipelineStatQueries ? 0u : to_base( GlobalQueryTypes::GPU_TIME );
    }

    /// Prepare the GPU for rendering a frame
    bool GL_API::drawToWindow( DisplayWindow& window )
    {
        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ) );
        GLUtil::s_glMainRenderWindow = &window;
        pollBufferReadbacks( window.getGUID() );
        return true;
    }

    void GL_API::onRenderThreadLoopStart( )
    {
       //GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), nullptr );
    }

    void GL_API::onRenderThreadLoopEnd()
    {
    }

    bool GL_API::prepareFlushWindow( DisplayWindow& window )
    {
        _screenPassRecorded = false;
        GLUtil::ValidateSDL( SDL_GL_MakeCurrent( window.getRawWindow(), window.userData()._glContext ));
        return true;
    }

    void GL_API::flushWindow( DisplayWindow& window )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        const bool mainWindow = window.getGUID() == _context.context().mainWindow().getGUID();
        const U32 presentsBeforeCapture = RenderSmokeTest::PresentCount();
        const bool capture = mainWindow && _screenPassRecorded && RenderSmokeTest::TryBeginCapture();
        ImageReadbackData sceneData{};
        vector<Byte> editorPixels{};
        if ( capture )
        {
            sceneData = CaptureSmokeScene( _context );
            if ( RenderSmokeTest::WindowCaptureRequested() )
            {
                editorPixels = CaptureSmokeEditor( window );
            }
        }
        if ( mainWindow )
        {
            endPerformanceQueries();
            _swapBufferTimer.start();
        }
        bool swapSucceeded = false;
        {
            PROFILE_SCOPE( "GL_API: Swap Buffers", Profiler::Category::Graphics );
            swapSucceeded = SDL_GL_SwapWindow( window.getRawWindow() );
        }

        RenderSmokeTest::RecordPresent( mainWindow && _screenPassRecorded, swapSucceeded, false );
        if ( RenderSmokeTest::Enabled() && !swapSucceeded )
        {
            RenderSmokeTest::Fail( "present", SDL_GetError(), RenderSmokeTest::CAPTURE_FAILURE_EXIT );
        }
        if ( capture )
        {
            if ( RenderSmokeTest::PresentCount() <= presentsBeforeCapture )
            {
                RenderSmokeTest::Fail( "capture", "The captured scene frame was not presented successfully",
                                       RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            const ResourcePath directory{ RenderSmokeTest::OutputDirectory() };
            if ( sceneData._data.empty() ||
                 !ImageTools::SaveImage( directory / "scene.png", sceneData._width, sceneData._height,
                                         sceneData._numComponents, sceneData._bpp, sceneData._sourceIsBGR,
                                         sceneData._data.data(), ImageTools::SaveImageFormat::PNG ) )
            {
                RenderSmokeTest::FinishCapture( false );
                RenderSmokeTest::Fail( "capture", "Unable to save scene image", RenderSmokeTest::CAPTURE_FAILURE_EXIT );
            }
            if ( RenderSmokeTest::WindowCaptureRequested() )
            {
                const auto dimensions = window.getDrawableSize();
                if ( editorPixels.empty() ||
                     !ImageTools::SaveImage( directory / "editor.pending.png", to_U16( dimensions.width ), to_U16( dimensions.height ),
                                             4u, 4u, false, editorPixels.data(), ImageTools::SaveImageFormat::PNG ) )
                {
                    RenderSmokeTest::Fail( "window_capture", "Unable to save editor image", RenderSmokeTest::CAPTURE_FAILURE_EXIT );
                }
            }
            RenderSmokeTest::FinishCapture( true );
            if ( RenderSmokeTest::WindowCaptureRequested() )
            {
                RenderSmokeTest::FinishWindowCapture( true );
            }
        }

        if ( mainWindow ) 
        {
            _swapBufferTimer.stop();
        }
        else if ( window.userData()._glContext != _context.context().mainWindow().userData()._glContext )
        {
            gl46core::GLsync fence = CreateFenceSync();
            if ( fence == nullptr )
            {
                Console::errorfn( "OpenGL: failed to create a window uniform upload completion fence" );
                _uniformContextFenceFailed = true;
            }
            else
            {
                gl46core::glFlush();
                _uniformContextFences[GFXDevice::FrameCount() % Config::MAX_FRAMES_IN_FLIGHT].push_back( fence );
            }
        }
    }

    bool GL_API::frameStarted()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DisplayWindow& mainWindow = _context.context().mainWindow();
        if ( SDL_GL_GetCurrentContext() != mainWindow.userData()._glContext &&
             !GLUtil::ValidateSDL( SDL_GL_MakeCurrent( mainWindow.getRawWindow(), mainWindow.userData()._glContext ), false ) )
        {
            return false;
        }

        auto& contextFences = _uniformContextFences[GFXDevice::FrameCount() % Config::MAX_FRAMES_IN_FLIGHT];
        for ( gl46core::GLsync& fence : contextFences )
        {
            if ( fence == nullptr )
            {
                continue;
            }
            if ( !WaitForUniformFence( fence ) )
            {
                return false;
            }
            DestroyFenceSync( fence );
        }
        contextFences.clear();

        // Start a duration query in debug builds
        if ( _runQueries )
        {
            for ( U8 i = firstPerformanceQuery(); i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                _performanceQueries[i]->begin();
            }
        }

        while ( !s_stateTracker._endFrameFences.empty() )
        {
            auto& sync = s_stateTracker._endFrameFences.front();
            const U64 frameNumber = GFXDevice::FrameCount();
            const bool mustWait = frameNumber >= Config::MAX_FRAMES_IN_FLIGHT &&
                                  sync.second <= frameNumber - Config::MAX_FRAMES_IN_FLIGHT;
            const gl46core::GLenum waitRet = gl46core::glClientWaitSync( sync.first, gl46core::SyncObjectMask::GL_NONE_BIT, 0u );
            if ( waitRet == gl46core::GL_ALREADY_SIGNALED || waitRet == gl46core::GL_CONDITION_SATISFIED ||
                 ( waitRet == gl46core::GL_TIMEOUT_EXPIRED && mustWait && WaitForUniformFence( sync.first ) ) )
            {
                s_stateTracker._lastSyncedFrameNumber.store( sync.second, std::memory_order_relaxed );
                DestroyFenceSync( sync.first );
                s_stateTracker._endFrameFences.pop();
            }
            else if ( waitRet == gl46core::GL_TIMEOUT_EXPIRED && !mustWait )
            {
                break;
            }
            else
            {
                Console::errorfn( "OpenGL: frame completion fence failed (status {})", to_U32( waitRet ) );
                return false;
            }
        }

        LockManager::CleanExpiredSyncObjects( RenderAPI::OpenGL, s_stateTracker._lastSyncedFrameNumber.load( std::memory_order_relaxed ) );

        return true;
    }

    bool GL_API::frameEnded()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( _uniformContextFenceFailed )
        {
            return false;
        }
        DisplayWindow& mainWindow = _context.context().mainWindow();
        if ( !GLUtil::ValidateSDL( SDL_GL_MakeCurrent( mainWindow.getRawWindow(), mainWindow.userData()._glContext ), false ) )
        {
            return false;
        }

        GLUtil::GLMemory::OnFrameEnd( GFXDevice::FrameCount() );

        for ( U32 i = 0u; i < GL_API::s_LockFrameLifetime - 1; ++i )
        {
            s_fenceSyncCounter[i].store(s_fenceSyncCounter[i + 1].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        PROFILE_SCOPE( "GL_API: Post-Swap cleanup", Profiler::Category::Graphics );
        s_textureViewCache.onFrameEnd();
        s_glFlushQueued.store( false );
        s_stateTracker.onFrameEnd();

        if ( _runQueries )
        {
            PROFILE_SCOPE( "GL_API: Time Query", Profiler::Category::Graphics );

            const U8 firstQuery = firstPerformanceQuery();
            thread_local std::array<I64, to_base( GlobalQueryTypes::COUNT )> results{};
            for ( U8 i = firstQuery; i < to_base( GlobalQueryTypes::COUNT ); ++i )
            {
                results[i] = _performanceQueries[i]->getResultNoWait();
                _performanceQueries[i]->incQueue();
            }

            _context.getPerformanceMetrics()._gpuTimeInMS             = Time::NanosecondsToMilliseconds<F32>( results[to_base( GlobalQueryTypes::GPU_TIME )] );
            if ( _runPipelineStatQueries )
            {
                _context.getPerformanceMetrics()._verticesSubmitted       = to_U64( results[to_base( GlobalQueryTypes::VERTICES_SUBMITTED )] );
                _context.getPerformanceMetrics()._primitivesGenerated     = to_U64( results[to_base( GlobalQueryTypes::PRIMITIVES_GENERATED )] );
                _context.getPerformanceMetrics()._tessellationPatches     = to_U64( results[to_base( GlobalQueryTypes::TESSELLATION_PATCHES )] );
                _context.getPerformanceMetrics()._tessellationInvocations = to_U64( results[to_base( GlobalQueryTypes::TESSELLATION_EVAL_INVOCATIONS )] );
            }
        }

        const size_t fenceSize = std::size( s_fenceSyncCounter );

        for ( size_t i = 0u; i < std::size( _context.getPerformanceMetrics()._syncObjectsInFlight ); ++i )
        {
            _context.getPerformanceMetrics()._syncObjectsInFlight[i] = i < fenceSize ? s_fenceSyncCounter[i].load(std::memory_order_relaxed) : 0u;
        }

        _context.getPerformanceMetrics()._generatedRenderTargetCount = to_U32( _context.renderTargetPool().getRenderTargets().size() );
        _runQueries = _context.queryPerformanceStats();
        _runPipelineStatQueries = _runQueries && _context.queryPipelineStatistics();

        gl46core::GLsync frameFence = CreateFenceSync();
        if ( frameFence == nullptr )
        {
            Console::errorfn( "OpenGL: failed to create a frame completion fence" );
            return false;
        }
        s_stateTracker._endFrameFences.push( std::make_pair( frameFence, GFXDevice::FrameCount() ) );
        _context.getPerformanceMetrics()._queuedGPUFrames = s_stateTracker._endFrameFences.size();

        const gl46core::GLenum resetStatus = gl46core::glGetGraphicsResetStatus();
        if ( resetStatus != gl46core::GL_NO_ERROR )
        {
            Console::errorfn( "OpenGL: device reset at frame end (status {})", to_U32( resetStatus ) );
            if ( RenderSmokeTest::Enabled() )
            {
                RenderSmokeTest::Fail( "device_lost", "OpenGL device reset at frame end",
                                       RenderSmokeTest::DEVICE_LOST_EXIT );
            }
            return false;
        }

        clearStates(s_stateTracker);
        return true;
    }

    bool GL_API::waitForGPUIdle() noexcept
    {
        if ( !_deviceInitialized )
        {
            return true;
        }

        DisplayWindow& mainWindow = _context.context().mainWindow();
        if ( !GLUtil::ValidateSDL( SDL_GL_MakeCurrent( mainWindow.getRawWindow(), mainWindow.userData()._glContext ), false ) )
        {
            return false;
        }

        const bool fencesCompleted = !_uniformContextFenceFailed && GLUtil::FinishUniformContextFences(
            std::span{ _uniformContextFences },
            []( const gl46core::GLsync fence ) { return WaitForUniformFence( fence ); },
            []
            {
                gl46core::GLsync fence = CreateFenceSync();
                if ( fence == nullptr )
                {
                    Console::errorfn( "OpenGL: failed to create a shutdown completion fence" );
                    return false;
                }

                gl46core::glFlush();
                const bool completed = WaitForUniformFence( fence );
                DestroyFenceSync( fence );
                return completed;
            },
            []( gl46core::GLsync& fence ) { DestroyFenceSync( fence ); } );

        bool success = fencesCompleted;
        const gl46core::GLenum resetStatus = gl46core::glGetGraphicsResetStatus();
        if ( resetStatus != gl46core::GL_NO_ERROR )
        {
            Console::errorfn( "OpenGL: device reset while waiting for GPU idle (status {})", to_U32( resetStatus ) );
            if ( RenderSmokeTest::Enabled() )
            {
                RenderSmokeTest::Fail( "device_lost", "OpenGL device reset while waiting for GPU idle",
                                       RenderSmokeTest::DEVICE_LOST_EXIT );
            }
            success = false;
        }

        if ( !success )
        {
            return false;
        }

        while ( !s_stateTracker._endFrameFences.empty() )
        {
            DestroyFenceSync( s_stateTracker._endFrameFences.front().first );
            s_stateTracker._endFrameFences.pop();
        }
        return success;
    }

    void GL_API::idle( [[maybe_unused]] const bool fast )
    {
        glShaderProgram::Idle( _context.context() );
    }

    bool GL_API::queueBufferReadback( BufferReadbackRequest&& request )
    {
        if ( request._buffer == nullptr || request._target.first == nullptr || request._windowGUID == I64_LOWEST )
        {
            return false;
        }

        PendingBufferReadback pending{};
        pending._sync = CreateFenceSync();
        if ( pending._sync == nullptr )
        {
            DIVIDE_GPU_ASSERT( false, "GL_API::queueBufferReadback: failed to create completion fence" );
            return false;
        }

        pending._request = MOV( request );
        _pendingBufferReadbacks.push_back( MOV( pending ) );
        return true;
    }

    void GL_API::pollBufferReadbacks( const I64 windowGUID )
    {
        for ( auto it = _pendingBufferReadbacks.begin(); it != _pendingBufferReadbacks.end(); )
        {
            if ( it->_request._windowGUID != windowGUID )
            {
                ++it;
                continue;
            }

            const gl46core::GLenum result = gl46core::glClientWaitSync( it->_sync, gl46core::SyncObjectMask::GL_NONE_BIT, 0u );
            if ( result == gl46core::GL_TIMEOUT_EXPIRED )
            {
                ++it;
                continue;
            }

            const bool signaled = result == gl46core::GL_ALREADY_SIGNALED ||
                                  result == gl46core::GL_CONDITION_SATISFIED;
            if ( !signaled )
            {
                DIVIDE_GPU_ASSERT( false, "GL_API::pollBufferReadbacks: fence wait failed" );
            }
            else
            {
                it->_request._buffer->readData(
                    { it->_request._offsetElementCount, it->_request._elementCount },
                    it->_request._target,
                    it->_request._queueIndex );
            }

            if ( it->_request._completion )
            {
                it->_request._completion( signaled );
            }
            DestroyFenceSync( it->_sync );
            it = _pendingBufferReadbacks.erase( it );
        }
    }

    void GL_API::discardBufferReadbacks()
    {
        for ( PendingBufferReadback& pending : _pendingBufferReadbacks )
        {
            DestroyFenceSync( pending._sync );
            if ( pending._request._completion )
            {
                pending._request._completion( false );
            }
        }
        _pendingBufferReadbacks.clear();
    }


    bool GL_API::Draw(GenericDrawCommand cmd)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        DIVIDE_GPU_ASSERT(cmd._drawCount < GFXDevice::GetDeviceInformation()._maxDrawIndirectCount);

        if ( cmd._sourceBuffersCount == 0u )
        {
            DIVIDE_GPU_ASSERT(cmd._cmd.indexCount == 0u);

            if (cmd._cmd.vertexCount == 0u)
            {
                GenericDrawCommand drawCmd = cmd;
                switch (GL_API::s_stateTracker._activeTopology)
                {
                    case PrimitiveTopology::POINTS: drawCmd._cmd.vertexCount = 1u; break;
                    case PrimitiveTopology::LINES:
                    case PrimitiveTopology::LINE_STRIP:
                    case PrimitiveTopology::LINE_STRIP_ADJACENCY:
                    case PrimitiveTopology::LINES_ADJACENCY: drawCmd._cmd.vertexCount = 2u; break;
                    case PrimitiveTopology::TRIANGLES:
                    case PrimitiveTopology::TRIANGLE_STRIP:
                    case PrimitiveTopology::TRIANGLE_FAN:
                    case PrimitiveTopology::TRIANGLES_ADJACENCY:
                    case PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY: drawCmd._cmd.vertexCount = 3u; break;
                    case PrimitiveTopology::PATCH: drawCmd._cmd.vertexCount = 4u; break;
                    default: return false;
                }
                GLUtil::SubmitRenderCommand(drawCmd);
            }
            else
            {
                GLUtil::SubmitRenderCommand(cmd);
            }

            return true;
        }

        assert(cmd._sourceBuffers != nullptr);

        bool ret = true;
        bool hasIndexBuffer = false;

        gl46core::GLenum indexFormat = gl46core::GL_NONE;
        U32 firstIndex = cmd._cmd.firstIndex;

        // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
        thread_local GLBufferActiveBindConfiguration s_lastVB = {};
        thread_local GLBufferActiveBindConfiguration s_lastIB = {};

        for ( size_t i = 0u; i < cmd._sourceBuffersCount; ++i )
        {
            GLBufferActiveBindConfiguration activeConfig{};
            activeConfig._handle = cmd._sourceBuffers[i];
            GPUBuffer* buffer = GPUBuffer::s_BufferPool.find(activeConfig._handle);

            DIVIDE_GPU_ASSERT(buffer != nullptr, "GL_API::Draw - Invalid GPU buffer handle!");
            activeConfig._buffer = static_cast<glGPUBuffer*>(buffer);
            glBufferImpl* impl = activeConfig._buffer->_internalBuffer.get();
            DIVIDE_GPU_ASSERT(impl != nullptr, "GL_API::Draw - GPU buffer has no internal implementation!");

            const size_t elementSizeInBytes = impl->_params._elementSize;
            activeConfig._bindIdx = activeConfig._buffer->_bindConfig._bindIdx;
            activeConfig._offset = impl->getDataOffset();
            if (activeConfig._buffer->queueLength() > 1)
            {
                activeConfig._offset += impl->_params._elementCount * elementSizeInBytes * activeConfig._buffer->queueIndex();
            }
            activeConfig._elementStride = activeConfig._buffer->_bindConfig._elementStride == BufferBindConfig::INVALID_ELEMENT_STRIDE
                                                                                            ? elementSizeInBytes
                                                                                            : activeConfig._buffer->_bindConfig._elementStride;
            
            if ( impl->_params._usageType == BufferUsageType::VERTEX_BUFFER )
            {
                if ( s_lastVB != activeConfig )
                {
                    s_lastVB = activeConfig;
                    DIVIDE_EXPECTED_CALL
                    (
                        GetStateTracker().bindActiveBuffer(activeConfig._bindIdx,
                                                           impl->getBufferHandle(),
                                                           activeConfig._offset,
                                                           activeConfig._elementStride) != GLStateTracker::BindResult::FAILED
                    );
                }
            }
            else if ( impl->_params._usageType == BufferUsageType::INDEX_BUFFER )
            {
                DIVIDE_GPU_ASSERT(activeConfig._buffer->firstIndexOffsetCount() != GPUBuffer::INVALID_INDEX_OFFSET);

                if (s_lastIB != activeConfig)
                {
                    s_lastIB = activeConfig;
                }

                DIVIDE_GPU_ASSERT(!hasIndexBuffer, "GL_API::Draw - Multiple index buffers bound!");
                hasIndexBuffer = true;

                firstIndex += to_U32(activeConfig._offset / elementSizeInBytes);
                firstIndex += activeConfig._buffer->firstIndexOffsetCount();
                indexFormat = elementSizeInBytes == sizeof(U16) ? gl46core::GL_UNSIGNED_SHORT : gl46core::GL_UNSIGNED_INT;

                DIVIDE_EXPECTED_CALL
                (
                    GL_API::GetStateTracker().setActiveBuffer(gl46core::GL_ELEMENT_ARRAY_BUFFER, impl->getBufferHandle()) != GLStateTracker::BindResult::FAILED
                );
                cmd._cmd.firstIndex = firstIndex;
            }
            else
            {
                DIVIDE_UNEXPECTED_CALL_MSG("GL_API::Draw - Unsupported buffer usage type for drawing!");
            }
        }

        if ( indexFormat != gl46core::GL_NONE )
        {
            GLUtil::SubmitRenderCommand(cmd, indexFormat);
        }
        else
        {
            GLUtil::SubmitRenderCommand(cmd);
        }

        return ret;
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
            DIVIDE_EXPECTED_CALL( s_stateTracker.bindTexture( texEntry._slot, texEntry._handle, texEntry._sampler ) != GLStateTracker::BindResult::FAILED );
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
                        DIVIDE_EXPECTED_CALL( s_stateTracker.bindTextures( bindOffset, idx, s_textureHandles.data(), s_textureSamplers.data() ) != GLStateTracker::BindResult::FAILED );

                        idx = 0u;
                    }
                }
                // Handle the final range (if any)
                if ( idx > 0u )
                {
                    DIVIDE_EXPECTED_CALL( s_stateTracker.bindTextures( bindOffset, idx, s_textureHandles.data(), s_textureSamplers.data() ) != GLStateTracker::BindResult::FAILED );
                }
            }
            else
            {
                for ( const TexBindEntry& texEntry : s_TexBindQueue )
                {
                    DIVIDE_EXPECTED_CALL( s_stateTracker.bindTexture( texEntry._slot, texEntry._handle, texEntry._sampler ) != GLStateTracker::BindResult::FAILED );
                }
            }
        }

        s_TexBindQueue.clear();
    }

    gl46core::GLuint GL_API::getGLTextureView( const ImageView srcView, const size_t srcViewHash, const U8 lifetimeInFrames ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        auto [handle, cacheHit] = s_textureViewCache.allocate( srcViewHash );

        if ( !cacheHit )
        {
            const glTexture* srcTexture = static_cast<const glTexture*>(srcView._srcTexture);

            const gl46core::GLuint srcHandle = srcTexture->textureHandle();
            
            if ( srcHandle == GL_NULL_HANDLE )
            {
                return srcHandle;
            }

            const gl46core::GLenum glInternalFormat = GLUtil::InternalFormatAndDataType( srcView._descriptor._baseFormat,
                                                                                         srcView._descriptor._dataType,
                                                                                         srcView._descriptor._packing )._format;

            const bool isCube = IsCubeTexture( srcTexture->descriptor()._texType );
            const bool isCubeView = IsCubeTexture( TargetType( srcView ) );
            const U32 texLayers = srcTexture->descriptor()._texType == TextureType::TEXTURE_3D
                                ? srcTexture->depth()
                                : srcTexture->layerCount() * (isCube ? 6u : 1u);
            const U16 viewLayerCount = srcView._subRange._layerRange._count;
            const U16 viewLayerOffset = srcView._subRange._layerRange._offset;

            const gl46core::GLuint glMinLayer = to_U32( viewLayerOffset * (isCubeView ? 6u : 1u) );
            const gl46core::GLuint glLayerCount = (viewLayerCount == ALL_LAYERS)
                                                          ? to_U32( texLayers - glMinLayer )
                                                          : to_U32( viewLayerCount * (isCubeView ? 6u : 1u) );

            PROFILE_SCOPE( "GL: cache miss  - Image", Profiler::Category::Graphics );
            gl46core::glTextureView( handle,
                                     GLUtil::internalTextureType( TargetType( srcView ), srcView._descriptor._msaaSamples ),
                                     srcHandle,
                                     glInternalFormat,
                                     static_cast<gl46core::GLuint>(srcView._subRange._mipLevels._offset),
                                     static_cast<gl46core::GLuint>(srcView._subRange._mipLevels._count),
                                     glMinLayer,
                                     glLayerCount);
        }

        s_textureViewCache.deallocate( handle, lifetimeInFrames );

        return handle;
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
                    _screenPassRecorded = true;

                    DIVIDE_EXPECTED_CALL( s_stateTracker.setActiveFB( RenderTarget::Usage::RT_WRITE_ONLY, 0u ) != GLStateTracker::BindResult::FAILED );
                    if ( !GFXDevice::GetDeviceInformation()._screenSRGB )
                    {
                        // Some drivers encode anyway if this is enabled, so make sure shader-side encoding isn't applied twice
                        gl46core::glDisable( gl46core::GL_FRAMEBUFFER_SRGB );
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
                    PushDebugMessage( _context.context().config(), crtCmd->_name.c_str(), SCREEN_TARGET_ID );
                }
                else
                {
                    PROFILE_SCOPE( "Begin Render Target", Profiler::Category::Graphics );

                    glFramebuffer* rt = static_cast<glFramebuffer*>(_context.renderTargetPool().getRenderTarget( crtCmd->_target ));

                    const GLStateTracker::BindResult bindResult = s_stateTracker.setActiveFB( RenderTarget::Usage::RT_WRITE_ONLY, rt->framebufferHandle() );
                    DIVIDE_EXPECTED_CALL( bindResult != GLStateTracker::BindResult::FAILED );

                    Attorney::GLAPIRenderTarget::begin( *rt, crtCmd->_descriptor, crtCmd->_clearDescriptor );
                    s_stateTracker._activeRenderTarget = rt;
                    s_stateTracker._activeRenderTargetDimensions = { rt->getWidth(), rt->getHeight() };

                    const Configuration& config = _context.context().config();
                    PushDebugMessage(config, crtCmd->_name.c_str(), crtCmd->_target);
                    AddDebugMessage(config, rt->debugMessage().c_str(), crtCmd->_target );
                }
            }break;
            case GFX::CommandType::END_RENDER_PASS:
            {
                PROFILE_SCOPE( "END_RENDER_PASS", Profiler::Category::Graphics );

                PopDebugMessage(_context.context().config());

                if ( GL_API::s_stateTracker._activeRenderTarget == nullptr )
                {
                    assert( GL_API::s_stateTracker._activeRenderTargetID == SCREEN_TARGET_ID );
                    if ( !GFXDevice::GetDeviceInformation()._screenSRGB )
                    {
                        gl46core::glEnable( gl46core::GL_FRAMEBUFFER_SRGB );
                    }
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

                    DIVIDE_GPU_ASSERT( crtCmd->_resultContainer != nullptr );

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
                else if ( _lastPushConstants.dataFlags() != PushConstantsStruct::DataFlags::NONE )
                {
                    Attorney::GLAPIShaderProgram::uploadPushConstants( *s_stateTracker._activeShaderProgram, _lastPushConstants );
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
                    s_stateTracker._activeShaderProgram->uploadUniformData( *uniforms );
                }
                if ( pushConstantsCmd->_fastData.dataFlags() != PushConstantsStruct::DataFlags::NONE )
                {
                    _lastPushConstants.merge( pushConstantsCmd->_fastData );
                    Attorney::GLAPIShaderProgram::uploadPushConstants( *s_stateTracker._activeShaderProgram, _lastPushConstants );
                }

            } break;
            case GFX::CommandType::BEGIN_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "BEGIN_DEBUG_SCOPE", Profiler::Category::Graphics );

                const auto& crtCmd = cmd->As<GFX::BeginDebugScopeCommand>();
                PushDebugMessage( _context.context().config(), crtCmd->_scopeName.c_str(), crtCmd->_scopeId );
            } break;
            case GFX::CommandType::END_DEBUG_SCOPE:
            {
                PROFILE_SCOPE( "END_DEBUG_SCOPE", Profiler::Category::Graphics );

                PopDebugMessage( _context.context().config() );
            } break;
            case GFX::CommandType::ADD_DEBUG_MESSAGE:
            {
                PROFILE_SCOPE( "ADD_DEBUG_MESSAGE", Profiler::Category::Graphics );

                const auto& crtCmd = cmd->As<GFX::AddDebugMessageCommand>();
                AddDebugMessage( _context.context().config(),crtCmd->_msg.c_str(), crtCmd->_msgId );
            }break;
            case GFX::CommandType::COMPUTE_MIPMAPS:
            {
                PROFILE_SCOPE( "COMPUTE_MIPMAPS", Profiler::Category::Graphics );

                const GFX::ComputeMipMapsCommand* crtCmd = cmd->As<GFX::ComputeMipMapsCommand>();

                ResourcePtr<Texture> tex = Get(crtCmd->_texture);
                const U16 texLayers = IsCubeTexture( tex->descriptor()._texType ) ? tex->depth() * 6u : tex->depth();
                const U16 layerCount = crtCmd->_layerRange._count == ALL_LAYERS ? texLayers : crtCmd->_layerRange._count;

                if ( crtCmd->_layerRange._offset == 0 && layerCount >= texLayers )
                {
                    PROFILE_SCOPE( "GL: In-place computation - Full", Profiler::Category::Graphics );
                    gl46core::glGenerateTextureMipmap( static_cast<glTexture*>(tex)->textureHandle() );
                }
                else
                {
                    PROFILE_SCOPE( "GL: View-based computation", Profiler::Category::Graphics );
                    DIVIDE_GPU_ASSERT( crtCmd->_mipRange._count != 0u );

                    ImageView view = tex->getView();
                    view._subRange._layerRange = crtCmd->_layerRange;
                    view._subRange._mipLevels =  crtCmd->_mipRange;

                    const TextureType targetType = TargetType( view );
                    DIVIDE_GPU_ASSERT( targetType != TextureType::COUNT );

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

                if ( !_context.uniformUploadFailed() && s_stateTracker._activePipeline != nullptr )
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

                if ( !_context.uniformUploadFailed() && s_stateTracker._activePipeline != nullptr )
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
                    };
                }
            }break;
            case GFX::CommandType::MEMORY_BARRIER:
            {
                PROFILE_SCOPE( "MEMORY_BARRIER", Profiler::Category::Graphics );

                const GFX::MemoryBarrierCommand* crtCmd = cmd->As<GFX::MemoryBarrierCommand>();

                gl46core::MemoryBarrierMask mask = gl46core::MemoryBarrierMask::GL_NONE_BIT;

                SyncObjectHandle handle{};
                for ( const BufferLock& lock : crtCmd->_bufferLocks )
                {
                    if ( lock._buffer == nullptr || lock._range._length == 0u)
                    {
                        continue;
                    }

                    glBufferImpl* buffer = static_cast<glBufferImpl*>(lock._buffer);
                    switch ( lock._type )
                    {
                        case BufferSyncUsage::CPU_WRITE_TO_GPU_READ:
                        {
                            if ( handle._id == SyncObjectHandle::INVALID_SYNC_ID )
                            {
                                handle = LockManager::CreateSyncObject( RenderAPI::OpenGL );
                            }

                            DIVIDE_EXPECTED_CALL( buffer->lockRange( lock._range, handle ) );
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_CPU_READ:
                        {
                            if ( buffer->_params._updateFrequency == BufferUpdateFrequency::ONCE )
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
                            switch ( buffer->_params._usageType )
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
                                default: DIVIDE_UNEXPECTED_CALL();
                            }
                        } break;
                        case BufferSyncUsage::GPU_WRITE_TO_GPU_WRITE:
                        {
                            mask |= gl46core::GL_SHADER_STORAGE_BARRIER_BIT;
                        } break;
                        case BufferSyncUsage::CPU_READ_TO_CPU_WRITE:
                        case BufferSyncUsage::CPU_WRITE_TO_CPU_WRITE:
                        {
                            NOP();
                        }break;
                        default: DIVIDE_UNEXPECTED_CALL();
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

                        default: DIVIDE_UNEXPECTED_CALL();
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
            default: DIVIDE_UNEXPECTED_CALL();
        }
    }

    void GL_API::postFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool expected = true;
        if ( s_glFlushQueued.compare_exchange_strong( expected, false ) )
        {
            PROFILE_SCOPE( "GL_FLUSH", Profiler::Category::Graphics );
            gl46core::glFlush();
        }
        GetStateTracker()._activeRenderTargetID = INVALID_RENDER_TARGET_ID;
        GetStateTracker()._activeRenderTargetDimensions = _context.context().mainWindow().getDrawableSize();
    }

    void GL_API::writePassTimestamp( const U32 index )
    {
        if ( index >= GPU_PASS_TIMESTAMP_CAPACITY ) [[unlikely]]
        {
            return;
        }

        if ( !_passTimestampQueriesCreated ) [[unlikely]]
        {
            for ( auto& queries : _passTimestampQueries )
            {
                gl46core::glGenQueries( to_I32( queries.size() ), queries.data() );
            }
            _passTimestampQueriesCreated = true;
        }

        const size_t slot = GFXDevice::FrameCount() % GL_PASS_TIMESTAMP_FRAMES;
        DIVIDE_ASSERT( index == _passTimestampCounts[slot] );
        gl46core::glQueryCounter( _passTimestampQueries[slot][index], gl46core::GL_TIMESTAMP );
        _passTimestampCounts[slot] = index + 1u;
    }

    U32 GL_API::readPassTimestamps( const std::span<F32> outMS, U64& frameNumberOut )
    {
        const U64 frameNumber = GFXDevice::FrameCount();
        const size_t slot = frameNumber % GL_PASS_TIMESTAMP_FRAMES;
        const U32 count = std::min( _passTimestampCounts[slot], to_U32( outMS.size() ) );
        _passTimestampCounts[slot] = 0u;
        frameNumberOut = _passTimestampFrames[slot];
        _passTimestampFrames[slot] = frameNumber;
        if ( count == 0u )
        {
            return 0u;
        }

        const auto& queries = _passTimestampQueries[slot];
        // Timestamps complete in submission order: if the last one is ready, all of them are.
        gl46core::GLint available = 0;
        gl46core::glGetQueryObjectiv( queries[count - 1u], gl46core::GL_QUERY_RESULT_AVAILABLE, &available );
        if ( available == 0 )
        {
            return 0u;
        }

        gl46core::GLuint64 first = 0u;
        gl46core::glGetQueryObjectui64v( queries[0], gl46core::GL_QUERY_RESULT, &first );
        outMS[0] = 0.f;
        for ( U32 i = 1u; i < count; ++i )
        {
            gl46core::GLuint64 value = 0u;
            gl46core::glGetQueryObjectui64v( queries[i], gl46core::GL_QUERY_RESULT, &value );
            outMS[i] = value >= first ? to_F32( to_D64( value - first ) * 1e-6 ) : 0.f;
        }
        return count;
    }

    void GL_API::initDescriptorSets()
    {
        NOP();
    }

    void GL_API::onThreadCreated( [[maybe_unused]] const size_t threadIndex, [[maybe_unused]] const std::thread::id& threadID, const bool isMainRenderThread )
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
        const bool ctxFound = g_ContextPool.getAvailableContext( GLUtil::s_glSecondaryContext );
        DIVIDE_GPU_ASSERT( ctxFound, "GL_API::syncToThread: context not found for current thread!" );

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

        if ( _supportsParallelShaderCompilation )
        {
            gl::glMaxShaderCompilerThreadsARB( 0xFFFFFFFF );
        }
    }

    /// Reset as much of the GL default state as possible within the limitations given
    void GL_API::clearStates(GLStateTracker& stateTracker) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_EXPECTED_CALL( stateTracker.unbindTextures() );

        DIVIDE_EXPECTED_CALL( stateTracker.setActiveBuffer( gl46core::GL_ELEMENT_ARRAY_BUFFER, 0 ) != GLStateTracker::BindResult::FAILED );

        DIVIDE_EXPECTED_CALL( stateTracker.setActiveFB( RenderTarget::Usage::RT_READ_WRITE, 0 ) != GLStateTracker::BindResult::FAILED );

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
        DIVIDE_EXPECTED_CALL( stateTracker.setActiveProgram( 0u ) != GLStateTracker::BindResult::FAILED );

        DIVIDE_EXPECTED_CALL( stateTracker.setActiveShaderPipeline( 0u ) != GLStateTracker::BindResult::FAILED );

        DIVIDE_EXPECTED_CALL( stateTracker.setStateBlock({}) != GLStateTracker::BindResult::FAILED );
    }

    bool GL_API::bindShaderResources( const DescriptorSetEntries& descriptorSetEntries )
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics );

        auto& bindingMetrics = _context.resourceBindingMetrics();
        const bool measureBindings = bindingMetrics.enabled();
        const bool useCompiledBindings = !RenderSmokeTest::BindingCacheDisabledRequested();
        auto& bindingCounters = bindingMetrics.current();
        if ( measureBindings )
        {
            bindingCounters._compiledBindingCacheBytes = _stableSampledBindingCache.StorageBytes();
        }
        const bool uniformBindingsChanged = [&]()
        {
            PROFILE_SCOPE( "Commit binding uniforms", Profiler::Category::Graphics );
            return s_stateTracker._activeShaderProgram != nullptr &&
                   s_stateTracker._activeShaderProgram->commitUniformData( _context.descriptorSet( DescriptorSetUsage::PER_DRAW ).impl() );
        }();
        if ( measureBindings )
        {
            ++bindingCounters._uniformCommits;
            bindingCounters._uniformChanges += uniformBindingsChanged ? 1u : 0u;
        }
        if ( _context.uniformUploadFailed() )
        {
            return false;
        }

        thread_local std::array<SampledImageResolutionCache, to_base(DescriptorSetUsage::COUNT)> resolvedSets;
        for ( DescriptorSetEntry entry : descriptorSetEntries )
        {
            SampledImageResolutionCache& cache = resolvedSets[to_base(entry._usage)];
            const bool hasFallback = [&]()
            {
                PROFILE_SCOPE( "Resolve binding fallbacks", Profiler::Category::Graphics );
                return s_stateTracker._activeShaderProgram != nullptr &&
                       s_stateTracker._activeShaderProgram->resolveSampledImages( entry, cache._resolvedSet );
            }();
            if ( measureBindings )
            {
                ++bindingCounters._fallbackChecks;
                bindingCounters._fallbackSets += hasFallback ? 1u : 0u;
            }
            const GLDescriptorUpdateScope updateScope = SelectGLDescriptorUpdateScope(
                entry._isDirty, cache.update( hasFallback, entry._isDirty ),
                entry._usage == DescriptorSetUsage::PER_DRAW, uniformBindingsChanged );
            if ( hasFallback )
            {
                entry._set = &cache._resolvedSet;
            }
            if ( measureBindings && entry._usage == DescriptorSetUsage::PER_DRAW && s_stateTracker._activeShaderProgram != nullptr )
            {
                PROFILE_SCOPE( "Measure effective bindings", Profiler::Category::Graphics );
                const auto* program = s_stateTracker._activeShaderProgram;
                U32 mask = 0u;
                for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
                {
                    if ( program->perDrawDescriptorSetLayout()[slot]._type != DescriptorSetBindingType::COUNT )
                    {
                        mask |= 1u << slot;
                    }
                }
                if ( mask != 0u )
                {
                    bindingMetrics.observe( program->getGUID(), _context.descriptorSet( entry._usage ).materialRevision(), *entry._set, mask );
                }
            }
            if ( updateScope == GLDescriptorUpdateScope::NONE )
            {
                // We don't need to keep track of descriptor set to layout compatibility in OpenGL
                continue;
            }

            for ( U8 i = 0u; i < entry._set->_bindingCount; ++i )
            {
                const DescriptorSetBinding& srcBinding = entry._set->_bindings[i];
                if ( !ShouldUpdateGLDescriptor( updateScope, srcBinding._data._type ) )
                {
                    continue;
                }
                gl46core::GLubyte glBindingSlot = INVALID_TEXTURE_BINDING;
                const auto getGLBindingSlot = [&]() noexcept
                {
                    if ( glBindingSlot == INVALID_TEXTURE_BINDING )
                    {
                        glBindingSlot = ShaderProgram::GetGLBindingForDescriptorSlot( entry._usage, srcBinding._slot );
                    }
                    return glBindingSlot;
                };

                switch ( srcBinding._data._type )
                {
                    case DescriptorSetBindingType::UNIFORM_BUFFER_STATIC:
                    case DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC:
                    case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC:
                    case DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC:
                    {
                        PROFILE_SCOPE( "Resolve descriptor buffer", Profiler::Category::Graphics );
                        if ( measureBindings )
                        {
                            ++bindingCounters._bufferResolutions;
                            ++bindingCounters._glBufferVisits;
                        }
                        const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;
                        if ( bufferEntry._buffer == nullptr || 
                             bufferEntry._range._length == 0u ) [[unlikely]]
                        {
                            continue;
                        }

                        DIVIDE_ASSERT(IsDescriptorSetBindingTypeDynamic(srcBinding._data._type) || bufferEntry._range._startOffset == 0u);

                        glShaderBuffer* glBuffer = static_cast<glShaderBuffer*>(bufferEntry._buffer);

                        if ( !glBuffer->bindByteRange(
                            getGLBindingSlot(),
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
                    case DescriptorSetBindingType::INDIRECT_BUFFER:
                    {
                        const ShaderBufferEntry& bufferEntry = srcBinding._data._buffer;
                        if ( bufferEntry._buffer == nullptr ) [[unlikely]]
                        {
                            continue;
                        }

                        if ( !static_cast<glShaderBuffer*>(bufferEntry._buffer)->bindAsIndirectBuffer( bufferEntry._queueReadIndex ) )
                        {
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    } break;
                    case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                    {
                        PROFILE_SCOPE( "Resolve descriptor sampled image", Profiler::Category::Graphics );
                        if ( srcBinding._slot == INVALID_TEXTURE_BINDING ) [[unlikely]]
                        {
                            continue;
                        }

                        const DescriptorCombinedImageSampler& imageSampler = srcBinding._data._sampledImage;
                        if ( srcBinding._slot >= MAX_BINDINGS_PER_DESCRIPTOR_SET )
                        {
                            DIVIDE_UNEXPECTED_CALL_MSG( "OpenGL sampled image binding slot is out of range." );
                            continue;
                        }
                        if ( measureBindings )
                        {
                            ++bindingCounters._glTextureVisits;
                        }
                        const U64 stableRevision = useCompiledBindings &&
                                                            imageSampler._image._srcTexture != nullptr &&
                                                           !hasFallback &&
                                                           entry._stableBindingRevisions != nullptr
                                                       ? (*entry._stableBindingRevisions)[srcBinding._slot]
                                                       : 0u;
                        const U8 cacheSlot = to_U8( to_base( entry._usage ) * MAX_BINDINGS_PER_DESCRIPTOR_SET +
                                                    srcBinding._slot );
                        StableSampledBinding compiled{};
                        if ( _stableSampledBindingCache.Find( stableRevision, cacheSlot, compiled ) )
                        {
                            if ( measureBindings )
                            {
                                ++bindingCounters._compiledBindingHits;
                            }
                            DIVIDE_EXPECTED_CALL( queueTextureViewResident( compiled._bindingSlot, compiled._sampler,
                                                                            imageSampler._image, imageSampler._imageHash ) );
                        }
                        else if ( stableRevision != 0u )
                        {
                            if ( measureBindings )
                            {
                                ++bindingCounters._compiledBindingMisses;
                                ++bindingCounters._imageResolutions;
                            }
                            compiled._bindingSlot = getGLBindingSlot();
                            if ( queueTextureViewResident( compiled._bindingSlot, compiled._sampler,
                                                           imageSampler._image, imageSampler._imageHash,
                                                           &imageSampler._sampler, imageSampler._samplerHash ) )
                            {
                                _stableSampledBindingCache.Store( stableRevision, cacheSlot, compiled );
                            }
                            else
                            {
                                DIVIDE_UNEXPECTED_CALL();
                            }
                        }
                        else
                        {
                            if ( measureBindings )
                            {
                                ++bindingCounters._imageResolutions;
                            }
                            DIVIDE_EXPECTED_CALL( makeTextureViewResident( getGLBindingSlot(), imageSampler._image,
                                                                           imageSampler._imageHash, imageSampler._sampler,
                                                                           imageSampler._samplerHash ) );
                        }

                    } break;
                    case DescriptorSetBindingType::IMAGE:
                    {
                        PROFILE_SCOPE( "Resolve descriptor storage image", Profiler::Category::Graphics );
                        if ( measureBindings )
                        {
                            ++bindingCounters._imageResolutions;
                            ++bindingCounters._glTextureVisits;
                        }
                        const DescriptorImageView& imageView = srcBinding._data._imageView;
                        DIVIDE_GPU_ASSERT( TargetType( imageView._image ) != TextureType::COUNT );
                        DIVIDE_GPU_ASSERT( imageView._image._subRange._layerRange._count != ALL_LAYERS );

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
                            case ImageUsage::COUNT: DIVIDE_UNEXPECTED_CALL();
                        }

                        DIVIDE_GPU_ASSERT( imageView._image._subRange._mipLevels._count == 1u );

                        const gl46core::GLenum glInternalFormat = GLUtil::InternalFormatAndDataType( imageView._image._descriptor._baseFormat,
                                                                                                     imageView._image._descriptor._dataType,
                                                                                                     imageView._image._descriptor._packing )._format;

                        const gl46core::GLuint handle = static_cast<const glTexture*>(imageView._image._srcTexture)->textureHandle();
                        if ( handle != GL_NULL_HANDLE &&
                             GL_API::s_stateTracker.bindTextureImage( getGLBindingSlot(),
                                                                      handle,
                                                                      imageView._image._subRange._mipLevels._offset,
                                                                      imageView._image._subRange._layerRange._count != ALL_LAYERS,
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
                    }
                };
            }
        }

        return true;
    }

    bool GL_API::makeTextureViewResident( const gl46core::GLubyte bindingSlot, const ImageView& imageView, const size_t imageViewHash, const SamplerDescriptor sampler, const size_t samplerHash ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        gl46core::GLuint samplerHandle = GL_NULL_HANDLE;
        return queueTextureViewResident( bindingSlot, samplerHandle, imageView, imageViewHash, &sampler, samplerHash );
    }

    bool GL_API::queueTextureViewResident( const gl46core::GLubyte bindingSlot,
                                           gl46core::GLuint& sampler,
                                           const ImageView& imageView,
                                           const size_t imageViewHash,
                                           const SamplerDescriptor* samplerToResolve,
                                           const size_t samplerHash ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( imageView._srcTexture == nullptr )
        {
            //unbind request
            s_TexBindQueue.emplace_back(0u, 0u, bindingSlot);

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

        if ( samplerToResolve != nullptr )
        {
            size_t tempSamplerHash = samplerHash;
            sampler = GetSamplerHandle( *samplerToResolve, tempSamplerHash );
        }
        entry._sampler = sampler;

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
            DIVIDE_EXPECTED_CALL( s_stateTracker.setStateBlock( pipelineDescriptor._stateBlock ) != GLStateTracker::BindResult::FAILED );
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
                DIVIDE_EXPECTED_CALL( s_stateTracker.setActiveProgram( 0u ) != GLStateTracker::BindResult::FAILED );
                DIVIDE_EXPECTED_CALL( s_stateTracker.setActiveShaderPipeline( 0u ) != GLStateTracker::BindResult::FAILED );
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

    void GL_API::AddDebugMessage( const Configuration& config, const char* message, const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        if constexpr ( Config::ENABLE_GPU_VALIDATION )
        {
            if ( config.debug.renderer.enableRenderAPIDebugGrouping)
            {
                gl46core::glPushDebugGroup( gl46core::GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
                gl46core::glPopDebugGroup();
            }
        }
        GFXDevice::AddDebugMessage(message, id);
    }

    void GL_API::PushDebugMessage( const Configuration& config, const char* message, const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            if ( config.debug.renderer.enableRenderAPIDebugGrouping)
            {
                gl46core::glPushDebugGroup( gl46core::GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
            }
        }
        GFXDevice::PushDebugMessage(message, id);
    }

    void GL_API::PopDebugMessage( const Configuration& config )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if constexpr( Config::ENABLE_GPU_VALIDATION )
        {
            if ( config.debug.renderer.enableRenderAPIDebugGrouping)
            {
                gl46core::glPopDebugGroup();
            }
        }
        GFXDevice::PopDebugMessage();
    }

    bool GL_API::DeleteShaderPrograms( const gl46core::GLuint count, gl46core::GLuint* programs )
    {
        if ( count > 0 && programs != nullptr )
        {
            for ( gl46core::GLuint i = 0; i < count; ++i )
            {
                if ( s_stateTracker._activeShaderProgramHandle == programs[i] )
                {
                    DIVIDE_EXPECTED_CALL( s_stateTracker.setActiveProgram( 0u ) != GLStateTracker::BindResult::FAILED );
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
        DIVIDE_GPU_ASSERT( samplerHashInOut != 0u );

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

        gl46core::GLsync sync = gl46core::glFenceSync( gl46core::GL_SYNC_GPU_COMMANDS_COMPLETE, static_cast<gl46core::GLbitfield>(gl46core::UnusedMask::GL_UNUSED_BIT) );
        if (sync != nullptr)
        {
            DIVIDE_GPU_ASSERT( s_fenceSyncCounter[s_LockFrameLifetime - 1u].load(std::memory_order_relaxed) < U32_MAX );
            s_fenceSyncCounter[s_LockFrameLifetime - 1u].fetch_add(1u, std::memory_order_relaxed);
        }
        return sync;
    }

    void GL_API::DestroyFenceSync( gl46core::GLsync& sync )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DIVIDE_GPU_ASSERT( s_fenceSyncCounter[s_LockFrameLifetime - 1u].load(std::memory_order_relaxed) > 0u );
        s_fenceSyncCounter[s_LockFrameLifetime - 1u].fetch_sub(1u, std::memory_order_relaxed);
        gl46core::glDeleteSync( sync );
        sync = nullptr;
    }

    RenderTarget_uptr GL_API::newRenderTarget( const RenderTargetDescriptor& descriptor ) const
    {
        return std::make_unique<glFramebuffer>( _context, descriptor );
    }

    GPUBuffer_uptr GL_API::newGPUBuffer( U32 ringBufferLength, const std::string_view name ) const
    {
        return std::make_unique<glGPUBuffer>( _context, ringBufferLength, name );
    }

    ShaderBuffer_uptr GL_API::newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const
    {
        return std::make_unique<glShaderBuffer>( _context, descriptor );
    }

};
