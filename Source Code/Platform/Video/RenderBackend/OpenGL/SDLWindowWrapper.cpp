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
    struct ContextPool {
        bool init(const size_t size, const DisplayWindow& window) {
            SDL_Window* raw = window.getRawWindow();
            _contexts.resize(size, std::make_pair(nullptr, false));
            for (auto&[context, _] : _contexts) {
                context = SDL_GL_CreateContext(raw);
            }
            return true;
        }

        bool destroy() noexcept {
            for (const auto& [context, _] : _contexts) {
                SDL_GL_DeleteContext(context);
            }
            _contexts.clear();
            return true;
        }

        bool getAvailableContext(SDL_GLContext& ctx) noexcept {
            assert(!_contexts.empty());
            for (auto& [context, used] : _contexts) {
                if (!used) {
                    ctx = context;
                    used = true;
                    return true;
                }
            }

            return false;
        }

        vector<std::pair<SDL_GLContext, bool /*in use*/>> _contexts;
    } g_ContextPool;
};

/// Try and create a valid OpenGL context taking in account the specified resolution and command line arguments
ErrorCode GL_API::initRenderingAPI([[maybe_unused]] GLint argc, [[maybe_unused]] char** argv, Configuration& config) {
    // Fill our (abstract API <-> openGL) enum translation tables with proper values
    GLUtil::fillEnumTables();

    const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();
    g_ContextPool.init(_context.parent().totalThreadCount(), window);

    SDL_GL_MakeCurrent(window.getRawWindow(), (SDL_GLContext)window.userData());
    GLUtil::s_glMainRenderWindow = &window;
    _currentContext = std::make_pair(window.getGUID(), window.userData());

    glbinding::Binding::initialize([](const char *proc) noexcept  { return (glbinding::ProcAddress)SDL_GL_GetProcAddress(proc); }, true);

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

    GFXDevice::setGPURenderer(renderer);
    GFXDevice::setGPUVendor(vendor);

    // GPU info, including vendor, gpu and driver
    Console::printfn(Locale::Get(_ID("GL_VENDOR_STRING")), gpuVendorStr, gpuRendererStr, glGetString(GL_VERSION));

    // Not supported in RenderDoc (as of 2021). Will always return false when using it to debug the app
    const bool extensionSupported = glbinding::aux::ContextInfo::supported({ GLextension::GL_ARB_bindless_texture });
    Console::printfn(Locale::Get(_ID("GL_BINDLESS_TEXTURE_EXTENSION_STATE")), extensionSupported ? "True" : "False");

    s_UseBindlessTextures = config.rendering.useBindlessTextures && extensionSupported;
    s_DebugBindlessTextures = config.rendering.debugBindlessTextures;

    if (s_UseBindlessTextures != config.rendering.useBindlessTextures) {
        config.rendering.useBindlessTextures = s_UseBindlessTextures;
        config.changed(true);
    }

    Console::printfn(Locale::Get(_ID("GL_BINDLESS_TEXTURES_STATE")), s_UseBindlessTextures ? "True" : "False", s_DebugBindlessTextures ? "True": "False");

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
        if (GFXDevice::getGPUVendor() == GPUVendor::NVIDIA) {
            // nVidia flushes a lot of useful info about buffer allocations and shader
            // re-compiles due to state and what now, but those aren't needed until that's
            // what's actually causing the bottlenecks
            const U32 nvidiaBufferErrors[] = { 131185, 131218, 131186 };
            // Disable shader compiler errors (shader class handles that)
            glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_ERROR,
                                  GL_DONT_CARE, 0, nullptr, GL_FALSE);
            // Disable nVidia buffer allocation info (an easy enable is to change the
            // count param to 0)
            glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER,
                                  GL_DONT_CARE, 3, nvidiaBufferErrors, GL_FALSE);
            // Shader will be recompiled nVidia error
            glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_PERFORMANCE,
                                  GL_DONT_CARE, 3, nvidiaBufferErrors, GL_FALSE);
        }
    }

    // If we got here, let's figure out what capabilities we have available
    // Maximum addressable texture image units in the fragment shader
    s_maxTextureUnits = static_cast<GLuint>(std::max(GLUtil::getGLValue(GL_MAX_TEXTURE_IMAGE_UNITS), 16));
    s_residentTextures.resize(to_size(s_maxTextureUnits) * (1 << 4));

    GLUtil::getGLValue(GL_MAX_VERTEX_ATTRIB_BINDINGS, s_maxAttribBindings);
    GLUtil::getGLValue(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, s_maxAtomicBufferBindingIndices);
    Console::printfn(Locale::Get(_ID("GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS")),
                     s_maxAtomicBufferBindingIndices);

    if (to_base(TextureUsage::COUNT) >= s_maxTextureUnits) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_TEXTURE_UNITS")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    if (to_base(AttribLocation::COUNT) >= s_maxAttribBindings) {
        Console::errorfn(Locale::Get(_ID("ERROR_INSUFFICIENT_ATTRIB_BINDS")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    GLint majGLVersion = GLUtil::getGLValue(GL_MAJOR_VERSION);
    GLint minGLVersion = GLUtil::getGLValue(GL_MINOR_VERSION);
    Console::printfn(Locale::Get(_ID("GL_MAX_VERSION")), majGLVersion, minGLVersion);

    if (majGLVersion <= 4 && minGLVersion < 6) {
        Console::errorfn(Locale::Get(_ID("ERROR_OPENGL_VERSION_TO_OLD")));
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    // Maximum number of colour attachments per framebuffer
    GLUtil::getGLValue(GL_MAX_COLOR_ATTACHMENTS, s_maxFBOAttachments);

    s_stateTracker.init();

    if (s_stateTracker._opengl46Supported) {
        glMaxShaderCompilerThreadsARB(0xFFFFFFFF);
        Console::printfn(Locale::Get(_ID("GL_SHADER_THREADS")), GLUtil::getGLValue(GL_MAX_SHADER_COMPILER_THREADS_ARB));
    }

    glEnable(GL_MULTISAMPLE);
    // Line smoothing should almost always be used
    glEnable(GL_LINE_SMOOTH);

    // GL_FALSE causes a conflict here. Thanks glbinding ...
    glClampColor(GL_CLAMP_READ_COLOR, GL_NONE);

    // Cap max anisotropic level to what the hardware supports
    CLAMP(config.rendering.maxAnisotropicFilteringLevel,
          to_U8(0),
          to_U8(s_stateTracker._opengl46Supported ? GLUtil::getGLValue(GL_MAX_TEXTURE_MAX_ANISOTROPY)
                                                  : GLUtil::getGLValue(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)));
    s_maxAnisotropicFilteringLevel = config.rendering.maxAnisotropicFilteringLevel;

    // Number of sample buffers associated with the framebuffer & MSAA sample count
    const U8 maxGLSamples = to_U8(std::min(254, GLUtil::getGLValue(GL_MAX_SAMPLES)));
    // If we do not support MSAA on a hardware level for whatever reason, override user set MSAA levels
    config.rendering.MSAASamples = std::min(config.rendering.MSAASamples, maxGLSamples);

    config.rendering.shadowMapping.csm.MSAASamples = std::min(config.rendering.shadowMapping.csm.MSAASamples, maxGLSamples);
    config.rendering.shadowMapping.spot.MSAASamples = std::min(config.rendering.shadowMapping.spot.MSAASamples, maxGLSamples);
    _context.gpuState().maxMSAASampleCount(maxGLSamples);

    // Print all of the OpenGL functionality info to the console and log
    // How many uniforms can we send to fragment shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_UNIFORM")),
                     GLUtil::getGLValue(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS));
    // How many uniforms can we send to vertex shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_VERT_UNIFORM")),
                     GLUtil::getGLValue(GL_MAX_VERTEX_UNIFORM_COMPONENTS));
    // How many uniforms can we send to vertex + fragment shaders at the same time
    Console::printfn(Locale::Get(_ID("GL_MAX_FRAG_AND_VERT_UNIFORM")),
                     GLUtil::getGLValue(GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS));
    // How many attributes can we send to a vertex shader
    Console::printfn(Locale::Get(_ID("GL_MAX_VERT_ATTRIB")),
                     GLUtil::getGLValue(GL_MAX_VERTEX_ATTRIBS)); 
    // How many workgroups can we have per compute dispatch
    for (U8 i = 0u; i < 3; ++i) {
        GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_COUNT, s_maxWorgroupCount[i], i);
        GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_SIZE, s_maxWorgroupSize[i], i);
    }

    s_maxWorgroupInvocations = GLUtil::getGLValue(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    s_maxComputeSharedMemory = GLUtil::getGLValue(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE);

    Console::printfn(Locale::Get(_ID("GL_MAX_COMPUTE_WORK_GROUP_INFO")),
                     s_maxWorgroupCount[0], s_maxWorgroupCount[1], s_maxWorgroupCount[2], 
                     s_maxWorgroupSize[0], s_maxWorgroupSize[1], s_maxWorgroupSize[2],
                     s_maxWorgroupInvocations);
    Console::printfn(Locale::Get(_ID("GL_MAX_COMPUTE_SHARED_MEMORY_SIZE")),
                     s_maxComputeSharedMemory / 1024);
    
    // Maximum number of texture units we can address in shaders
    Console::printfn(Locale::Get(_ID("GL_MAX_TEX_UNITS")),
                     GLUtil::getGLValue(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS),
                     s_maxTextureUnits);
    // Maximum number of varying components supported as outputs in the vertex shader
    Console::printfn(Locale::Get(_ID("GL_MAX_VERTEX_OUTPUT_COMPONENTS")),
                     GLUtil::getGLValue(GL_MAX_VERTEX_OUTPUT_COMPONENTS));
    // Query shading language version support
    Console::printfn(Locale::Get(_ID("GL_GLSL_SUPPORT")),
                     glGetString(GL_SHADING_LANGUAGE_VERSION));
    // In order: Maximum number of uniform buffer binding points,
    //           maximum size in basic machine units of a uniform block and
    //           minimum required alignment for uniform buffer sizes and offset
    GLUtil::getGLValue(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, s_UBOffsetAlignment);
    GLUtil::getGLValue(GL_MAX_UNIFORM_BLOCK_SIZE, s_UBMaxSize);
    const bool UBOSizeOver1Mb = s_UBMaxSize / 1024 > 1024;
    Console::printfn(Locale::Get(_ID("GL_UBO_INFO")),
                     GLUtil::getGLValue(GL_MAX_UNIFORM_BUFFER_BINDINGS),
                     (s_UBMaxSize / 1024) / (UBOSizeOver1Mb ? 1024 : 1),
                     UBOSizeOver1Mb ? "Mb" : "Kb",
                     s_UBOffsetAlignment);

    // In order: Maximum number of shader storage buffer binding points,
    //           maximum size in basic machine units of a shader storage block,
    //           maximum total number of active shader storage blocks that may
    //           be accessed by all active shaders and
    //           minimum required alignment for shader storage buffer sizes and
    //           offset.
    GLUtil::getGLValue(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, s_SSBOffsetAlignment);
    GLUtil::getGLValue(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, s_SSBMaxSize);
    Console::printfn(
        Locale::Get(_ID("GL_SSBO_INFO")),
        GLUtil::getGLValue(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS),
        GLUtil::getGLValue(GL_MAX_SHADER_STORAGE_BLOCK_SIZE) / 1024 / 1024,
        GLUtil::getGLValue(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS),
        s_SSBOffsetAlignment);

    // Maximum number of subroutines and maximum number of subroutine uniform
    // locations usable in a shader
    Console::printfn(Locale::Get(_ID("GL_SUBROUTINE_INFO")),
                     GLUtil::getGLValue(GL_MAX_SUBROUTINES),
                     GLUtil::getGLValue(GL_MAX_SUBROUTINE_UNIFORM_LOCATIONS));

    GLint range[2];
    GLUtil::getGLValue(GL_SMOOTH_LINE_WIDTH_RANGE, range);
    Console::printfn(Locale::Get(_ID("GL_LINE_WIDTH_INFO")), range[0], range[1]);

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

    // Initialize shader buffers
    glUniformBuffer::OnGLInit();
    // Init static program data
    glShaderProgram::OnStartup();
    // Init any buffer locking mechanism we might need
    glBufferLockManager::OnStartup();
    // We need a dummy VAO object for point rendering
    glCreateVertexArrays(1, &s_dummyVAO);
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_VERTEX_ARRAY, s_dummyVAO, -1, "Dummy VAO");
    }
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
    if (InitGLSW(config)) {
        // That's it. Everything should be ready for draw calls
        Console::printfn(Locale::Get(_ID("START_OGL_API_OK")));
        return ErrorCode::NO_ERR;
    }


    return ErrorCode::GLSL_INIT_ERROR;
}

