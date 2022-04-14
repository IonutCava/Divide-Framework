#include "stdafx.h"

#include "Headers/glHardwareQuery.h"
#include "Headers/GLWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "GUI/Headers/GUI.h"

#include "Utility/Headers/Localization.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/CEGUIOpenGLRenderer/include/GL3Renderer.h"
#include <glbinding-aux/ContextInfo.h>

#include "Platform/Video/GLIM/glim.h"

#include <glbinding/Binding.h>

#ifndef GLFONTSTASH_IMPLEMENTATION
#define GLFONTSTASH_IMPLEMENTATION
#define FONTSTASH_IMPLEMENTATION
#include "Text/Headers/fontstash.h"
#include "Text/Headers/glfontstash.h"
#endif

namespace Divide {
namespace {
    struct SDLContextEntry {
        SDL_GLContext _context = nullptr;
        bool _inUse = false;
    };

    struct ContextPool {
        bool init(const size_t size, const DisplayWindow& window) {
            SDL_Window* raw = window.getRawWindow();
            _contexts.resize(size);
            for (SDLContextEntry& contextEntry : _contexts) {
                contextEntry._context = SDL_GL_CreateContext(raw);
            }
            return true;
        }

        bool destroy() noexcept {
            for (const SDLContextEntry& contextEntry : _contexts) {
                SDL_GL_DeleteContext(contextEntry._context);
            }
            _contexts.clear();
            return true;
        }

        bool getAvailableContext(SDL_GLContext& ctx) noexcept {
            assert(!_contexts.empty());
            for (SDLContextEntry& contextEntry : _contexts) {
                if (!contextEntry._inUse) {
                    ctx = contextEntry._context;
                    contextEntry._inUse = true;
                    return true;
                }
            }

            return false;
        }

