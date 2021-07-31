#include "stdafx.h"

#include "Headers/GLWrapper.h"

#include "CEGUIOpenGLRenderer/include/Texture.h"
#include "Headers/glHardwareQuery.h"

#include "Platform/File/Headers/FileManagement.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/RenderTarget/Headers/glFramebuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/ShaderBuffer/Headers/glUniformBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/VertexBuffer/Headers/glGenericVertexData.h"
#include "Platform/Video/RenderBackend/OpenGL/glsw/Headers/glsw.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Geometry/Material/Headers/Material.h"
#include "GUI/Headers/GUIText.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Utility/Headers/Localization.h"

#include <SDL_video.h>
#include <CEGUI/CEGUI.h>

#include <glbinding-aux/Meta.h>


#include "Scenes/Headers/SceneShaderData.h"
#include "Text/Headers/fontstash.h"

namespace Divide {

namespace {
    // Weird stuff happens if this is enabled (i.e. certain draw calls hang forever)
    constexpr bool g_runAllQueriesInSameFrame = false;
    // Keep resident textures in memory for a max of 30 frames
    constexpr U8 g_maxTextureResidencyFrameCount = Config::TARGET_FRAME_RATE / 2;
    // Will try and reduce memory usage by static data that we may not be need anymore
    const U32 g_clearMemoryFrameInterval = Config::TARGET_FRAME_RATE;
}

GLStateTracker GL_API::s_stateTracker;
std::atomic_bool GL_API::s_glFlushQueued;
GLUtil::glTextureViewCache GL_API::s_textureViewCache = {};
GL_API::IMPrimitivePool GL_API::s_IMPrimitivePool = {};
eastl::fixed_vector<BufferLockEntry, 64, true> GL_API::s_bufferLockQueueMidFlush;
eastl::fixed_vector<BufferLockEntry, 64, true> GL_API::s_bufferLockQueueEndOfBuffer;

//1GB for buffers?
GLUtil::GLMemory::DeviceAllocator GL_API::s_memoryAllocator;

GL_API::GL_API(GFXDevice& context, const bool glES)
    : RenderAPIWrapper(),
      _context(context),
      _swapBufferTimer(Time::ADD_TIMER("Swap Buffer Timer"))
{
    ACKNOWLEDGE_UNUSED(glES);
    std::atomic_init(&s_glFlushQueued, false);
}


/// Prepare the GPU for rendering a frame
void GL_API::beginFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT();
    // Start a duration query in debug builds
    if (global && _runQueries) {
        if_constexpr(g_runAllQueriesInSameFrame) {
            for (U8 i = 0; i < to_base(QueryType::COUNT); ++i) {
                _performanceQueries[i]->begin();
            }
        } else {
            _performanceQueries[_queryIdxForCurrentFrame]->begin();
        }
    }

    GLStateTracker& stateTracker = getStateTracker();

    // Clear our buffers
    if (window.swapBuffers() && !window.minimized() && !window.hidden()) {
        SDL_GLContext glContext = static_cast<SDL_GLContext>(window.userData());
        const I64 windowGUID = window.getGUID();

        if (glContext != nullptr && (_currentContext.first != windowGUID || _currentContext.second != glContext)) {
            SDL_GL_MakeCurrent(window.getRawWindow(), glContext);
            _currentContext = std::make_pair(windowGUID, glContext);
        }

        bool shouldClearColour = false, shouldClearDepth = false, shouldClearStencil = false;
        stateTracker.setClearColour(window.clearColour(shouldClearColour, shouldClearDepth));
        ClearBufferMask mask = ClearBufferMask::GL_NONE_BIT;
        if (shouldClearColour) {
            mask |= ClearBufferMask::GL_COLOR_BUFFER_BIT;
        }
        if (shouldClearDepth) {
            mask |= ClearBufferMask::GL_DEPTH_BUFFER_BIT;
        }
        if (shouldClearStencil) {
            mask |= ClearBufferMask::GL_STENCIL_BUFFER_BIT;
        }
        if (mask != ClearBufferMask::GL_NONE_BIT) {
            glClear(mask);
        }
    }
    // Clears are registered as draw calls by most software, so we do the same
    // to stay in sync with third party software
    _context.registerDrawCall();

    clearStates(window, stateTracker, global);
}

/// Finish rendering the current frame
void GL_API::endFrame(DisplayWindow& window, const bool global) {
    OPTICK_EVENT("GL_API: endFrame");

    // End the timing query started in beginFrame() in debug builds
    if (global && _runQueries) {
        if_constexpr(g_runAllQueriesInSameFrame) {
            for (U8 i = 0; i < to_base(QueryType::COUNT); ++i) {
                _performanceQueries[i]->end();
            }
        } else {
            _performanceQueries[_queryIdxForCurrentFrame]->end();
        }
    }
    // Swap buffers
    {
        if (global) {
            if (glGetGraphicsResetStatus() != GL_NO_ERROR) { 
                DIVIDE_UNEXPECTED_CALL_MSG("OpenGL Reset Status raised!");
            }
            _swapBufferTimer.start();
        }

        if (window.swapBuffers() && !window.minimized() && !window.hidden()) {
            SDL_GLContext glContext = static_cast<SDL_GLContext>(window.userData());
            const I64 windowGUID = window.getGUID();
            
            if (glContext != nullptr && (_currentContext.first != windowGUID || _currentContext.second != glContext)) {
                OPTICK_EVENT("GL_API: Swap Context");
                SDL_GL_MakeCurrent(window.getRawWindow(), glContext);
                _currentContext = std::make_pair(windowGUID, glContext);
            }
            {
                OPTICK_EVENT("GL_API: Swap Buffers");
                SDL_GL_SwapWindow(window.getRawWindow());
            }
        }
        if (global) {
            _swapBufferTimer.stop();
            s_textureViewCache.onFrameEnd();
            s_glFlushQueued.store(false);
            if (s_UseBindlessTextures) {
                for (ResidentTexture& texture : s_residentTextures) {
                    if (texture._address == 0u) {
                        // Most common case
                        continue;
                    }

                    if (++texture._frameCount > g_maxTextureResidencyFrameCount) {
                        glMakeTextureHandleNonResidentARB(texture._address);
                        texture = {};
                    }
                }
            }
            if (_context.frameCount() % g_clearMemoryFrameInterval == 0) {
                glBufferImpl::CleanMemory();
                glTexture::CleanMemory();
            }
        }
    }

    if (global && _runQueries) {
          OPTICK_EVENT("GL_API: Time Query");
          static std::array<I64, to_base(QueryType::COUNT)> results{};
          if_constexpr(g_runAllQueriesInSameFrame) {
              for (U8 i = 0; i < to_base(QueryType::COUNT); ++i) {
                  results[i] = _performanceQueries[i]->getResultNoWait();
                  _performanceQueries[i]->incQueue();
              }
          } else {
              results[_queryIdxForCurrentFrame] = _performanceQueries[_queryIdxForCurrentFrame]->getResultNoWait();
              _performanceQueries[_queryIdxForCurrentFrame]->incQueue();
          }
          _queryIdxForCurrentFrame = (_queryIdxForCurrentFrame + 1) % to_base(QueryType::COUNT);

          if (g_runAllQueriesInSameFrame || _queryIdxForCurrentFrame == 0) {
              _perfMetrics._gpuTimeInMS = Time::NanosecondsToMilliseconds<F32>(results[to_base(QueryType::GPU_TIME)]);
              _perfMetrics._verticesSubmitted = to_U64(results[to_base(QueryType::VERTICES_SUBMITTED)]);
              _perfMetrics._primitivesGenerated = to_U64(results[to_base(QueryType::PRIMITIVES_GENERATED)]);
              _perfMetrics._tessellationPatches = to_U64(results[to_base(QueryType::TESSELLATION_PATCHES)]);
              _perfMetrics._tessellationInvocations = to_U64(results[to_base(QueryType::TESSELLATION_CTRL_INVOCATIONS)]);
          }
    }

    _runQueries = _context.queryPerformanceStats();
}
PerformanceMetrics GL_API::getPerformanceMetrics() const noexcept {
    return _perfMetrics;
}