/// Clear everything that was setup in initRenderingAPI()
void GL_API::closeRenderingAPI() {
    glShaderProgram::OnShutdown();
    glShaderProgram::DestroyStaticData();
    DeInitGLSW();

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
    if (s_dummyVAO != GLUtil::k_invalidObjectID) {
        DeleteVAOs(1, &s_dummyVAO);
    }
    s_textureViewCache.destroy();
    if (s_hardwareQueryPool != nullptr) {
        s_hardwareQueryPool->destroy();
        MemoryManager::DELETE(s_hardwareQueryPool);
    }
    for (auto& allocator : s_memoryAllocators) {
        allocator.deallocate();
    }
    g_ContextPool.destroy();
}

vec2<U16> GL_API::getDrawableSize(const DisplayWindow& window) const noexcept {
    int w = 1, h = 1;
    SDL_GL_GetDrawableSize(window.getRawWindow(), &w, &h);
    return vec2<U16>(w, h);
}

void GL_API::QueueFlush() noexcept {
    s_glFlushQueued.store(true);
}

void GL_API::QueueComputeMipMaps(const GLuint textureHandle) {
    ScopedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
    if (s_mipmapQueue.find(textureHandle) == std::cend(s_mipmapQueue)) {
        s_mipmapQueue.insert(textureHandle);
    }
}