        vector<SDLContextEntry> _contexts;
    } g_ContextPool;
};

/// Try and create a valid OpenGL context taking in account the specified resolution and command line arguments
ErrorCode GL_API::initRenderingAPI([[maybe_unused]] GLint argc, [[maybe_unused]] char** argv, Configuration& config) {
    // Fill our (abstract API <-> openGL) enum translation tables with proper values
    GLUtil::fillEnumTables();

    const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();
    g_ContextPool.init(_context.parent().totalThreadCount(), window);

    SDL_GL_MakeCurrent(window.getRawWindow(), window.userData()->_glContext);
    GLUtil::s_glMainRenderWindow = &window;
    _currentContext._windowGUID = window.getGUID();
    _currentContext._context = window.userData()->_glContext;

    glbinding::Binding::initialize([](const char *proc) noexcept  {
                                        return (glbinding::ProcAddress)SDL_GL_GetProcAddress(proc);
                                  }, true);

    if (SDL_GL_GetCurrentContext() == nullptr) {
        return ErrorCode::GLBINGING_INIT_ERROR;
    }

    glbinding::Binding::useCurrentContext();

    // Query GPU vendor to enable/disable vendor specific features
    GPUVendor vendor = GPUVendor::COUNT;
    const char* gpuVendorStr = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    if (gpuVendorStr != nullptr) {
        if (strstr(gpuVendorStr, "Intel") != nullptr) {
            vendor = GPUVendor::INTEL;
        } else if (strstr(gpuVendorStr, "NVIDIA") != nullptr) {
            vendor = GPUVendor::NVIDIA;
        } else if (strstr(gpuVendorStr, "ATI") != nullptr || strstr(gpuVendorStr, "AMD") != nullptr) {
            vendor = GPUVendor::AMD;
        } else if (strstr(gpuVendorStr, "Microsoft") != nullptr) {
            vendor = GPUVendor::MICROSOFT;
        } else {
            vendor = GPUVendor::OTHER;
        }
    } else {
        gpuVendorStr = "Unknown GPU Vendor";
        vendor = GPUVendor::OTHER;
    }
    GPURenderer renderer = GPURenderer::COUNT;
    const char* gpuRendererStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (gpuRendererStr != nullptr) {
        if (strstr(gpuRendererStr, "Tegra") || strstr(gpuRendererStr, "GeForce") || strstr(gpuRendererStr, "NV")) {
            renderer = GPURenderer::GEFORCE;
        } else if (strstr(gpuRendererStr, "PowerVR") || strstr(gpuRendererStr, "Apple")) {
            renderer = GPURenderer::POWERVR;
            vendor = GPUVendor::IMAGINATION_TECH;
        } else if (strstr(gpuRendererStr, "Mali")) {
            renderer = GPURenderer::MALI;
            vendor = GPUVendor::ARM;
        } else if (strstr(gpuRendererStr, "Adreno")) {
            renderer = GPURenderer::ADRENO;
            vendor = GPUVendor::QUALCOMM;
        } else if (strstr(gpuRendererStr, "AMD") || strstr(gpuRendererStr, "ATI")) {
            renderer = GPURenderer::RADEON;
        } else if (strstr(gpuRendererStr, "Intel")) {
            renderer = GPURenderer::INTEL;
        } else if (strstr(gpuRendererStr, "Vivante")) {
            renderer = GPURenderer::VIVANTE;
            vendor = GPUVendor::VIVANTE;
        } else if (strstr(gpuRendererStr, "VideoCore")) {
            renderer = GPURenderer::VIDEOCORE;
            vendor = GPUVendor::ALPHAMOSAIC;
        } else if (strstr(gpuRendererStr, "WebKit") || strstr(gpuRendererStr, "Mozilla") || strstr(gpuRendererStr, "ANGLE")) {
            renderer = GPURenderer::WEBGL;
            vendor = GPUVendor::WEBGL;
        } else if (strstr(gpuRendererStr, "GDI Generic")) {
            renderer = GPURenderer::GDI;
        } else {
            renderer = GPURenderer::UNKNOWN;
        }
    } else {
        gpuRendererStr = "Unknown GPU Renderer";
        renderer = GPURenderer::UNKNOWN;
    }
    // GPU info, including vendor, gpu and driver
    Console::printfn(Locale::Get(_ID("GL_VENDOR_STRING")), gpuVendorStr, gpuRendererStr, glGetString(GL_VERSION));

    DeviceInformation deviceInformation{};
    deviceInformation._vendor = vendor;
    deviceInformation._renderer = renderer;

    // Not supported in RenderDoc (as of 2021). Will always return false when using it to debug the app
    deviceInformation._bindlessTexturesSupported = glbinding::aux::ContextInfo::supported({ GLextension::GL_ARB_bindless_texture });
    Console::printfn(Locale::Get(_ID("GL_BINDLESS_TEXTURE_EXTENSION_STATE")), deviceInformation._bindlessTexturesSupported ? "True" : "False");

    if (s_hardwareQueryPool == nullptr) {
        s_hardwareQueryPool = MemoryManager_NEW glHardwareQueryPool(_context);
    }

    // OpenGL has a nifty error callback system, available in every build configuration if required
    if (Config::ENABLE_GPU_VALIDATION && config.debug.enableRenderAPIDebugging) {
        // GL_DEBUG_OUTPUT_SYNCHRONOUS is essential for debugging gl commands in the IDE
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        // hard-wire our debug callback function with OpenGL's implementation
        glDebugMessageCallback((GLDEBUGPROC)GLUtil::DebugCallback, nullptr);
    }

    // If we got here, let's figure out what capabilities we have available
    // Maximum addressable texture image units in the fragment shader
    deviceInformation._maxTextureUnits = to_U8(CLAMPED(GLUtil::getGLValue(GL_MAX_TEXTURE_IMAGE_UNITS), 16u, 255u));
    s_residentTextures.resize(to_size(deviceInformation._maxTextureUnits) * (1 << 4));

    GLUtil::getGLValue(GL_MAX_VERTEX_ATTRIB_BINDINGS, deviceInformation._maxVertAttributeBindings);
    GLUtil::getGLValue(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, deviceInformation._maxAtomicBufferBindingIndices);
    Console::printfn(Locale::Get(_ID("GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS")), deviceInformation._maxAtomicBufferBindingIndices);

    if (to_base(TextureUsage::COUNT) >= deviceInformation._maxTextureUnits) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_TEXTURE_UNITS")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    if (to_base(AttribLocation::COUNT) >= deviceInformation._maxVertAttributeBindings) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_ATTRIB_BINDS")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    deviceInformation._versionInfo._major = to_U8(GLUtil::getGLValue(GL_MAJOR_VERSION));
    deviceInformation._versionInfo._minor = to_U8(GLUtil::getGLValue(GL_MINOR_VERSION));
    Console::printfn(Locale::Get(_ID("GL_MAX_VERSION")), deviceInformation._versionInfo._major, deviceInformation._versionInfo._minor);