void GL_API::idle(const bool fast) {
    OPTICK_EVENT("GL_API: fast idle");
    glShaderProgram::Idle(_context.context());

    if (!fast) {
        OPTICK_EVENT("GL_API: slow idle");
        UniqueLock<SharedMutex> w_lock(s_mipmapQueueSetLock);
        if (!s_mipmapQueue.empty()) {
            const auto it = s_mipmapQueue.begin();
            glGenerateTextureMipmap(*it);
            s_mipmapQueue.erase(it);
        }
    }
}

void GL_API::AppendToShaderHeader(const ShaderType type, const stringImpl& entry) {
    glswAddDirectiveToken(type != ShaderType::COUNT ? Names::shaderTypes[to_U8(type)] : "", entry.c_str());
}

bool GL_API::InitGLSW(Configuration& config) {
    constexpr std::pair<const char*, const char*> shaderVaryings[] =
    {
        { "vec4"       , "_vertexW"},
        { "vec4"       , "_vertexWV"},
        { "vec4"       , "_prevVertexWVP"},
        { "vec3"       , "_normalWV"},
        { "vec3"       , "_viewDirectionWV"},
        { "vec2"       , "_texCoord"},
        { "flat uint"  , "_baseInstance" }
    };

    constexpr std::pair<const char*, const char*> shaderVaryingsBump[] =
    {
        { "mat3" , "_tbnWV"},
    };

    constexpr const char* crossTypeGLSLHLSL = "#define float2 vec2\n"
                                              "#define float3 vec3\n"
                                              "#define float4 vec4\n"
                                              "#define int2 ivec2\n"
                                              "#define int3 ivec3\n"
                                              "#define int4 ivec4\n"
                                              "#define float2x2 mat2\n"
                                              "#define float3x3 mat3\n"
                                              "#define float4x4 mat4\n"
                                              "#define lerp mix";

    const auto getPassData = [&](const ShaderType type) -> stringImpl {
        stringImpl baseString = "     _out.%s = _in[index].%s;";
        if (type == ShaderType::TESSELLATION_CTRL) {
            baseString = "    _out[gl_InvocationID].%s = _in[index].%s;";
        }

        stringImpl passData("void PassData(in int index) {");
        passData.append("\n");
        for (const auto& [varType, name] : shaderVaryings) {
            passData.append(Util::StringFormat(baseString.c_str(), name, name));
            passData.append("\n");
        }
        passData.append("#if defined(COMPUTE_TBN)\n");
        for (const auto& [varType, name] : shaderVaryingsBump) {
            passData.append(Util::StringFormat(baseString.c_str(), name, name));
            passData.append("\n");
        }
        passData.append("#endif\n");

        passData.append("}\n");

        return passData;
    };

    // Initialize GLSW
    GLint glswState = -1;
    if (!glswGetCurrentContext()) {
        glswState = glswInit();
        DIVIDE_ASSERT(glswState == 1);
    }

    I32 numLightsPerCluster = config.rendering.numLightsPerCluster;
    if (numLightsPerCluster < 0) {
        numLightsPerCluster = to_I32(Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER);
    } else {
        numLightsPerCluster = std::min(numLightsPerCluster, to_I32(Config::Lighting::ClusteredForward::MAX_LIGHTS_PER_CLUSTER));
    }
    if (numLightsPerCluster != config.rendering.numLightsPerCluster) {
        config.rendering.numLightsPerCluster = numLightsPerCluster;
        config.changed(true);
    }
    const U16 reflectionProbeRes = to_U16(nextPOW2(CLAMPED(to_U32(config.rendering.reflectionProbeResolution), 16u, 4096u) - 1u));
    if (reflectionProbeRes != config.rendering.reflectionProbeResolution) {
        config.rendering.reflectionProbeResolution = reflectionProbeRes;
        config.changed(true);
    }

    vec3<U8> gridSize = config.rendering.lightClusteredSizes;
    CLAMP(gridSize.x, to_U8(1), to_U8(255));
    CLAMP(gridSize.y, to_U8(1), to_U8(255));
    CLAMP(gridSize.z, to_U8(Config::Lighting::ClusteredForward::CLUSTER_Z_THREADS), to_U8(255));

    if (gridSize != config.rendering.lightClusteredSizes) {
        config.rendering.lightClusteredSizes = gridSize;
        config.changed(true);
    }  

    DIVIDE_ASSERT(Config::MAX_CULL_DISTANCES <= GLUtil::getGLValue(GL_MAX_COMBINED_CLIP_AND_CULL_DISTANCES) - Config::MAX_CLIP_DISTANCES,
                  "GLWrapper error: incorrect combination of clip and cull distance counts");
    static_assert(Config::MAX_BONE_COUNT_PER_NODE <= 1024, "GLWrapper error: too many bones per vert. Can't fit inside UBO");
    // Add our engine specific defines and various code pieces to every GLSL shader
    // Add version as the first shader statement, followed by copyright notice
    AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#version 4%d0 core", GLUtil::getGLValue(GL_MINOR_VERSION)));
    AppendToShaderHeader(ShaderType::COUNT, "/*Copyright 2009-2020 DIVIDE-Studio*/");

    if (s_UseBindlessTextures) {
        AppendToShaderHeader(ShaderType::COUNT, "#extension  GL_ARB_bindless_texture : require");
    }

    AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_gpu_shader5 : require");
    if (!getStateTracker()._opengl46Supported) {
        AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_shader_draw_parameters : require");
        AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_cull_distance : require");
        AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_enhanced_layouts : require");

        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_DRAW_ID gl_DrawIDARB");
        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_VERTEX gl_BaseVertexARB");
        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_INSTANCE gl_BaseInstanceARB");
    } else {
        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_DRAW_ID gl_DrawID");
        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_VERTEX gl_BaseVertex");
        AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_INSTANCE gl_BaseInstance");
    }

    AppendToShaderHeader(ShaderType::COUNT, crossTypeGLSLHLSL);

    // Add current build environment information to the shaders
    if_constexpr(Config::Build::IS_DEBUG_BUILD) {
        AppendToShaderHeader(ShaderType::COUNT, "#define _DEBUG");
    } else if_constexpr(Config::Build::IS_PROFILE_BUILD) {
        AppendToShaderHeader(ShaderType::COUNT, "#define _PROFILE");
    } else {
        AppendToShaderHeader(ShaderType::COUNT, "#define _RELEASE");
    }

    // Shader stage level reflection system. A shader stage must know what stage it's used for
    AppendToShaderHeader(ShaderType::VERTEX,   "#define VERT_SHADER");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define FRAG_SHADER");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define GEOM_SHADER");
    AppendToShaderHeader(ShaderType::COMPUTE,  "#define COMPUTE_SHADER");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define TESS_EVAL_SHADER");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define TESS_CTRL_SHADER");

    // This line gets replaced in every shader at load with the custom list of defines specified by the material
    AppendToShaderHeader(ShaderType::COUNT, "_CUSTOM_DEFINES__");

    // Add some nVidia specific pragma directives
    if (GFXDevice::getGPUVendor() == GPUVendor::NVIDIA) {
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma option fastmath on");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma option fastprecision on");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma option inline all");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma option ifcvt none");
        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            AppendToShaderHeader(ShaderType::COUNT, "#pragma option strict on");
        }
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma option unroll all");
    }

    if_constexpr(Config::USE_COLOURED_WOIT) {
        AppendToShaderHeader(ShaderType::COUNT, "#define USE_COLOURED_WOIT");
    }

    if (s_UseBindlessTextures) {
        AppendToShaderHeader(ShaderType::COUNT, "#define USE_BINDLESS_TEXTURES");
    }

    constexpr F32 Z_TEST_SIGMA = 0.00001f;// 1.f / U8_MAX;

    AppendToShaderHeader(ShaderType::COUNT,    "#define Z_TEST_SIGMA "                    + Util::to_string(Z_TEST_SIGMA) + "f");
    AppendToShaderHeader(ShaderType::COUNT,    "#define INV_Z_TEST_SIGMA "                + Util::to_string(1.f - Z_TEST_SIGMA) + "f");
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_CSM_SPLITS_PER_LIGHT "        + Util::to_string(Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_SHADOW_CASTING_LIGHTS "       + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_SHADOW_CASTING_DIR_LIGHTS "   + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_SHADOW_CASTING_POINT_LIGHTS " + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_SHADOW_CASTING_SPOT_LIGHTS "  + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_LIGHTS "                      + Util::to_string(Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_VISIBLE_NODES "               + Util::to_string(Config::MAX_VISIBLE_NODES));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_CONCURRENT_MATERIALS "        + Util::to_string(Config::MAX_CONCURRENT_MATERIALS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_CLIP_PLANES "                 + Util::to_string(Config::MAX_CLIP_DISTANCES));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_CULL_DISTANCES "              + Util::to_string(Config::MAX_CULL_DISTANCES));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_ACCUMULATION "             + Util::to_string(to_base(GFXDevice::ScreenTargets::ACCUMULATION)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_ALBEDO "                   + Util::to_string(to_base(GFXDevice::ScreenTargets::ALBEDO)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_VELOCITY "                 + Util::to_string(to_base(GFXDevice::ScreenTargets::VELOCITY)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_NORMALS_AND_MATERIAL_DATA "+ Util::to_string(to_base(GFXDevice::ScreenTargets::NORMALS_AND_MATERIAL_PROPERTIES)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_SPECULAR "                 + Util::to_string(to_base(GFXDevice::ScreenTargets::SPECULAR)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_REVEALAGE "                + Util::to_string(to_base(GFXDevice::ScreenTargets::REVEALAGE)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define TARGET_MODULATE "                 + Util::to_string(to_base(GFXDevice::ScreenTargets::MODULATE)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_GPU_BLOCK "                + Util::to_string(to_base(ShaderBufferLocation::GPU_BLOCK)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_ATOMIC_COUNTER "           + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_GPU_COMMANDS "             + Util::to_string(to_base(ShaderBufferLocation::GPU_COMMANDS)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_NORMAL "             + Util::to_string(to_base(ShaderBufferLocation::LIGHT_NORMAL)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_SHADOW "             + Util::to_string(to_base(ShaderBufferLocation::LIGHT_SHADOW)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_INDICES "            + Util::to_string(to_base(ShaderBufferLocation::LIGHT_INDICES)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_GRID "               + Util::to_string(to_base(ShaderBufferLocation::LIGHT_GRID)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_INDEX_COUNT "        + Util::to_string(to_base(ShaderBufferLocation::LIGHT_INDEX_COUNT)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_LIGHT_CLUSTER_AABBS "      + Util::to_string(to_base(ShaderBufferLocation::LIGHT_CLUSTER_AABBS)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_NODE_TRANSFORM_DATA "      + Util::to_string(to_base(ShaderBufferLocation::NODE_TRANSFORM_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_NODE_MATERIAL_DATA "       + Util::to_string(to_base(ShaderBufferLocation::NODE_MATERIAL_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_SCENE_DATA "               + Util::to_string(to_base(ShaderBufferLocation::SCENE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_PROBE_DATA "               + Util::to_string(to_base(ShaderBufferLocation::PROBE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_COMMANDS "                 + Util::to_string(to_base(ShaderBufferLocation::CMD_BUFFER)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_GRASS_DATA "               + Util::to_string(to_base(ShaderBufferLocation::GRASS_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_TREE_DATA "                + Util::to_string(to_base(ShaderBufferLocation::TREE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define BUFFER_UNIFORM_BLOCK "            + Util::to_string(to_base(ShaderBufferLocation::UNIFORM_BLOCK)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define GRID_SIZE_X "                     + Util::to_string(gridSize.x));
    AppendToShaderHeader(ShaderType::COUNT,    "#define GRID_SIZE_Y "                     + Util::to_string(gridSize.y));
    AppendToShaderHeader(ShaderType::COUNT,    "#define GRID_SIZE_Z "                     + Util::to_string(gridSize.z));
    AppendToShaderHeader(ShaderType::COUNT,    "#define GRID_SIZE_Z_THREADS "             + Util::to_string(Config::Lighting::ClusteredForward::CLUSTER_Z_THREADS));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MAX_LIGHTS_PER_CLUSTER "          + Util::to_string(numLightsPerCluster));
    AppendToShaderHeader(ShaderType::COUNT,    "#define REFLECTION_PROBE_RESOLUTION "     + Util::to_string(reflectionProbeRes));
    for (U8 i = 0; i < to_base(TextureUsage::COUNT); ++i) {
        AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define TEXTURE_%s %d", TypeUtil::TextureUsageToString(static_cast<TextureUsage>(i)), i).c_str());
    }
    for (U8 i = 0; i < to_base(TextureOperation::COUNT); ++i) {
        AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define TEX_%s %d", TypeUtil::TextureOperationToString(static_cast<TextureOperation>(i)), i).c_str());
    }
    AppendToShaderHeader(ShaderType::COUNT,    "#define GLOBAL_WATER_BODIES_COUNT "       + Util::to_string(GLOBAL_WATER_BODIES_COUNT));
    AppendToShaderHeader(ShaderType::COUNT,    "#define GLOBAL_PROBE_COUNT "              + Util::to_string(GLOBAL_PROBE_COUNT + 0));
    AppendToShaderHeader(ShaderType::COUNT,    "#define PROBE_ID_NO_REFLECTIONS "         + Util::to_string(GLOBAL_PROBE_COUNT + 1));
    AppendToShaderHeader(ShaderType::COUNT,    "#define PROBE_ID_NO_SSR "                 + Util::to_string(GLOBAL_PROBE_COUNT + 2));
    AppendToShaderHeader(ShaderType::COUNT,    "#define PROBE_ID_NO_ENV_REFLECTIONS "     + Util::to_string(GLOBAL_PROBE_COUNT + 3));
    AppendToShaderHeader(ShaderType::COUNT,    "#define MATERIAL_TEXTURE_COUNT "          + Util::to_string(MATERIAL_TEXTURE_COUNT));
    AppendToShaderHeader(ShaderType::COMPUTE,  "#define BUFFER_LUMINANCE_HISTOGRAM "      + Util::to_string(to_base(ShaderBufferLocation::LUMINANCE_HISTOGRAM)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define BUFFER_BONE_TRANSFORMS "          + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define BUFFER_BONE_TRANSFORMS_PREV "     + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS_PREV)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define MAX_BONE_COUNT_PER_NODE "         + Util::to_string(Config::MAX_BONE_COUNT_PER_NODE));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_POSITION "                 + Util::to_string(to_base(AttribLocation::POSITION)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_TEXCOORD "                 + Util::to_string(to_base(AttribLocation::TEXCOORD)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_NORMAL "                   + Util::to_string(to_base(AttribLocation::NORMAL)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_TANGENT "                  + Util::to_string(to_base(AttribLocation::TANGENT)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_COLOR "                    + Util::to_string(to_base(AttribLocation::COLOR)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_BONE_WEIGHT "              + Util::to_string(to_base(AttribLocation::BONE_WEIGHT)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_BONE_INDICE "              + Util::to_string(to_base(AttribLocation::BONE_INDICE)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_WIDTH "                    + Util::to_string(to_base(AttribLocation::WIDTH)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_GENERIC "                  + Util::to_string(to_base(AttribLocation::GENERIC)));

    for (U8 i = 0u; i < to_U8(MaterialDebugFlag::COUNT) + 1u; ++i) {
        const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
        AppendToShaderHeader(ShaderType::FRAGMENT, Util::StringFormat("#define DEBUG_%s %d", TypeUtil::MaterialDebugFlagToString(flag), i));
    }

    const auto addVaryings = [&](const ShaderType type) {
        for (const auto& [varType, name] : shaderVaryings) {
            AppendToShaderHeader(type, Util::StringFormat("    %s %s;", varType, name));
        }
    };

    const auto addVaryingsBump = [&](const ShaderType type) {
        for (const auto& [varType, name] : shaderVaryingsBump) {
            AppendToShaderHeader(type, Util::StringFormat("    %s %s;", varType, name));
        }
    };

    // Vertex shader output
    AppendToShaderHeader(ShaderType::VERTEX, "out Data {");
    addVaryings(ShaderType::VERTEX);
    AppendToShaderHeader(ShaderType::VERTEX, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::VERTEX);
    AppendToShaderHeader(ShaderType::VERTEX, "#endif");
    AppendToShaderHeader(ShaderType::VERTEX, "} _out;\n");

    // Tessellation Control shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "in Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#endif");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _in[];\n");

    // Tessellation Control shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "out Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#endif");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _out[];\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, getPassData(ShaderType::TESSELLATION_CTRL));

    // Tessellation Eval shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "in Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#endif");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _in[];\n");

    // Tessellation Eval shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "out Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#endif");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _out;\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, getPassData(ShaderType::TESSELLATION_EVAL));

    // Geometry shader input
    AppendToShaderHeader(ShaderType::GEOMETRY, "in Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "#endif");
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _in[];\n");

    // Geometry shader output
    AppendToShaderHeader(ShaderType::GEOMETRY, "out Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "#endif");
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _out;\n");

    AppendToShaderHeader(ShaderType::GEOMETRY, getPassData(ShaderType::GEOMETRY));

    // Fragment shader input
    AppendToShaderHeader(ShaderType::FRAGMENT, "in Data {");
    addVaryings(ShaderType::FRAGMENT);
    AppendToShaderHeader(ShaderType::FRAGMENT, "#if defined(COMPUTE_TBN)");
    addVaryingsBump(ShaderType::FRAGMENT);
    AppendToShaderHeader(ShaderType::FRAGMENT, "#endif");
    AppendToShaderHeader(ShaderType::FRAGMENT, "} _in;\n");

    AppendToShaderHeader(ShaderType::VERTEX, "#define VAR _out");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define VAR _in[gl_InvocationID]");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define VAR _in");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define VAR _in");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define VAR _in");

    // GPU specific data, such as GFXDevice's main uniform block and clipping
    // planes are defined in an external file included in every shader
    AppendToShaderHeader(ShaderType::COUNT, "#include \"nodeDataInput.cmn\"");
    AppendToShaderHeader(ShaderType::COUNT, "_CUSTOM_UNIFORMS__");

    // Check initialization status for GLSL and glsl-optimizer
    return glswState == 1;
}

bool GL_API::DeInitGLSW() {
    // Shutdown GLSW
    return glswShutdown() == 1;
}

/// Try to find the requested font in the font cache. Load on cache miss.
I32 GL_API::getFont(const Str64& fontName) {
    if (_fontCache.first.compare(fontName) != 0) {
        _fontCache.first = fontName;
        const U64 fontNameHash = _ID(fontName.c_str());
        // Search for the requested font by name
        const auto& it = _fonts.find(fontNameHash);
        // If we failed to find it, it wasn't loaded yet
        if (it == std::cend(_fonts)) {
            // Fonts are stored in the general asset directory -> in the GUI
            // subfolder -> in the fonts subfolder
            ResourcePath fontPath(Paths::g_assetsLocation + Paths::g_GUILocation + Paths::g_fontsPath);
            fontPath += fontName.c_str();
            // We use FontStash to load the font file
            _fontCache.second = fonsAddFont(_fonsContext, fontName.c_str(), fontPath.c_str());
            // If the font is invalid, inform the user, but map it anyway, to avoid
            // loading an invalid font file on every request
            if (_fontCache.second == FONS_INVALID) {
                Console::errorfn(Locale::Get(_ID("ERROR_FONT_FILE")), fontName.c_str());
            }
            // Save the font in the font cache
            hashAlg::insert(_fonts, fontNameHash, _fontCache.second);
            
        } else {
            _fontCache.second = it->second;
        }

    }

    // Return the font
    return _fontCache.second;
}

/// Text rendering is handled exclusively by Mikko Mononen's FontStash library (https://github.com/memononen/fontstash)
/// with his OpenGL frontend adapted for core context profiles
void GL_API::drawText(const TextElementBatch& batch) {
    OPTICK_EVENT();

    getStateTracker().setBlending(0,
        BlendingProperties{
            BlendProperty::SRC_ALPHA,
            BlendProperty::INV_SRC_ALPHA,
            BlendOperation::ADD,

            BlendProperty::ONE,
            BlendProperty::ZERO,
            BlendOperation::COUNT,

            true //enabled
        }
    );
    getStateTracker().setBlendColour(DefaultColours::BLACK_U8);

    const I32 width = _context.renderingResolution().width;
    const I32 height = _context.renderingResolution().height;
        
    size_t drawCount = 0;
    size_t previousStyle = 0;

    fonsClearState(_fonsContext);
    for (const TextElement& entry : batch.data())
    {
        if (previousStyle != entry.textLabelStyleHash()) {
            const TextLabelStyle& textLabelStyle = TextLabelStyle::get(entry.textLabelStyleHash());
            const UColour4& colour = textLabelStyle.colour();
            // Retrieve the font from the font cache
            const I32 font = getFont(TextLabelStyle::fontName(textLabelStyle.font()));
            // The font may be invalid, so skip this text label
            if (font != FONS_INVALID) {
                fonsSetFont(_fonsContext, font);
            }
            fonsSetBlur(_fonsContext, textLabelStyle.blurAmount());
            fonsSetBlur(_fonsContext, textLabelStyle.spacing());
            fonsSetAlign(_fonsContext, textLabelStyle.alignFlag());
            fonsSetSize(_fonsContext, to_F32(textLabelStyle.fontSize()));
            fonsSetColour(_fonsContext, colour.r, colour.g, colour.b, colour.a);
            previousStyle = entry.textLabelStyleHash();
        }

        const F32 textX = entry.position().d_x.d_scale * width + entry.position().d_x.d_offset;
        const F32 textY = height - (entry.position().d_y.d_scale * height + entry.position().d_y.d_offset);

        F32 lh = 0;
        fonsVertMetrics(_fonsContext, nullptr, nullptr, &lh);
        
        const TextElement::TextType& text = entry.text();
        const size_t lineCount = text.size();
        for (size_t i = 0; i < lineCount; ++i) {
            fonsDrawText(_fonsContext,
                         textX,
                         textY - lh * i,
                         text[i].c_str(),
                         nullptr);
        }
        drawCount += lineCount;
        

        // Register each label rendered as a draw call
        _context.registerDrawCalls(to_U32(drawCount));
    }
}

void GL_API::drawIMGUI(ImDrawData* data, I64 windowGUID) {
    OPTICK_EVENT();

    if (data != nullptr && data->Valid) {
        auto& stateTracker = getStateTracker();

        GenericVertexData::IndexBuffer idxBuffer;
        GenericDrawCommand cmd = {};
        cmd._primitiveType = PrimitiveType::TRIANGLES;

        GenericVertexData* buffer = getOrCreateIMGUIBuffer(windowGUID);
        assert(buffer != nullptr);

        const ImVec2 pos = data->DisplayPos;
        for (I32 n = 0; n < data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = data->CmdLists[n];

            cmd._cmd.firstIndex = 0;

            idxBuffer.smallIndices = sizeof(ImDrawIdx) == 2;
            idxBuffer.count = to_U32(cmd_list->IdxBuffer.Size);
            idxBuffer.data = reinterpret_cast<Byte*>(cmd_list->IdxBuffer.Data);

            buffer->waitBufferRange(0u, 0u, to_U32(cmd_list->VtxBuffer.size()), true);
            buffer->updateBuffer(0u, 0u, to_U32(cmd_list->VtxBuffer.size()), cmd_list->VtxBuffer.Data);
            buffer->updateIndexBuffer(idxBuffer);

            for (I32 cmd_i = 0; cmd_i < cmd_list->CmdBuffer.size(); ++cmd_i)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

                if (pcmd->UserCallback) {
                    // User callback (registered via ImDrawList::AddCallback)
                    pcmd->UserCallback(cmd_list, pcmd);
                } else {
                    Rect<I32> clip_rect = {
                        pcmd->ClipRect.x - pos.x,
                        pcmd->ClipRect.y - pos.y,
                        pcmd->ClipRect.z - pos.x,
                        pcmd->ClipRect.w - pos.y
                    };

                    const Rect<I32>& viewport = stateTracker._activeViewport;
                    if (clip_rect.x < viewport.z &&
                        clip_rect.y < viewport.w &&
                        clip_rect.z >= 0 &&
                        clip_rect.w >= 0)
                    {
                        const I32 tempW = clip_rect.w;
                        clip_rect.z -= clip_rect.x;
                        clip_rect.w -= clip_rect.y;
                        clip_rect.y  = viewport.w - tempW;

                        stateTracker.setScissor(clip_rect);
                        stateTracker.bindTexture(0, TextureType::TEXTURE_2D, static_cast<GLuint>(reinterpret_cast<intptr_t>(pcmd->TextureId)));
                        cmd._cmd.indexCount = to_U32(pcmd->ElemCount);
                        buffer->draw(cmd);
                     }
                }
                cmd._cmd.firstIndex += pcmd->ElemCount;
            }
            buffer->lockBuffers();
            buffer->incQueue();
        }
    }
}

bool GL_API::bindPipeline(const Pipeline& pipeline, bool& shaderWasReady) const {
    OPTICK_EVENT();
    auto& stateTracker = getStateTracker();

    if (stateTracker._activePipeline && *stateTracker._activePipeline == pipeline) {
        return true;
    }

    ShaderProgram* program = ShaderProgram::FindShaderProgram(pipeline.shaderProgramHandle());
    if (program == nullptr) {
        return false;
    }

    stateTracker._activePipeline = &pipeline;
    // Set the proper render states
    const size_t stateBlockHash = pipeline.stateHash();
    // Passing 0 is a perfectly acceptable way of enabling the default render state block
    stateTracker.setStateBlock(stateBlockHash != 0 ? stateBlockHash : _context.getDefaultStateBlock(false));

    glShaderProgram& glProgram = static_cast<glShaderProgram&>(*program);
    // We need a valid shader as no fixed function pipeline is available

    // Try to bind the shader program. If it failed to load, or isn't loaded yet, cancel the draw request for this frame
    const auto[state, wasBound] = Attorney::GLAPIShaderProgram::bind(glProgram);
    shaderWasReady = state;

    if (!shaderWasReady) {
        stateTracker.setActiveProgram(0u);
        stateTracker.setActiveShaderPipeline(0u);
        stateTracker._activePipeline = nullptr;
        return false;
    }

    return true;
}

bool GL_API::draw(const GenericDrawCommand& cmd) const {
    OPTICK_EVENT();

    if (cmd._sourceBuffer._id == 0) {
        getStateTracker().setActiveVAO(s_dummyVAO);

        U32 indexCount = 0u;
        switch (cmd._primitiveType) {
            case PrimitiveType::TRIANGLES  : indexCount = cmd._drawCount * 3; break;
            case PrimitiveType::API_POINTS : indexCount = cmd._drawCount; break;
            case PrimitiveType::COUNT      : DIVIDE_UNEXPECTED_CALL(); break;
            default                        : indexCount = cmd._cmd.indexCount; break;
        }

        glDrawArrays(GLUtil::glPrimitiveTypeTable[to_U32(cmd._primitiveType)], cmd._cmd.firstIndex, indexCount);
    } else {
        // Because this can only happen on the main thread, try and avoid costly lookups for hot-loop drawing
        static PoolHandle lastID = { U16_MAX, 0u };
        static VertexDataInterface* lastBuffer = nullptr;
        if (lastID != cmd._sourceBuffer) {
            lastID = cmd._sourceBuffer;
            lastBuffer = VertexDataInterface::s_VDIPool.find(lastID);
        }

        assert(lastBuffer != nullptr);
        lastBuffer->draw(cmd);
    }

    return true;
}

void GL_API::PushDebugMessage(const char* message) {
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, static_cast<GLuint>(_ID(message)), -1, message);
        getStateTracker()._debugScope = message;
    }
}

void GL_API::PopDebugMessage() {
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        glPopDebugGroup();
        getStateTracker()._debugScope.clear();
    }
}

void GL_API::preFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) {
    ACKNOWLEDGE_UNUSED(commandBuffer);
    getStateTracker()._flushingCommandBuffer = true;
}

void GL_API::flushCommand(const GFX::CommandBuffer::CommandEntry& entry, const GFX::CommandBuffer& commandBuffer) {
    OPTICK_EVENT();

    const auto cmdType = static_cast<GFX::CommandType>(entry._typeIndex);

    OPTICK_TAG("Type", to_base(cmdType));

    switch (cmdType) {
        case GFX::CommandType::BEGIN_RENDER_PASS: {
            GFX::BeginRenderPassCommand* crtCmd = commandBuffer.get<GFX::BeginRenderPassCommand>(entry);
            glFramebuffer& rt = static_cast<glFramebuffer&>(_context.renderTargetPool().renderTarget(crtCmd->_target));
            Attorney::GLAPIRenderTarget::begin(rt, crtCmd->_descriptor);
            getStateTracker()._activeRenderTarget = &rt;
            PushDebugMessage(crtCmd->_name.c_str());
        }break;
        case GFX::CommandType::END_RENDER_PASS: {
            GFX::EndRenderPassCommand* crtCmd = commandBuffer.get<GFX::EndRenderPassCommand>(entry);
            assert(GL_API::getStateTracker()._activeRenderTarget != nullptr);
            PopDebugMessage();
            glFramebuffer& fb = *getStateTracker()._activeRenderTarget;
            Attorney::GLAPIRenderTarget::end(fb, crtCmd->_setDefaultRTState);
        }break;
        case GFX::CommandType::BEGIN_PIXEL_BUFFER: {
            GFX::BeginPixelBufferCommand* crtCmd = commandBuffer.get<GFX::BeginPixelBufferCommand>(entry);
            assert(crtCmd->_buffer != nullptr);
            glPixelBuffer* buffer = static_cast<glPixelBuffer*>(crtCmd->_buffer);
            const bufferPtr data = Attorney::GLAPIPixelBuffer::begin(*buffer);
            if (crtCmd->_command) {
                crtCmd->_command(data);
            }
            getStateTracker()._activePixelBuffer = buffer;
        }break;
        case GFX::CommandType::END_PIXEL_BUFFER: {
            assert(GL_API::getStateTracker()._activePixelBuffer != nullptr);
            Attorney::GLAPIPixelBuffer::end(*getStateTracker()._activePixelBuffer);
        }break;
        case GFX::CommandType::BEGIN_RENDER_SUB_PASS: {
            assert(GL_API::getStateTracker()._activeRenderTarget != nullptr);
            GFX::BeginRenderSubPassCommand* crtCmd = commandBuffer.get<GFX::BeginRenderSubPassCommand>(entry);
            for (const RenderTarget::DrawLayerParams& params : crtCmd->_writeLayers) {
                getStateTracker()._activeRenderTarget->drawToLayer(params);
            }

            getStateTracker()._activeRenderTarget->setMipLevel(crtCmd->_mipWriteLevel);
        }break;
        case GFX::CommandType::END_RENDER_SUB_PASS: {
        }break;
        case GFX::CommandType::SET_BLEND_STATE: {
            assert(GL_API::getStateTracker()._activeRenderTarget != nullptr);
            GFX::SetBlendStateCommand* crtCmd = commandBuffer.get<GFX::SetBlendStateCommand>(entry);
            getStateTracker()._activeRenderTarget->setBlendState(crtCmd->_blendStates);
        }break;
        case GFX::CommandType::COPY_TEXTURE: {
            GFX::CopyTextureCommand* crtCmd = commandBuffer.get<GFX::CopyTextureCommand>(entry);
            glTexture::copy(crtCmd->_source, crtCmd->_destination, crtCmd->_params);
        }break;
        case GFX::CommandType::BIND_DESCRIPTOR_SETS: {
            GFX::BindDescriptorSetsCommand* crtCmd = commandBuffer.get<GFX::BindDescriptorSetsCommand>(entry);
            DescriptorSet& set = crtCmd->_set;

            if (!makeTexturesResident(set._textureData, set._textureViews)) {
                //Error
            }
            if (!makeImagesResident(set._images)) {
                //Error
            }

            OPTICK_EVENT("Bind Shader Buffers");
            for (U8 i = 0; i < set._buffers.count(); ++i) {
                const ShaderBufferBinding& binding = set._buffers._entries[i];
                if (binding._binding == ShaderBufferLocation::COUNT) {
                    // might be leftover from a batching call
                    continue;
                }

                assert(binding._buffer != nullptr);
                glUniformBuffer* buffer = static_cast<glUniformBuffer*>(binding._buffer);
                buffer->bindRange(to_U8(binding._binding), binding._elementRange.min, binding._elementRange.max);
            }

        }break;
        case GFX::CommandType::BIND_PIPELINE: {
            const Pipeline* pipeline = commandBuffer.get<GFX::BindPipelineCommand>(entry)->_pipeline;
            assert(pipeline != nullptr);
            bool shaderWasReady = false;
            if (!bindPipeline(*pipeline, shaderWasReady) && shaderWasReady) {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INVALID_BIND")), pipeline->shaderProgramHandle());
            }
        } break;
        case GFX::CommandType::SEND_PUSH_CONSTANTS: {
            OPTICK_EVENT();

            const auto dumpLogs = [this]() {
                Console::d_errorfn(Locale::Get(_ID("ERROR_GLSL_INVALID_PUSH_CONSTANTS")));
                if (Config::ENABLE_GPU_VALIDATION) {
                    // Shader failed to compile probably. Dump all shader caches for inspection.
                    glShaderProgram::Idle(_context.context());
                    Console::flush();
                }
            };

            const Pipeline* activePipeline = getStateTracker()._activePipeline;
            if (activePipeline == nullptr) {
                dumpLogs();
                break;
            }

            ShaderProgram* program = ShaderProgram::FindShaderProgram(activePipeline->shaderProgramHandle());
            if (program == nullptr) {
                // Should we skip the upload?
                dumpLogs();
                break;
            }

            const PushConstants& pushConstants = commandBuffer.get<GFX::SendPushConstantsCommand>(entry)->_constants;
            static_cast<glShaderProgram*>(program)->uploadPushConstants(pushConstants);
        } break;
        case GFX::CommandType::SET_SCISSOR: {
            getStateTracker().setScissor(commandBuffer.get<GFX::SetScissorCommand>(entry)->_rect);
        }break;
        case GFX::CommandType::SET_BLEND: {
            getStateTracker().setBlending(commandBuffer.get<GFX::SetBlendCommand>(entry)->_blendProperties);
        }break;
        case GFX::CommandType::SET_TEXTURE_RESIDENCY:
        {
            GFX::SetTexturesResidencyCommand* crtCmd = commandBuffer.get<GFX::SetTexturesResidencyCommand>(entry);
            if (crtCmd->_state) {
                for (SamplerAddress address : crtCmd->_addresses) {
                    MakeTexturesResidentInternal(address);
                }
            } else {
                for (SamplerAddress address : crtCmd->_addresses) {
                    MakeTexturesNonResidentInternal(address);
                }
            }
        }break;
        case GFX::CommandType::BEGIN_DEBUG_SCOPE: {
             GFX::BeginDebugScopeCommand* crtCmd = commandBuffer.get<GFX::BeginDebugScopeCommand>(entry);
             PushDebugMessage(crtCmd->_scopeName.c_str());
        } break;
        case GFX::CommandType::END_DEBUG_SCOPE: {
             PopDebugMessage();
        } break;
        case GFX::CommandType::COMPUTE_MIPMAPS: {
            OPTICK_EVENT("GL: Compute MipMaps");
            GFX::ComputeMipMapsCommand* crtCmd = commandBuffer.get<GFX::ComputeMipMapsCommand>(entry);
            if (!crtCmd->_clearOnly) {
                if (crtCmd->_layerRange.x == 0 && crtCmd->_layerRange.y == crtCmd->_texture->descriptor().layerCount()) {
                    if (crtCmd->_defer) {
                        OPTICK_EVENT("GL: Deferred computation");
                        QueueComputeMipMap(crtCmd->_texture->data()._textureHandle);
                    } else {
                        OPTICK_EVENT("GL: In-place computation");
                        const GLuint handle = crtCmd->_texture->data()._textureHandle;
                        glGenerateTextureMipmap(handle);
                        DequeueComputeMipMap(handle);
                    }
                } else {
                    OPTICK_EVENT("GL: View-based computation");

                    const TextureDescriptor& descriptor = crtCmd->_texture->descriptor();
                    const GLenum glInternalFormat = GLUtil::internalFormat(descriptor.baseFormat(), descriptor.dataType(), descriptor.srgb(), descriptor.normalized());

                    TextureView view = {};
                    view._textureData = crtCmd->_texture->data();
                    view._layerRange.set(crtCmd->_layerRange);
                    view._targetType = view._textureData._textureType;

                    if (crtCmd->_mipRange.max == 0u) {
                        view._mipLevels.set(0, descriptor.mipCount());
                    } else {
                        view._mipLevels.set(crtCmd->_mipRange);
                    }
                    assert(IsValid(view._textureData));

                    if (IsArrayTexture(view._targetType) && view._layerRange.max == 1) {
                        switch (view._targetType) {
                          case TextureType::TEXTURE_2D_ARRAY:
                              view._targetType = TextureType::TEXTURE_2D;
                              break;
                          case TextureType::TEXTURE_2D_ARRAY_MS:
                              view._targetType = TextureType::TEXTURE_2D_MS;
                              break;
                          case TextureType::TEXTURE_CUBE_ARRAY:
                              view._targetType = TextureType::TEXTURE_CUBE_MAP;
                              break;
                          default: break;
                        }
                    }

                    if (IsCubeTexture(view._targetType)) {
                        view._layerRange *= 6; //offset and count
                    }

                    auto[handle, cacheHit] = s_textureViewCache.allocate(view.getHash());

                    if (!cacheHit)
                    {
                        OPTICK_EVENT("GL: View cache miss");
                        glTextureView(handle,
                                      GLUtil::glTextureTypeTable[to_base(view._targetType)],
                                      view._textureData._textureHandle,
                                      glInternalFormat,
                                      static_cast<GLuint>(view._mipLevels.x),
                                      static_cast<GLuint>(view._mipLevels.y),
                                      static_cast<GLuint>(view._layerRange.x),
                                      static_cast<GLuint>(view._layerRange.y));
                    }

                    if (crtCmd->_defer) {
                        OPTICK_EVENT("GL: Deferred computation");
                        QueueComputeMipMap(handle);
                    } else {
                        OPTICK_EVENT("GL: In-place computation");
                        glGenerateTextureMipmap(handle);
                        DequeueComputeMipMap(handle);
                    }
                    s_textureViewCache.deallocate(handle, 3);
                }
            }
        }break;
        case GFX::CommandType::DRAW_TEXT: {
            if (getStateTracker()._activePipeline != nullptr) {
                GFX::DrawTextCommand* crtCmd = commandBuffer.get<GFX::DrawTextCommand>(entry);
                drawText(crtCmd->_batch);
            }
        }break;
        case GFX::CommandType::DRAW_IMGUI: {
            if (getStateTracker()._activePipeline != nullptr) {
                GFX::DrawIMGUICommand* crtCmd = commandBuffer.get<GFX::DrawIMGUICommand>(entry);
                drawIMGUI(crtCmd->_data, crtCmd->_windowGUID);
            }
        }break;
        case GFX::CommandType::DRAW_COMMANDS : {
            const GLStateTracker& stateTracker = getStateTracker();
            const auto& drawCommands = commandBuffer.get<GFX::DrawCommand>(entry)->_drawCommands;

            U32 drawCount = 0u;

            DIVIDE_ASSERT(drawCount == 0u || stateTracker._activePipeline != nullptr);

            for (const GenericDrawCommand& currentDrawCommand : drawCommands) {
                if (draw(currentDrawCommand)) {
                    drawCount += isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_WIREFRAME) 
                                       ? 2 
                                       : isEnabledOption(currentDrawCommand, CmdRenderOptions::RENDER_GEOMETRY) ? 1 : 0;
                }
            }
            _context.registerDrawCalls(drawCount);
        }break;
        case GFX::CommandType::DISPATCH_COMPUTE: {
            GFX::DispatchComputeCommand* crtCmd = commandBuffer.get<GFX::DispatchComputeCommand>(entry);
            if(getStateTracker()._activePipeline != nullptr) {
                OPTICK_EVENT("GL: Dispatch Compute");
                glDispatchCompute(crtCmd->_computeGroupSize.x, crtCmd->_computeGroupSize.y, crtCmd->_computeGroupSize.z);
            }
        }break;
        case GFX::CommandType::SET_CLIPING_STATE: {
            GFX::SetClippingStateCommand* crtCmd = commandBuffer.get<GFX::SetClippingStateCommand>(entry);
            getStateTracker().setClippingPlaneState(crtCmd->_lowerLeftOrigin, crtCmd->_negativeOneToOneDepth);
        } break;
        case GFX::CommandType::MEMORY_BARRIER: {
            GFX::MemoryBarrierCommand* crtCmd = commandBuffer.get<GFX::MemoryBarrierCommand>(entry);
            MemoryBarrierMask glMask = MemoryBarrierMask::GL_NONE_BIT;
            const U32 barrierMask = crtCmd->_barrierMask;
            if (barrierMask != 0) {
                if (BitCompare(barrierMask, to_base(MemoryBarrierType::TEXTURE_BARRIER))) {
                    glTextureBarrier();
                } 
                if (barrierMask == to_base(MemoryBarrierType::ALL_MEM_BARRIERS)) {
                    glMemoryBarrier(MemoryBarrierMask::GL_ALL_BARRIER_BITS);
                } else {
                    for (U8 i = 0; i < to_U8(MemoryBarrierType::COUNT) + 1; ++i) {
                        if (BitCompare(barrierMask, 1u << i)) {
                            switch (static_cast<MemoryBarrierType>(1 << i)) {
                                case MemoryBarrierType::BUFFER_UPDATE:
                                    glMask |= MemoryBarrierMask::GL_BUFFER_UPDATE_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::SHADER_STORAGE:
                                    glMask |= MemoryBarrierMask::GL_SHADER_STORAGE_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::COMMAND_BUFFER:
                                    glMask |= MemoryBarrierMask::GL_COMMAND_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::ATOMIC_COUNTER:
                                    glMask |= MemoryBarrierMask::GL_ATOMIC_COUNTER_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::QUERY:
                                    glMask |= MemoryBarrierMask::GL_QUERY_BUFFER_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::RENDER_TARGET:
                                    glMask |= MemoryBarrierMask::GL_FRAMEBUFFER_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::TEXTURE_UPDATE:
                                    glMask |= MemoryBarrierMask::GL_TEXTURE_UPDATE_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::TEXTURE_FETCH:
                                    glMask |= MemoryBarrierMask::GL_TEXTURE_FETCH_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::SHADER_IMAGE:
                                    glMask |= MemoryBarrierMask::GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::TRANSFORM_FEEDBACK:
                                    glMask |= MemoryBarrierMask::GL_TRANSFORM_FEEDBACK_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::VERTEX_ATTRIB_ARRAY:
                                    glMask |= MemoryBarrierMask::GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::INDEX_ARRAY:
                                    glMask |= MemoryBarrierMask::GL_ELEMENT_ARRAY_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::UNIFORM_DATA:
                                    glMask |= MemoryBarrierMask::GL_UNIFORM_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::PIXEL_BUFFER:
                                    glMask |= MemoryBarrierMask::GL_PIXEL_BUFFER_BARRIER_BIT;
                                    break;
                                case MemoryBarrierType::PERSISTENT_BUFFER:
                                    glMask |= MemoryBarrierMask::GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT;
                                    break;
                                default:
                                    NOP();
                                    break;
                            }
                        }
                    }
                   glMemoryBarrier(glMask);
               }
            }
        } break;
        default: break;
    }

    switch (cmdType) {
        case GFX::CommandType::EXTERNAL:
        case GFX::CommandType::DRAW_TEXT:
        case GFX::CommandType::DRAW_IMGUI:
        case GFX::CommandType::DRAW_COMMANDS:
        case GFX::CommandType::DISPATCH_COMPUTE:
        {
            OPTICK_EVENT();
            const U32 frameIndex = _context.frameCount();
            for (BufferLockEntry& lockEntry : s_bufferLockQueueMidFlush) {
                if (!lockEntry._buffer->lockByteRange(lockEntry._offset, lockEntry._length, frameIndex)) {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
            s_bufferLockQueueMidFlush.resize(0);
        } break;
    }
}

void GL_API::RegisterBufferBind(BufferLockEntry&& data, const bool fenceAfterFirstDraw) {
    assert(Runtime::isMainThread());
    if (fenceAfterFirstDraw) {
        s_bufferLockQueueMidFlush.push_back(data);
    } else {
        s_bufferLockQueueEndOfBuffer.push_back(data);
    }
}


void GL_API::postFlushCommandBuffer(const GFX::CommandBuffer& commandBuffer) {
    ACKNOWLEDGE_UNUSED(commandBuffer);

    OPTICK_EVENT();
    const U32 frameIndex = _context.frameCount();
    for (BufferLockEntry& lockEntry : s_bufferLockQueueEndOfBuffer) {
        if (!lockEntry._buffer->lockByteRange(lockEntry._offset, lockEntry._length, frameIndex)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    s_bufferLockQueueEndOfBuffer.resize(0);

    bool expected = true;
    if (s_glFlushQueued.compare_exchange_strong(expected, false)) {
        OPTICK_EVENT("GL_FLUSH");
        glFlush();
    }

    getStateTracker()._flushingCommandBuffer = false;
}

GenericVertexData* GL_API::getOrCreateIMGUIBuffer(const I64 windowGUID) {
    const auto it = _IMGUIBuffers.find(windowGUID);
    if (it != cend(_IMGUIBuffers)) {
        return it->second;
    }

    // Ring buffer wouldn't work properly with an IMMEDIATE MODE gui
    // We update and draw multiple times in a loop
    GenericVertexData* ret = _context.newGVD(1);

    GenericVertexData::IndexBuffer idxBuff;
    idxBuff.smallIndices = sizeof(ImDrawIdx) == 2;
    idxBuff.count = (1 << 16) * 3;

    ret->create(1);
    ret->renderIndirect(false);

    GenericVertexData::SetBufferParams params = {};
    params._buffer = 0;
    params._useRingBuffer = false;

    params._bufferParams._elementCount = 1 << 16;
    params._bufferParams._elementSize = sizeof(ImDrawVert);
    params._bufferParams._usePersistentMapping = false;
    params._bufferParams._updateFrequency = BufferUpdateFrequency::OFTEN;
    params._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
    params._bufferParams._sync = false; // We use manual waiting and locking
    params._bufferParams._initialData = { nullptr, 0 };

    ret->setBuffer(params); //Pos, UV and Colour
    ret->setIndexBuffer(idxBuff, BufferUpdateFrequency::OFTEN);

    AttributeDescriptor& descPos = ret->attribDescriptor(to_base(AttribLocation::GENERIC));
    AttributeDescriptor& descUV = ret->attribDescriptor(to_base(AttribLocation::TEXCOORD));
    AttributeDescriptor& descColour = ret->attribDescriptor(to_base(AttribLocation::COLOR));

#   define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    descPos.set(   0, 2, GFXDataFormat::FLOAT_32,      false, to_U32(OFFSETOF(ImDrawVert, pos)));
    descUV.set(    0, 2, GFXDataFormat::FLOAT_32,      false, to_U32(OFFSETOF(ImDrawVert, uv)));
    descColour.set(0, 4, GFXDataFormat::UNSIGNED_BYTE, true,  to_U32(OFFSETOF(ImDrawVert, col)));
#   undef OFFSETOF

    _IMGUIBuffers[windowGUID] = ret;

    return ret;
}

bool GL_API::makeImagesResident(const Images& images) const {
    OPTICK_EVENT();

    for (const Image& image : images._entries) {
        if (image._texture != nullptr) {
            image._texture->bindLayer(image._binding, image._level, image._layer, false, image._flag );
        }
    }

    return true;
}

bool GL_API::makeTexturesResidentInternal(TextureDataContainer& textureData, const U8 offset, U8 count) const {
    // All of the complicate and fragile code bellow does actually provide a measurable performance increase 
    // (micro second range for a typical scene, nothing amazing, but still ...)
    // CPU cost is comparable to the multiple glBind calls on some specific driver + GPU combos.

    constexpr GLuint k_textureThreshold = 3;
    GLStateTracker& stateTracker = getStateTracker();

    const size_t totalTextureCount = textureData.count();

    count = std::min(count, to_U8(totalTextureCount - offset));
    assert(offset + count <= totalTextureCount);
    const auto& textures = textureData._entries;

    bool bound = false;
    if (count > 1) {
        // If we have 3 or more textures, there's a chance we might get a binding gap, so just sort
        if (totalTextureCount > 2) {
            textureData.sortByBinding();
        }

        U8 prevBinding = textures.front()._binding;
        const TextureType targetType = textures.front()._data._textureType;

        U8 matchingTexCount = 0u;
        U8 startBinding = U8_MAX;
        U8 endBinding = 0u; 

        for (U8 idx = offset; idx < offset + count; ++idx) {
            const TextureEntry& entry = textures[idx];
            assert(IsValid(entry._data));
            if (entry._binding != INVALID_TEXTURE_BINDING && targetType != entry._data._textureType) {
                break;
            }
            // Avoid large gaps between bindings. It's faster to just bind them individually.
            if (matchingTexCount > 0 && entry._binding - prevBinding > k_textureThreshold) {
                break;
            }
            // We mainly want to handle ONLY consecutive units
            prevBinding = entry._binding;
            startBinding = std::min(startBinding, entry._binding);
            endBinding = std::max(endBinding, entry._binding);
            ++matchingTexCount;
        }

        if (matchingTexCount >= k_textureThreshold) {
            static vectorEASTL<GLuint> handles{};
            static vectorEASTL<GLuint> samplers{};
            static bool init = false;
            if (!init) {
                init = true;
                handles.resize(s_maxTextureUnits, GLUtil::k_invalidObjectID);
                samplers.resize(s_maxTextureUnits, GLUtil::k_invalidObjectID);
            } else {
                std::memset(&handles[startBinding], GLUtil::k_invalidObjectID, (endBinding - startBinding + 1) * sizeof(GLuint));
            }

            for (U8 idx = offset; idx < offset + matchingTexCount; ++idx) {
                const TextureEntry& entry = textures[idx];
                if (entry._binding != INVALID_TEXTURE_BINDING) {
                    handles[entry._binding]  = entry._data._textureHandle;
                    samplers[entry._binding] = GetSamplerHandle(entry._sampler);
                }
            }

            
            for (U8 binding = startBinding; binding < endBinding; ++binding) {
                if (handles[binding] == GLUtil::k_invalidObjectID) {
                    const TextureType crtType = stateTracker.getBoundTextureType(binding);
                    samplers[binding] = stateTracker.getBoundSamplerHandle(binding);
                    handles[binding] = stateTracker.getBoundTextureHandle(binding, crtType);
                }
            }

            bound = stateTracker.bindTextures(startBinding, endBinding - startBinding + 1, targetType, &handles[startBinding], &samplers[startBinding]);
        } else {
            matchingTexCount = 1;
            bound = makeTexturesResidentInternal(textureData, offset, 1) || bound;
        }

        // Recurse to try and get more matches
        bound = makeTexturesResidentInternal(textureData, offset + matchingTexCount, count - matchingTexCount) || bound;
    } else if (count == 1) {
        // Normal usage. Bind a single texture at a time
        const TextureEntry& entry = textures[offset];
        if (entry._binding != INVALID_TEXTURE_BINDING) {
            assert(IsValid(entry._data));
            GLuint handle = entry._data._textureHandle;
            GLuint sampler = GetSamplerHandle(entry._sampler);
            bound = stateTracker.bindTextures(entry._binding, 1, entry._data._textureType, &handle, &sampler) || bound;
        }
    } else {
        // Used for breaking the debugger if we get missing textures or similar issues
        NOP();
    }

    return bound;
}

bool GL_API::makeTextureViewsResidentInternal(const TextureViews& textureViews, const U8 offset, U8 count) const {
    count = std::min(count, to_U8(textureViews.count()));

    bool bound = false;
    for (U8 i = offset; i < count + offset; ++i) {
        const auto& it = textureViews._entries[i];
        const size_t viewHash = it.getHash();
        TextureView view = it._view;
        if (view._targetType == TextureType::COUNT) {
            view._targetType = view._textureData._textureType;
        }
        const TextureData& data = view._textureData;
        assert(IsValid(data));

        auto [textureID, cacheHit] = s_textureViewCache.allocate(viewHash);
        DIVIDE_ASSERT(textureID != 0u);

        if (!cacheHit)
        {
            const GLenum glInternalFormat = GLUtil::internalFormat(it._descriptor.baseFormat(), it._descriptor.dataType(), it._descriptor.srgb(), it._descriptor.normalized());

            if (IsCubeTexture(view._targetType)) {
                view._layerRange *= 6;
            }

            glTextureView(textureID,
                GLUtil::glTextureTypeTable[to_base(view._targetType)],
                data._textureHandle,
                glInternalFormat,
                static_cast<GLuint>(it._view._mipLevels.x),
                static_cast<GLuint>(it._view._mipLevels.y),
                view._layerRange.min,
                view._layerRange.max);
        }

        getStateTracker().ProcessMipMapQueue(1, &data._textureHandle);

        GLuint samplerHandle = GetSamplerHandle(it._view._samplerHash);
        bound = getStateTracker().bindTexturesNoMipMap(static_cast<GLushort>(it._binding), 1, view._targetType, &textureID, &samplerHandle) || bound;
        // Self delete after 3 frames unless we use it again
        s_textureViewCache.deallocate(textureID, 3u);
    }

    return bound;
}

bool GL_API::MakeTexturesResidentInternal(const SamplerAddress address) {
    if (address > 0u) {
        bool valid = false;
        // Check for existing resident textures
        for (ResidentTexture& texture : s_residentTextures) {
            if (texture._address == address) {
                texture._frameCount = 0u;
                valid = true;
                break;
            }
        }

        if (!valid) {
            // Register a new resident texture
            for (ResidentTexture& texture : s_residentTextures) {
                if (texture._address == 0u) {
                    texture._address = address;
                    texture._frameCount = 0u;
                    glMakeTextureHandleResidentARB(address);
                    valid = true;
                    break;
                }
            }
        }

        return valid;
    }

    return true;
}

bool GL_API::MakeTexturesNonResidentInternal(const SamplerAddress address) {
    if (address > 0u) {
        for (ResidentTexture& texture : s_residentTextures) {
            if (texture._address == address) {
                texture._address = 0u;
                texture._frameCount = 0u;
                glMakeTextureHandleNonResidentARB(address);
                return true;
            }
        }
        return false;
    }

    return true;
}

bool GL_API::makeTexturesResident(TextureDataContainer& textureData, const TextureViews& textureViews, const U8 offset, const U8 count) const {
    OPTICK_EVENT();

    bool bound = makeTextureViewsResidentInternal(textureViews, offset, count);
    bound = makeTexturesResidentInternal(textureData, offset, count) || bound;
    return bound;
}

bool GL_API::setViewport(const Rect<I32>& viewport) {
    OPTICK_EVENT();

    return getStateTracker().setViewport(viewport);
}

/// Return the OpenGL sampler object's handle for the given hash value
GLuint GL_API::GetSamplerHandle(const size_t samplerHash) {
    // If the hash value is 0, we assume the code is trying to unbind a sampler object
    if (samplerHash > 0) {
        {
            SharedLock<SharedMutex> r_lock(s_samplerMapLock);
            // If we fail to find the sampler object for the given hash, we print an error and return the default OpenGL handle
            const SamplerObjectMap::const_iterator it = s_samplerMap.find(samplerHash);
            if (it != std::cend(s_samplerMap)) {
                // Return the OpenGL handle for the sampler object matching the specified hash value
                return it->second;
            }
        }
        {

            UniqueLock<SharedMutex> w_lock(s_samplerMapLock);
            // Check again
            const SamplerObjectMap::const_iterator it = s_samplerMap.find(samplerHash);
            if (it == std::cend(s_samplerMap)) {
                //Cache miss. Create the sampler object now.
                // Create and store the newly created sample object. GL_API is responsible for deleting these!
                const GLuint sampler = glSamplerObject::construct(SamplerDescriptor::get(samplerHash));
                emplace(s_samplerMap, samplerHash, sampler);
                return sampler;
            }
        }
    }

    return 0u;
}

U32 GL_API::getHandleFromCEGUITexture(const CEGUI::Texture& textureIn) const {
    return to_U32(static_cast<const CEGUI::OpenGLTexture&>(textureIn).getOpenGLTexture());
}

IMPrimitive* GL_API::NewIMP(Mutex& lock, GFXDevice& parent) {
    UniqueLock<Mutex> w_lock(lock);
    return s_IMPrimitivePool.newElement(parent);
}

bool GL_API::DestroyIMP(Mutex& lock, IMPrimitive*& primitive) {
    if (primitive != nullptr) {
        UniqueLock<Mutex> w_lock(lock);
        s_IMPrimitivePool.deleteElement(static_cast<glIMPrimitive*>(primitive));
        primitive = nullptr;
        return true;
    }

    return false;
}

};