void GL_API::DequeueComputeMipMaps(const GLuint textureHandle) {
    ScopedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
    const auto it = s_mipmapQueue.find(textureHandle);
    if (it != std::cend(s_mipmapQueue)) {
        s_mipmapQueue.erase(it);
    }
}

void GL_API::ComputeMipMaps(const GLuint textureHandle) {
    ScopedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
    const auto it = s_mipmapQueue.find(textureHandle);
    if (it != std::cend(s_mipmapQueue)) {
        glGenerateTextureMipmap(*it);
        s_mipmapQueue.erase(it);
    } else {
        glGenerateTextureMipmap(textureHandle);
    }
}

bool GL_API::ComputeMipMapsQueued(const GLuint textureHandle) {
    ScopedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
    return s_mipmapQueue.find(textureHandle) != std::cend(s_mipmapQueue);
}

void GL_API::ProcessMipMapsQueue() noexcept {
    {
        SharedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
        if (GL_API::s_mipmapQueue.empty()) {
            return;
        }
    }

    ScopedLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
    for (const GLuint handle : s_mipmapQueue) {
        glGenerateTextureMipmap(handle);
    }
    s_mipmapQueue.clear();
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
        // Debug callback in a separate thread requires a flag to distinguish it
        // from the main thread's callbacks
        glDebugMessageCallback((GLDEBUGPROC)GLUtil::DebugCallback, GLUtil::s_glSecondaryContext);
    }

    if (s_stateTracker._opengl46Supported) {
        glMaxShaderCompilerThreadsARB(0xFFFFFFFF);
    }

    InitGLSW(_context.context().config());
}
};