    if (deviceInformation._versionInfo._major < 4 || (deviceInformation._versionInfo._major == 4 && deviceInformation._versionInfo._minor < 6)) {
        Console::errorfn(Locale::Get(_ID("ERROR_OPENGL_VERSION_TO_OLD")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    // Maximum number of colour attachments per framebuffer
    GLUtil::getGLValue(GL_MAX_COLOR_ATTACHMENTS, deviceInformation._maxRTColourAttachments);

    s_stateTracker.init();

    glMaxShaderCompilerThreadsARB(0xFFFFFFFF);
    deviceInformation._shaderCompilerThreads = GLUtil::getGLValue(GL_MAX_SHADER_COMPILER_THREADS_ARB);
    Console::printfn(Locale::Get(_ID("GL_SHADER_THREADS")), deviceInformation._shaderCompilerThreads);

    glEnable(GL_MULTISAMPLE);
    // Line smoothing should almost always be used
    glEnable(GL_LINE_SMOOTH);

    // GL_FALSE causes a conflict here. Thanks glbinding ...
    glClampColor(GL_CLAMP_READ_COLOR, GL_NONE);

    // Cap max anisotropic level to what the hardware supports
    CLAMP(config.rendering.maxAnisotropicFilteringLevel,
          to_U8(0),
          to_U8(GLUtil::getGLValue(GL_MAX_TEXTURE_MAX_ANISOTROPY)));

    deviceInformation._maxAnisotropy = config.rendering.maxAnisotropicFilteringLevel;

    // Number of sample buffers associated with the framebuffer & MSAA sample count
    const U8 maxGLSamples = to_U8(std::min(254, GLUtil::getGLValue(GL_MAX_SAMPLES)));
    // If we do not support MSAA on a hardware level for whatever reason, override user set MSAA levels
    config.rendering.MSAASamples = std::min(config.rendering.MSAASamples, maxGLSamples);

    config.rendering.shadowMapping.csm.MSAASamples = std::min(config.rendering.shadowMapping.csm.MSAASamples, maxGLSamples);
    config.rendering.shadowMapping.spot.MSAASamples = std::min(config.rendering.shadowMapping.spot.MSAASamples, maxGLSamples);
    _context.gpuState().maxMSAASampleCount(maxGLSamples);

    // Print all of the OpenGL functionality info to the console and log
    // How many uniforms can we send to fragment shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_UNIFORM")), GLUtil::getGLValue(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS));
    // How many uniforms can we send to vertex shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_VERT_UNIFORM")), GLUtil::getGLValue(GL_MAX_VERTEX_UNIFORM_COMPONENTS));
    // How many uniforms can we send to vertex + fragment shaders at the same time
    Console::printfn(Locale::Get(_ID("GL_MAX_FRAG_AND_VERT_UNIFORM")), GLUtil::getGLValue(GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS));
    // How many attributes can we send to a vertex shader
    deviceInformation._maxVertAttributes = GLUtil::getGLValue(GL_MAX_VERTEX_ATTRIBS);
    Console::printfn(Locale::Get(_ID("GL_MAX_VERT_ATTRIB")), deviceInformation._maxVertAttributes);
        
    // How many workgroups can we have per compute dispatch
    for (U8 i = 0u; i < 3; ++i) {
        GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_COUNT, deviceInformation._maxWorgroupCount[i], i);
        GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_SIZE,  deviceInformation._maxWorgroupSize[i], i);
    }

    deviceInformation._maxWorgroupInvocations = GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    deviceInformation._maxComputeSharedMemoryBytes = GLUtil::getGLValue(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE);

    Console::printfn(Locale::Get(_ID("GL_MAX_COMPUTE_WORK_GROUP_INFO")),
                     deviceInformation._maxWorgroupCount[0], deviceInformation._maxWorgroupCount[1], deviceInformation._maxWorgroupCount[2],
                     deviceInformation._maxWorgroupSize[0],  deviceInformation._maxWorgroupSize[1],  deviceInformation._maxWorgroupSize[2],
                     deviceInformation._maxWorgroupInvocations);
    Console::printfn(Locale::Get(_ID("GL_MAX_COMPUTE_SHARED_MEMORY_SIZE")), deviceInformation._maxComputeSharedMemoryBytes / 1024);
    
    // Maximum number of texture units we can address in shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_TEX_UNITS")),
                     GLUtil::getGLValue(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS),
                     deviceInformation._maxTextureUnits);
    // Maximum number of varying components supported as outputs in the vertex shader
    deviceInformation._maxVertOutputComponents = GLUtil::getGLValue(GL_MAX_VERTEX_OUTPUT_COMPONENTS);
    Console::printfn(Locale::Get(_ID("GL_MAX_VERTEX_OUTPUT_COMPONENTS")), deviceInformation._maxVertOutputComponents);

    // Query shading language version support
    Console::printfn(Locale::Get(_ID("GL_GLSL_SUPPORT")),
                     glGetString(GL_SHADING_LANGUAGE_VERSION));
    // In order: Maximum number of uniform buffer binding points,
    //           maximum size in basic machine units of a uniform block and
    //           minimum required alignment for uniform buffer sizes and offset
    GLUtil::getGLValue(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, deviceInformation._UBOffsetAlignmentBytes);
    GLUtil::getGLValue(GL_MAX_UNIFORM_BLOCK_SIZE, deviceInformation._UBOMaxSizeBytes);
    const bool UBOSizeOver1Mb = deviceInformation._UBOMaxSizeBytes / 1024 > 1024;
    Console::printfn(Locale::Get(_ID("GL_UBO_INFO")),
                     GLUtil::getGLValue(GL_MAX_UNIFORM_BUFFER_BINDINGS),
                     (deviceInformation._UBOMaxSizeBytes / 1024) / (UBOSizeOver1Mb ? 1024 : 1),
                     UBOSizeOver1Mb ? "Mb" : "Kb",
                     deviceInformation._UBOffsetAlignmentBytes);

    // In order: Maximum number of shader storage buffer binding points,
    //           maximum size in basic machine units of a shader storage block,
    //           maximum total number of active shader storage blocks that may
    //           be accessed by all active shaders and
    //           minimum required alignment for shader storage buffer sizes and
    //           offset.
    GLUtil::getGLValue(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, deviceInformation._SSBOffsetAlignmentBytes);
    GLUtil::getGLValue(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, deviceInformation._SSBOMaxSizeBytes);
    deviceInformation._maxSSBOBufferBindings = GLUtil::getGLValue(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
    Console::printfn(
        Locale::Get(_ID("GL_SSBO_INFO")),
        deviceInformation._maxSSBOBufferBindings,
        deviceInformation._SSBOMaxSizeBytes / 1024 / 1024,
        GLUtil::getGLValue(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS),
        deviceInformation._SSBOffsetAlignmentBytes);

    // Maximum number of subroutines and maximum number of subroutine uniform
    // locations usable in a shader
    Console::printfn(Locale::Get(_ID("GL_SUBROUTINE_INFO")),
                     GLUtil::getGLValue(GL_MAX_SUBROUTINES),
                     GLUtil::getGLValue(GL_MAX_SUBROUTINE_UNIFORM_LOCATIONS));

    GLint range[2];
    GLUtil::getGLValue(GL_SMOOTH_LINE_WIDTH_RANGE, range);
    Console::printfn(Locale::Get(_ID("GL_LINE_WIDTH_INFO")), range[0], range[1]);

    const I32 clipDistanceCount = std::max(GLUtil::getGLValue(GL_MAX_CLIP_DISTANCES), 0);
    const I32 cullDistanceCount = std::max(GLUtil::getGLValue(GL_MAX_CULL_DISTANCES), 0);

    deviceInformation._maxClipAndCullDistances = to_U8(GLUtil::getGLValue(GL_MAX_COMBINED_CLIP_AND_CULL_DISTANCES));
    deviceInformation._maxClipDistances = to_U8(clipDistanceCount);
    deviceInformation._maxCullDistances = to_U8(cullDistanceCount);
    DIVIDE_ASSERT(Config::MAX_CLIP_DISTANCES <= deviceInformation._maxClipDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");
    DIVIDE_ASSERT(Config::MAX_CULL_DISTANCES <= deviceInformation._maxCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");
    DIVIDE_ASSERT(Config::MAX_CULL_DISTANCES + Config::MAX_CLIP_DISTANCES <= deviceInformation._maxClipAndCullDistances, "SDLWindowWrapper error: incorrect combination of clip and cull distance counts");

    DIVIDE_ASSERT(Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS < deviceInformation._maxWorgroupSize[0] &&
                  Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS < deviceInformation._maxWorgroupSize[1] &&
                  Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < deviceInformation._maxWorgroupSize[2]);

    DIVIDE_ASSERT(to_U32(Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS) *
                         Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS *
                         Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS < deviceInformation._maxWorgroupInvocations);

    GFXDevice::OverrideDeviceInformation(deviceInformation);
    // Seamless cubemaps are a nice feature to have enabled (core since 3.2)
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    //glEnable(GL_FRAMEBUFFER_SRGB);
    // Culling is enabled by default, but RenderStateBlocks can toggle it on a per-draw call basis
    glEnable(GL_CULL_FACE);

    // Enable all clip planes, I guess
    for (U8 i = 0u; i < Config::MAX_CLIP_DISTANCES; ++i) {
        glEnable(static_cast<GLenum>(static_cast<U32>(GL_CLIP_DISTANCE0) + i));
    }

    for (U8 i = 0u; i < to_base(GLUtil::GLMemory::GLMemoryType::COUNT); ++i) {
        s_memoryAllocators[i].init(s_memoryAllocatorSizes[i]);
    }

    s_textureViewCache.init(256);

    // FontStash library initialization
    // 512x512 atlas with bottom-left origin
    _fonsContext = glfonsCreate(512, 512, FONS_ZERO_BOTTOMLEFT);
    if (_fonsContext == nullptr) {
        Console::errorfn(Locale::Get(_ID("ERROR_FONT_INIT")));
        return ErrorCode::FONT_INIT_ERROR;
    }

    // Prepare immediate mode emulation rendering
    NS_GLIM::glim.SetVertexAttribLocation(to_base(AttribLocation::POSITION));

    // Initialize our query pool
    s_hardwareQueryPool->init(
        {
            { GL_TIME_ELAPSED, 9 },
            { GL_TRANSFORM_FEEDBACK_OVERFLOW, 6 },
            { GL_VERTICES_SUBMITTED, 6 },
            { GL_PRIMITIVES_SUBMITTED, 6 },
            { GL_VERTEX_SHADER_INVOCATIONS, 6 },
            { GL_SAMPLES_PASSED, 6 },
            { GL_ANY_SAMPLES_PASSED, 6 },
            { GL_PRIMITIVES_GENERATED, 6 },
            { GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, 6 },
            { GL_ANY_SAMPLES_PASSED_CONSERVATIVE, 6 },
            { GL_TESS_CONTROL_SHADER_PATCHES, 6},
            { GL_TESS_EVALUATION_SHADER_INVOCATIONS, 6}
        }
    );

    // Init any buffer locking mechanism we might need
    glBufferLockManager::OnStartup();
    // Once OpenGL is ready for rendering, init CEGUI
    _GUIGLrenderer = &CEGUI::OpenGL3Renderer::create();
    _GUIGLrenderer->enableExtraStateSettings(false);
    _context.context().gui().setRenderer(*_GUIGLrenderer);

    glClearColor(DefaultColours::BLACK.r,
                 DefaultColours::BLACK.g,
                 DefaultColours::BLACK.b,
                 DefaultColours::BLACK.a);

    _performanceQueries[to_base(QueryType::GPU_TIME)] = eastl::make_unique<glHardwareQueryRing>(_context, GL_TIME_ELAPSED, 6);
    _performanceQueries[to_base(QueryType::VERTICES_SUBMITTED)] = eastl::make_unique<glHardwareQueryRing>(_context, GL_VERTICES_SUBMITTED, 6);
    _performanceQueries[to_base(QueryType::PRIMITIVES_GENERATED)] = eastl::make_unique<glHardwareQueryRing>(_context, GL_PRIMITIVES_GENERATED, 6);
    _performanceQueries[to_base(QueryType::TESSELLATION_PATCHES)] = eastl::make_unique<glHardwareQueryRing>(_context, GL_TESS_CONTROL_SHADER_PATCHES, 6);
    _performanceQueries[to_base(QueryType::TESSELLATION_CTRL_INVOCATIONS)] = eastl::make_unique<glHardwareQueryRing>(_context, GL_TESS_EVALUATION_SHADER_INVOCATIONS, 6);

    // Prepare shader headers and various shader related states
    glShaderProgram::InitStaticData();
    // That's it. Everything should be ready for draw calls
    Console::printfn(Locale::Get(_ID("START_OGL_API_OK")));
    return ErrorCode::NO_ERR;
}

/// Clear everything that was setup in initRenderingAPI()
void GL_API::closeRenderingAPI() {
    glShaderProgram::DestroyStaticData();

    if (_GUIGLrenderer) {
        CEGUI::OpenGL3Renderer::destroy(*_GUIGLrenderer);
        _GUIGLrenderer = nullptr;
    }
    // Destroy sampler objects
    {
        for (auto &sampler : s_samplerMap) {
            glSamplerObject::destruct(sampler.second);
        }
        s_samplerMap.clear();
    }
    // Destroy the text rendering system
    glfonsDelete(_fonsContext);
    _fonsContext = nullptr;

    _fonts.clear();
    s_textureViewCache.destroy();
    if (s_hardwareQueryPool != nullptr) {
        s_hardwareQueryPool->destroy();
        MemoryManager::DELETE(s_hardwareQueryPool);
    }
    for (GLUtil::GLMemory::DeviceAllocator& allocator : s_memoryAllocators) {
        allocator.deallocate();
    }
    g_ContextPool.destroy();
    s_stateTracker.clear();
}

vec2<U16> GL_API::getDrawableSize(const DisplayWindow& window) const noexcept {
    int w = 1, h = 1;
    SDL_GL_GetDrawableSize(window.getRawWindow(), &w, &h);
    return vec2<U16>(w, h);
}

void GL_API::QueueFlush() noexcept {
    s_glFlushQueued.store(true);
}

void GL_API::onThreadCreated([[maybe_unused]] const std::thread::id& threadID) {
    // Double check so that we don't run into a race condition!
    ScopedLock<Mutex> lock(GLUtil::s_glSecondaryContextMutex);
    assert(SDL_GL_GetCurrentContext() == NULL);

    // This also makes the context current
    assert(GLUtil::s_glSecondaryContext == nullptr && "GL_API::syncToThread: double init context for current thread!");
    [[maybe_unused]] const bool ctxFound = g_ContextPool.getAvailableContext(GLUtil::s_glSecondaryContext);
    assert(ctxFound && "GL_API::syncToThread: context not found for current thread!");

    SDL_GL_MakeCurrent(GLUtil::s_glMainRenderWindow->getRawWindow(), GLUtil::s_glSecondaryContext);
    glbinding::Binding::initialize([](const char* proc) noexcept {
        return (glbinding::ProcAddress)SDL_GL_GetProcAddress(proc);
    });
    
    // Enable OpenGL debug callbacks for this context as well
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        // Debug callback in a separate thread requires a flag to distinguish it from the main thread's callbacks
        glDebugMessageCallback((GLDEBUGPROC)GLUtil::DebugCallback, GLUtil::s_glSecondaryContext);
    }

    glMaxShaderCompilerThreadsARB(0xFFFFFFFF);
}
};
