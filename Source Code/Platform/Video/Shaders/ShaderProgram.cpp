#include "stdafx.h"

#include "Headers/ShaderProgram.h"

#include "Managers/Headers/SceneManager.h"

#include "Rendering/Headers/Renderer.h"
#include "Geometry/Material/Headers/Material.h"
#include "Scenes/Headers/SceneShaderData.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/glsw/Headers/glsw.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/FileUpdateMonitor.h"
#include "Platform/File/Headers/FileWatcherManager.h"

extern "C" {
#include "fcpp/fpp.h"
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4458)
#pragma warning(disable:4706)
#endif
#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp> // lexer class
#include <boost/wave/cpplexer/cpp_lex_token.hpp>    // token class
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Divide {

constexpr I8 s_maxHeaderRecursionLevel = 64;

moodycamel::BlockingConcurrentQueue<ShaderProgram::TextDumpEntry> g_sDumpToFileQueue;

bool ShaderProgram::s_useShaderTextCache = false;
bool ShaderProgram::s_useShaderBinaryCache = false;
bool ShaderProgram::s_UseBindlessTextures = false;

SharedMutex ShaderProgram::s_atomLock;
ShaderProgram::AtomMap ShaderProgram::s_atoms;
ShaderProgram::AtomInclusionMap ShaderProgram::s_atomIncludes;

I64 ShaderProgram::s_shaderFileWatcherID = -1;
ResourcePath ShaderProgram::shaderAtomLocationPrefix[to_base(ShaderType::COUNT) + 1];
U64 ShaderProgram::shaderAtomExtensionHash[to_base(ShaderType::COUNT) + 1];
Str8 ShaderProgram::shaderAtomExtensionName[to_base(ShaderType::COUNT) + 1];

ShaderProgram_ptr ShaderProgram::s_imShader = nullptr;
ShaderProgram_ptr ShaderProgram::s_imWorldShader = nullptr;
ShaderProgram_ptr ShaderProgram::s_imWorldOITShader = nullptr;
ShaderProgram_ptr ShaderProgram::s_nullShader = nullptr;
ShaderProgram::ShaderQueue ShaderProgram::s_recompileQueue;
ShaderProgram::ShaderProgramMap ShaderProgram::s_shaderPrograms;
std::pair<I64, ShaderProgram::ShaderProgramMapEntry> ShaderProgram::s_lastRequestedShaderProgram = { -1, {} };

SharedMutex ShaderProgram::s_programLock;
std::atomic_int ShaderProgram::s_shaderCount;

size_t ShaderProgramDescriptor::getHash() const {
    _hash = PropertyDescriptor::getHash();
    for (const ShaderModuleDescriptor& desc : _modules) {
        Util::Hash_combine(_hash, ShaderProgram::DefinesHash(desc._defines));
        Util::Hash_combine(_hash, std::string(desc._variant.c_str()));
        Util::Hash_combine(_hash, desc._sourceFile.data());
        Util::Hash_combine(_hash, desc._moduleType);
        Util::Hash_combine(_hash, desc._batchSameFile);
    }
    return _hash;
}

UpdateListener g_sFileWatcherListener(
    [](const std::string_view atomName, const FileUpdateEvent evt) {
        ShaderProgram::OnAtomChange(atomName, evt);
    }
);

namespace Preprocessor{
     //ref: https://stackoverflow.com/questions/14858017/using-boost-wave
    class custom_directives_hooks final : public boost::wave::context_policies::default_preprocessing_hooks
    {
    public:
        template <typename ContextT, typename ContainerT>
        bool found_unknown_directive(ContextT const& /*ctx*/, ContainerT const& line, ContainerT& pending) {
            auto itBegin = cbegin(line);
            const auto itEnd = cend(line);

            const boost::wave::token_id ret = boost::wave::util::impl::skip_whitespace(itBegin, itEnd);

            if (ret == boost::wave::T_IDENTIFIER) {
                const auto& temp = (*itBegin).get_value();
                if (temp == "version" || temp == "extension") {
                    // handle #version and #extension directives
                    copy(cbegin(line), itEnd, back_inserter(pending));
                    return true;
                }
            }

            return false;  // unknown directive
        }
    };

    struct WorkData
    {
        const char* _input = nullptr;
        size_t _inputSize = 0u;
        const char* _fileName = nullptr;

        std::array<char, 16 << 10> _scratch{};
        eastl::string _depends = "";
        eastl::string _default = "";
        eastl::string _output = "";

        U32 _scratchPos = 0u;
        U32 _fGetsPos = 0u;
        bool _firstError = true;
    };

     namespace Callback {
         FORCE_INLINE void AddDependency(const char* file, void* userData) {
            eastl::string& depends = static_cast<WorkData*>(userData)->_depends;

            depends += " \\\n ";
            depends += file;
        }

        char* Input(char* buffer, const int size, void* userData) noexcept {
            WorkData* work = static_cast<WorkData*>(userData);
            int i = 0;
            for (char ch = work->_input[work->_fGetsPos];
                work->_fGetsPos < work->_inputSize && i < size - 1; ch = work->_input[++work->_fGetsPos]) {
                buffer[i++] = ch;

                if (ch == '\n' || i == size) {
                    buffer[i] = '\0';
                    work->_fGetsPos++;
                    return buffer;
                }
            }

            return nullptr;
        }

        FORCE_INLINE void Output(const int ch, void* userData) {
            static_cast<WorkData*>(userData)->_output += static_cast<char>(ch);
        }

        char* Scratch(const char* str, WorkData& workData) {
            char* result = &workData._scratch[workData._scratchPos];
            strcpy(result, str);
            workData._scratchPos += to_U32(strlen(str)) + 1;
            return result;
        }

        void Error(void* userData, const char* format, const va_list args) {
            static bool firstErrorPrint = true;
            WorkData* work = static_cast<WorkData*>(userData);
            char formatted[1024];
            vsnprintf(formatted, 1024, format, args);
            if (work->_firstError) {
                work->_firstError = false;
                Console::errorfn("------------------------------------------");
                Console::errorfn(Locale::Get(firstErrorPrint ? _ID("ERROR_GLSL_PARSE_ERROR_NAME_LONG")
                                                             : _ID("ERROR_GLSL_PARSE_ERROR_NAME_SHORT")), work->_fileName);
                firstErrorPrint = false;
            }
            if (strlen(formatted) != 1 && formatted[0] != '\n') {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_PARSE_ERROR_MSG")), formatted);
            } else {
                Console::errorfn("------------------------------------------\n");
            }
        }
    }
    
     eastl::string PreProcessBoost(const eastl::string& source, const char* fileName) {
         eastl::string ret = {};

         // Fallback to slow Boost.Wave parsing
         using ContextType = boost::wave::context<eastl::string::const_iterator,
                             boost::wave::cpplexer::lex_iterator<boost::wave::cpplexer::lex_token<>>,
                             boost::wave::iteration_context_policies::load_file_to_string,
                             custom_directives_hooks>;

         ContextType ctx(cbegin(source), cend(source), fileName);

         ctx.set_language(enable_long_long(ctx.get_language()));
         ctx.set_language(enable_preserve_comments(ctx.get_language()));
         ctx.set_language(enable_prefer_pp_numbers(ctx.get_language()));
         ctx.set_language(enable_emit_line_directives(ctx.get_language(), false));

         for (const auto& it : ctx) {
             ret.append(it.get_value().c_str());
         }

         return ret;
     }

    eastl::string PreProcess(const eastl::string& source, const char* fileName) {
        constexpr U8 g_maxTagCount = 64;

        if (source.empty()) {
            return source;
        }

        eastl::string temp(source.size() + 1, ' ');
        {
            const char* in  = source.data();
                  char* out = temp.data();
            const char* end = out + source.size();

            for (char ch = *in++; out < end && ch != '\0'; ch = *in++) {
                if (ch != '\r') {
                    *out++ = ch;
                }
            }
            *out = '\0';
        }

        WorkData workData{
            temp.c_str(), // input
            temp.size(),  // input size
            fileName      // file name
        };

        fppTag tags[g_maxTagCount]{};
        fppTag* tagHead = tags;
  
        const auto setTag = [&tagHead](const int tag, void* value) {
            tagHead->tag = tag;
            tagHead->data = value;
            ++tagHead;
        };

        setTag(FPPTAG_USERDATA,           &workData);
        setTag(FPPTAG_DEPENDS,            Callback::AddDependency);
        setTag(FPPTAG_INPUT,              Callback::Input);
        setTag(FPPTAG_OUTPUT,             Callback::Output);
        setTag(FPPTAG_ERROR,              Callback::Error);
        setTag(FPPTAG_INPUT_NAME,         Callback::Scratch(fileName, workData));
        setTag(FPPTAG_KEEPCOMMENTS,       (void*)TRUE);
        setTag(FPPTAG_IGNOREVERSION,      (void*)FALSE);
        setTag(FPPTAG_LINE,               (void*)FALSE);
        setTag(FPPTAG_OUTPUTBALANCE,      (void*)TRUE);
        setTag(FPPTAG_OUTPUTSPACE,        (void*)TRUE);
        setTag(FPPTAG_NESTED_COMMENTS,    (void*)TRUE);
        //setTag(FPPTAG_IGNORE_CPLUSPLUS, (void*)TRUE);
        setTag(FPPTAG_RIGHTCONCAT,        (void*)TRUE);
        //setTag(FPPTAG_WARNILLEGALCPP,   (void*)TRUE);
        setTag(FPPTAG_END,                nullptr);

        if (fppPreProcess(tags) != 0) {
            return PreProcessBoost(source, fileName);
        }

        return workData._output;
    }

} //Preprocessor

void AppendToShaderHeader(const ShaderType type, const string& entry) {
    glswAddDirectiveToken(type != ShaderType::COUNT ? Names::shaderTypes[to_U8(type)] : "", entry.c_str());
}

bool InitGLSW(const DeviceInformation& deviceInfo, const Configuration& config) {
    constexpr std::pair<const char*, const char*> shaderVaryings[] =
    {
        { "vec4"       , "_vertexW"},          // 16 bytes
        { "vec4"       , "_vertexWV"},         // 32 bytes
        { "vec4"       , "_prevVertexWVP"},    // 48 bytes
        { "vec3"       , "_normalWV"},         // 60 bytes
        { "vec3"       , "_viewDirectionWV"},  // 72 bytes
        { "vec2"       , "_texCoord"},         // 80 bytes
        { "flat uvec4" , "_indirectionIDs"},   // 96 bytes
        { "flat uint"  , "_LoDLevel"},         // 100 bytes
        //{ "mat3" , "_tbnWV"},                // 136 bytes
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

    const auto getPassData = [&](const ShaderType type) -> string {
        string baseString = "     _out.%s = _in[index].%s;";
        if (type == ShaderType::TESSELLATION_CTRL) {
            baseString = "    _out[gl_InvocationID].%s = _in[index].%s;";
        }

        string passData("void PassData(in int index) {");
        passData.append("\n");
        for (const auto& [varType, name] : shaderVaryings) {
            passData.append(Util::StringFormat(baseString.c_str(), name, name));
            passData.append("\n");
        }

        passData.append("#if defined(ENABLE_TBN)\n");
        passData.append(Util::StringFormat(baseString.c_str(), "_tbnWV", "_tbnWV"));
        passData.append("\n#endif //ENABLE_TBN\n");

        passData.append("}\n");

        return passData;
    };

    const auto addVaryings = [&](const ShaderType type) {
        for (const auto& [varType, name] : shaderVaryings) {
            AppendToShaderHeader(type, Util::StringFormat("    %s %s;", varType, name));
        }
        AppendToShaderHeader(type, "#if defined(ENABLE_TBN)");
        AppendToShaderHeader(type, "    mat3 _tbnWV;");
        AppendToShaderHeader(type, "#endif //ENABLE_TBN");
    };

    // Initialize GLSW
    I32 glswState = -1;
    if (!glswGetCurrentContext()) {
        glswState = glswInit();
        DIVIDE_ASSERT(glswState == 1);
    }

    const U16 reflectionProbeRes = to_U16(nextPOW2(CLAMPED(to_U32(config.rendering.reflectionProbeResolution), 16u, 4096u) - 1u));

    static_assert(Config::MAX_BONE_COUNT_PER_NODE <= 1024, "ShaderProgram error: too many bones per vert. Can't fit inside UBO");

    // Add our engine specific defines and various code pieces to every GLSL shader
    // Add version as the first shader statement, followed by copyright notice
    AppendToShaderHeader(ShaderType::COUNT, "#version 460 core");
    AppendToShaderHeader(ShaderType::COUNT, "/*Copyright 2009-2022 DIVIDE-Studio*/");

    if (ShaderProgram::s_UseBindlessTextures) {
        AppendToShaderHeader(ShaderType::COUNT, "#extension  GL_ARB_bindless_texture : require");
    }

    AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_gpu_shader5 : require");
    AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_DRAW_ID gl_DrawID");
    AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_VERTEX gl_BaseVertex");
    AppendToShaderHeader(ShaderType::COUNT, "#define DVD_GL_BASE_INSTANCE gl_BaseInstance");
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
    AppendToShaderHeader(ShaderType::VERTEX, "#define VERT_SHADER");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define FRAG_SHADER");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define GEOM_SHADER");
    AppendToShaderHeader(ShaderType::COMPUTE, "#define COMPUTE_SHADER");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define TESS_EVAL_SHADER");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define TESS_CTRL_SHADER");

    // This line gets replaced in every shader at load with the custom list of defines specified by the material
    AppendToShaderHeader(ShaderType::COUNT, "_CUSTOM_DEFINES__");

    // Add some nVidia specific pragma directives
    if (GFXDevice::GetDeviceInformation()._vendor == GPUVendor::NVIDIA) {
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(fastmath on)");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(fastprecision on)");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(inline all)");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(ifcvt none)");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(strict on)");
        AppendToShaderHeader(ShaderType::COUNT, "//#pragma optionNV(unroll all)");
    }

    if_constexpr(Config::USE_COLOURED_WOIT) {
        AppendToShaderHeader(ShaderType::COUNT, "#define USE_COLOURED_WOIT");
    }

    if (ShaderProgram::s_UseBindlessTextures) {
        AppendToShaderHeader(ShaderType::COUNT, "#define BINDLESS_TEXTURES_ENABLED");
    }

    // ToDo: Automate adding of buffer bindings by using, for example, a TypeUtil::bufferBindingToString -Ionut
    AppendToShaderHeader(ShaderType::COUNT, "#define ALPHA_DISCARD_THRESHOLD " + Util::to_string(Config::ALPHA_DISCARD_THRESHOLD) + "f");
    AppendToShaderHeader(ShaderType::COUNT, "#define Z_TEST_SIGMA " + Util::to_string(Config::Z_TEST_SIGMA) + "f");
    AppendToShaderHeader(ShaderType::COUNT, "#define INV_Z_TEST_SIGMA " + Util::to_string(1.f - Config::Z_TEST_SIGMA) + "f");
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_CSM_SPLITS_PER_LIGHT " + Util::to_string(Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_SHADOW_CASTING_LIGHTS " + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_SHADOW_CASTING_DIR_LIGHTS " + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_SHADOW_CASTING_POINT_LIGHTS " + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_SHADOW_CASTING_SPOT_LIGHTS " + Util::to_string(Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_LIGHTS " + Util::to_string(Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_VISIBLE_NODES " + Util::to_string(Config::MAX_VISIBLE_NODES));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_CONCURRENT_MATERIALS " + Util::to_string(Config::MAX_CONCURRENT_MATERIALS));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_CLIP_PLANES " + Util::to_string(Config::MAX_CLIP_DISTANCES));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_CULL_DISTANCES " + Util::to_string(Config::MAX_CULL_DISTANCES));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_ACCUMULATION " + Util::to_string(to_base(GFXDevice::ScreenTargets::ACCUMULATION)));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_ALBEDO " + Util::to_string(to_base(GFXDevice::ScreenTargets::ALBEDO)));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_VELOCITY " + Util::to_string(to_base(GFXDevice::ScreenTargets::VELOCITY)));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_NORMALS " + Util::to_string(to_base(GFXDevice::ScreenTargets::NORMALS)));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_REVEALAGE " + Util::to_string(to_base(GFXDevice::ScreenTargets::REVEALAGE)));
    AppendToShaderHeader(ShaderType::COUNT, "#define TARGET_MODULATE " + Util::to_string(to_base(GFXDevice::ScreenTargets::MODULATE)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_CAM_BLOCK " + Util::to_string(to_base(ShaderBufferLocation::CAM_BLOCK)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_RENDER_BLOCK " + Util::to_string(to_base(ShaderBufferLocation::RENDER_BLOCK)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_ATOMIC_COUNTER_0 " + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER_0)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_ATOMIC_COUNTER_1 " + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER_1)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_ATOMIC_COUNTER_2 " + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER_2)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_ATOMIC_COUNTER_3 " + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER_3)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_ATOMIC_COUNTER_4 " + Util::to_string(to_base(ShaderBufferLocation::ATOMIC_COUNTER_4)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_GPU_COMMANDS " + Util::to_string(to_base(ShaderBufferLocation::GPU_COMMANDS)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_NORMAL " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_NORMAL)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_SCENE " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_SCENE)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_SHADOW " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_SHADOW)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_INDICES " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_INDICES)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_GRID " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_GRID)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_INDEX_COUNT " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_INDEX_COUNT)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_LIGHT_CLUSTER_AABBS " + Util::to_string(to_base(ShaderBufferLocation::LIGHT_CLUSTER_AABBS)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_NODE_TRANSFORM_DATA " + Util::to_string(to_base(ShaderBufferLocation::NODE_TRANSFORM_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_NODE_TEXTURE_DATA " + Util::to_string(to_base(ShaderBufferLocation::NODE_TEXTURE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_NODE_MATERIAL_DATA " + Util::to_string(to_base(ShaderBufferLocation::NODE_MATERIAL_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_NODE_INDIRECTION_DATA " + Util::to_string(to_base(ShaderBufferLocation::NODE_INDIRECTION_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_SCENE_DATA " + Util::to_string(to_base(ShaderBufferLocation::SCENE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_PROBE_DATA " + Util::to_string(to_base(ShaderBufferLocation::PROBE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_COMMANDS " + Util::to_string(to_base(ShaderBufferLocation::CMD_BUFFER)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_GRASS_DATA " + Util::to_string(to_base(ShaderBufferLocation::GRASS_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_TREE_DATA " + Util::to_string(to_base(ShaderBufferLocation::TREE_DATA)));
    AppendToShaderHeader(ShaderType::COUNT, "#define BUFFER_UNIFORM_BLOCK " + Util::to_string(to_base(ShaderBufferLocation::UNIFORM_BLOCK)));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_X_THREADS " + Util::to_string(Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_Y_THREADS " + Util::to_string(Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_Z_THREADS " + Util::to_string(Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_X " + Util::to_string(Renderer::CLUSTER_SIZE.x));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_Y " + Util::to_string(Renderer::CLUSTER_SIZE.y));
    AppendToShaderHeader(ShaderType::COUNT, "#define CLUSTERS_Z " + Util::to_string(Renderer::CLUSTER_SIZE.z));
    AppendToShaderHeader(ShaderType::COUNT, "#define SKY_LIGHT_LAYER_IDX " + Util::to_string(SceneEnvironmentProbePool::SkyProbeLayerIndex()));
    AppendToShaderHeader(ShaderType::COUNT, "#define MAX_LIGHTS_PER_CLUSTER " + Util::to_string(config.rendering.numLightsPerCluster));
    AppendToShaderHeader(ShaderType::COUNT, "#define REFLECTION_PROBE_RESOLUTION " + Util::to_string(reflectionProbeRes));
    AppendToShaderHeader(ShaderType::COUNT, "#define REFLECTION_PROBE_MIP_COUNT " + Util::to_string(to_U32(std::log2(reflectionProbeRes))));
    for (U8 i = 0; i < to_base(TextureUsage::COUNT); ++i) {
        AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define TEXTURE_%s %d", TypeUtil::TextureUsageToString(static_cast<TextureUsage>(i)), i).c_str());
    }
    for (U8 i = 0; i < to_base(TextureOperation::COUNT); ++i) {
        AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define TEX_%s %d", TypeUtil::TextureOperationToString(static_cast<TextureOperation>(i)), i).c_str());
    }
    AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define WORLD_X_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_X_AXIS.x, WORLD_X_AXIS.y, WORLD_X_AXIS.z));
    AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define WORLD_Y_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_Y_AXIS.x, WORLD_Y_AXIS.y, WORLD_Y_AXIS.z));
    AppendToShaderHeader(ShaderType::COUNT, Util::StringFormat("#define WORLD_Z_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_Z_AXIS.x, WORLD_Z_AXIS.y, WORLD_Z_AXIS.z));


    AppendToShaderHeader(ShaderType::COUNT, "#define M_EPSILON 1e-5f");
    AppendToShaderHeader(ShaderType::COUNT, "#define M_PI 3.14159265358979323846");
    AppendToShaderHeader(ShaderType::COUNT, "#define M_PI_2 (3.14159265358979323846 / 2)");
    AppendToShaderHeader(ShaderType::COUNT, "#define INV_M_PI 0.31830988618");

    AppendToShaderHeader(ShaderType::COUNT, "#define ACCESS_RW");
    AppendToShaderHeader(ShaderType::COUNT, "#define ACCESS_R readonly");
    AppendToShaderHeader(ShaderType::COUNT, "#define ACCESS_W writeonly");

    AppendToShaderHeader(ShaderType::VERTEX, "#define COMP_ONLY_W readonly");
    AppendToShaderHeader(ShaderType::VERTEX, "#define COMP_ONLY_R");
    AppendToShaderHeader(ShaderType::VERTEX, "#define COMP_ONLY_RW readonly");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_W readonly");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_R");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_RW readonly");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_W readonly");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_R");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_RW readonly");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define COMP_ONLY_W readonly");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define COMP_ONLY_R");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define COMP_ONLY_RW readonly");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define COMP_ONLY_W readonly");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define COMP_ONLY_R");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define COMP_ONLY_RW readonly");
    AppendToShaderHeader(ShaderType::COMPUTE, "#define COMP_ONLY_W ACCESS_W");
    AppendToShaderHeader(ShaderType::COMPUTE, "#define COMP_ONLY_R ACCESS_R");
    AppendToShaderHeader(ShaderType::COMPUTE, "#define COMP_ONLY_RW ACCESS_RW");

    AppendToShaderHeader(ShaderType::COUNT, "#define AND(a, b) (a * b)");
    AppendToShaderHeader(ShaderType::COUNT, "#define OR(a, b) min(a + b, 1.f)");
    AppendToShaderHeader(ShaderType::COUNT, "#define XOR(a, b) ((a + b) % 2)");
    AppendToShaderHeader(ShaderType::COUNT, "#define NOT(X) (1.f - X)");
    AppendToShaderHeader(ShaderType::COUNT, "#define Squared(X) (X * X)");
    AppendToShaderHeader(ShaderType::COUNT, "#define Round(X) floor((X) + .5f)");
    AppendToShaderHeader(ShaderType::COUNT, "#define Saturate(X) clamp(X, 0, 1)");
    AppendToShaderHeader(ShaderType::COUNT, "#define Mad(a, b, c) (a * b + c)");

    AppendToShaderHeader(ShaderType::COUNT, "#define GLOBAL_WATER_BODIES_COUNT " + Util::to_string(GLOBAL_WATER_BODIES_COUNT));
    AppendToShaderHeader(ShaderType::COUNT, "#define GLOBAL_PROBE_COUNT " + Util::to_string(GLOBAL_PROBE_COUNT));
    AppendToShaderHeader(ShaderType::COUNT, "#define MATERIAL_TEXTURE_COUNT " + Util::to_string(MATERIAL_TEXTURE_COUNT));
    AppendToShaderHeader(ShaderType::COMPUTE, "#define BUFFER_LUMINANCE_HISTOGRAM " + Util::to_string(to_base(ShaderBufferLocation::LUMINANCE_HISTOGRAM)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define BUFFER_BONE_TRANSFORMS " + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define BUFFER_BONE_TRANSFORMS_PREV " + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS_PREV)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define MAX_BONE_COUNT_PER_NODE " + Util::to_string(Config::MAX_BONE_COUNT_PER_NODE));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_POSITION " + Util::to_string(to_base(AttribLocation::POSITION)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_TEXCOORD " + Util::to_string(to_base(AttribLocation::TEXCOORD)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_NORMAL " + Util::to_string(to_base(AttribLocation::NORMAL)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_TANGENT " + Util::to_string(to_base(AttribLocation::TANGENT)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_COLOR " + Util::to_string(to_base(AttribLocation::COLOR)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_BONE_WEIGHT " + Util::to_string(to_base(AttribLocation::BONE_WEIGHT)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_BONE_INDICE " + Util::to_string(to_base(AttribLocation::BONE_INDICE)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_WIDTH " + Util::to_string(to_base(AttribLocation::WIDTH)));
    AppendToShaderHeader(ShaderType::VERTEX, "#define ATTRIB_GENERIC " + Util::to_string(to_base(AttribLocation::GENERIC)));
    AppendToShaderHeader(ShaderType::COUNT, "#define ATTRIB_FREE_START " + Util::to_string(to_base(AttribLocation::COUNT) + 1u));
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define MAX_SHININESS " + Util::to_string(Material::MAX_SHININESS));

    for (U8 i = 0u; i < to_U8(ShadingMode::COUNT) + 1u; ++i) {
        const ShadingMode mode = static_cast<ShadingMode>(i);
        AppendToShaderHeader(ShaderType::FRAGMENT, Util::StringFormat("#define SHADING_%s %d", TypeUtil::ShadingModeToString(mode), i));
    }

    AppendToShaderHeader(ShaderType::FRAGMENT, Util::StringFormat("#define SHADING_COUNT %d", to_base(ShadingMode::COUNT)));

    for (U8 i = 0u; i < to_U8(MaterialDebugFlag::COUNT) + 1u; ++i) {
        const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
        AppendToShaderHeader(ShaderType::FRAGMENT, Util::StringFormat("#define DEBUG_%s %d", TypeUtil::MaterialDebugFlagToString(flag), i));
    }

    AppendToShaderHeader(ShaderType::COUNT, "#if defined(PRE_PASS) || defined(SHADOW_PASS)");
    AppendToShaderHeader(ShaderType::COUNT, "#   define DEPTH_PASS");
    AppendToShaderHeader(ShaderType::COUNT, "#endif //PRE_PASS || SHADOW_PASS");

    AppendToShaderHeader(ShaderType::COUNT, "#if defined(COMPUTE_TBN) && !defined(ENABLE_TBN)");
    AppendToShaderHeader(ShaderType::COUNT, "#   define ENABLE_TBN");
    AppendToShaderHeader(ShaderType::COUNT, "#endif //COMPUTE_TBN && !ENABLE_TBN");

    // Vertex shader output
    AppendToShaderHeader(ShaderType::VERTEX, "out Data {");
    addVaryings(ShaderType::VERTEX);
    AppendToShaderHeader(ShaderType::VERTEX, "} _out;\n");

    // Tessellation Control shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "in Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _in[];\n");

    // Tessellation Control shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "out Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _out[];\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, getPassData(ShaderType::TESSELLATION_CTRL));

    // Tessellation Eval shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "in Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _in[];\n");

    // Tessellation Eval shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "out Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _out;\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, getPassData(ShaderType::TESSELLATION_EVAL));

    // Geometry shader input
    AppendToShaderHeader(ShaderType::GEOMETRY, "in Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _in[];\n");

    // Geometry shader output
    AppendToShaderHeader(ShaderType::GEOMETRY, "out Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _out;\n");

    AppendToShaderHeader(ShaderType::GEOMETRY, getPassData(ShaderType::GEOMETRY));

    // Fragment shader input
    AppendToShaderHeader(ShaderType::FRAGMENT, "in Data {");
    addVaryings(ShaderType::FRAGMENT);
    AppendToShaderHeader(ShaderType::FRAGMENT, "} _in;\n");

    AppendToShaderHeader(ShaderType::VERTEX, "#define VAR _out");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define VAR _in[gl_InvocationID]");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define VAR _in[0]");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define VAR _in");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define VAR _in");

    AppendToShaderHeader(ShaderType::COUNT, "_CUSTOM_UNIFORMS__");

    // Check initialization status for GLSL and glsl-optimizer
    return glswState == 1;
}

bool DeInitGLSW() noexcept {
    // Shutdown GLSW
    return glswShutdown() == 1;
}

ShaderProgram::ShaderProgram(GFXDevice& context, 
                             const size_t descriptorHash,
                             const Str256& shaderName,
                             const Str256& shaderFileName,
                             const ResourcePath& shaderFileLocation,
                             ShaderProgramDescriptor descriptor,
                             const bool asyncLoad)
    : CachedResource(ResourceType::GPU_OBJECT, descriptorHash, shaderName, ResourcePath(shaderFileName), shaderFileLocation),
      GraphicsResource(context, Type::SHADER_PROGRAM, getGUID(), _ID(shaderName.c_str())),
      _descriptor(MOV(descriptor)),
      _asyncLoad(asyncLoad)
{
    if (shaderFileName.empty()) {
        assetName(ResourcePath(resourceName().c_str()));
    }
    s_shaderCount.fetch_add(1, std::memory_order_relaxed);
}

ShaderProgram::~ShaderProgram()
{
    Console::d_printfn(Locale::Get(_ID("SHADER_PROGRAM_REMOVE")), resourceName().c_str());
    s_shaderCount.fetch_sub(1, std::memory_order_relaxed);
}

void ShaderProgram::threadedLoad([[maybe_unused]] const bool reloadExisting) {
    if (!weak_from_this().expired()) {
        RegisterShaderProgram(std::dynamic_pointer_cast<ShaderProgram>(shared_from_this()).get());
    }

    CachedResource::load();
};

bool ShaderProgram::load() {
    if (_asyncLoad) {
        Start(*CreateTask([this](const Task&) {threadedLoad(false); }), _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
    } else {
        threadedLoad(false);
    }

    return true;
}

bool ShaderProgram::unload() {
    // Unregister the program from the manager
    return UnregisterShaderProgram(descriptorHash());
}

/// Rebuild the specified shader stages from source code
bool ShaderProgram::recompile(bool& skipped) {
    skipped = true;
    return getState() == ResourceState::RES_LOADED;
}

//================================ static methods ========================================
void ShaderProgram::Idle(PlatformContext& platformContext) {
    OPTICK_EVENT();

    // If we don't have any shaders queued for recompilation, return early
    if (!s_recompileQueue.empty()) {
        // Else, recompile the top program from the queue
        bool skipped = false;
        ShaderProgram* program = s_recompileQueue.top();
        if (program->recompile(skipped)) {
            if (!skipped) {
                if (program->getGUID() == s_lastRequestedShaderProgram.first) {
                    s_lastRequestedShaderProgram = { -1, {} };
                }
            }
            //Re-register because the handle is probably different by now
            RegisterShaderProgram(program);
        } else {
            Console::errorfn(Locale::Get(_ID("ERROR_SHADER_RECOMPILE_FAILED")), program->resourceName().c_str());
        }
        s_recompileQueue.pop();
    }


    // Schedule all of the shader "dump to text file" operations
    static TextDumpEntry textOutputCache;
    while (g_sDumpToFileQueue.try_dequeue(textOutputCache)) {
        DIVIDE_ASSERT(!textOutputCache._name.empty() && !textOutputCache._sourceCode.empty());
        Start(*CreateTask([cache = MOV(textOutputCache)](const Task&) { DumpShaderTextCacheToDisk(cache); }), platformContext.taskPool(TaskPoolType::LOW_PRIORITY));
    }
}

/// Calling this will force a recompilation of all shader stages for the program
/// that matches the name specified
bool ShaderProgram::RecompileShaderProgram(const Str256& name) {
    bool state = false;
    SharedLock<SharedMutex> r_lock(s_programLock);

    // Find the shader program
    for (const auto& [handle, programEntry] : s_shaderPrograms) {
       
        ShaderProgram* program = programEntry.first;
        assert(program != nullptr);

        const Str256& shaderName = program->resourceName();
        // Check if the name matches any of the program's name components    
        if (shaderName.find(name) != Str256::npos || shaderName.compare(name) == 0) {
            // We process every partial match. So add it to the recompilation queue
            s_recompileQueue.push(program);
            // Mark as found
            state = true;
        }
    }
    // If no shaders were found, show an error
    if (!state) {
        Console::errorfn(Locale::Get(_ID("ERROR_RECOMPILE_NOT_FOUND")),  name.c_str());
    }

    return state;
}

ErrorCode ShaderProgram::OnStartup(ResourceCache* parentCache) {
    if_constexpr(!Config::Build::IS_SHIPPING_BUILD) {
        FileWatcher& watcher = FileWatcherManager::allocateWatcher();
        s_shaderFileWatcherID = watcher.getGUID();
        g_sFileWatcherListener.addIgnoredEndCharacter('~');
        g_sFileWatcherListener.addIgnoredExtension("tmp");

        const vector<ResourcePath> atomLocations = GetAllAtomLocations();
        for (const ResourcePath& loc : atomLocations) {
            if (!CreateDirectories(loc)) {
                DebugBreak();
            }
            watcher().addWatch(loc.c_str(), &g_sFileWatcherListener);
        }
    }

    const ResourcePath locPrefix{ Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::GLSL::g_parentShaderLoc };

    shaderAtomLocationPrefix[to_base(ShaderType::FRAGMENT)]          = locPrefix + Paths::Shaders::GLSL::g_fragAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::VERTEX)]            = locPrefix + Paths::Shaders::GLSL::g_vertAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::GEOMETRY)]          = locPrefix + Paths::Shaders::GLSL::g_geomAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::TESSELLATION_CTRL)] = locPrefix + Paths::Shaders::GLSL::g_tescAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::TESSELLATION_EVAL)] = locPrefix + Paths::Shaders::GLSL::g_teseAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::COMPUTE)]           = locPrefix + Paths::Shaders::GLSL::g_compAtomLoc;
    shaderAtomLocationPrefix[to_base(ShaderType::COUNT)]             = locPrefix + Paths::Shaders::GLSL::g_comnAtomLoc;

    shaderAtomExtensionName[to_base(ShaderType::FRAGMENT)]          = Paths::Shaders::GLSL::g_fragAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::VERTEX)]            = Paths::Shaders::GLSL::g_vertAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::GEOMETRY)]          = Paths::Shaders::GLSL::g_geomAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::TESSELLATION_CTRL)] = Paths::Shaders::GLSL::g_tescAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::TESSELLATION_EVAL)] = Paths::Shaders::GLSL::g_teseAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::COMPUTE)]           = Paths::Shaders::GLSL::g_compAtomExt;
    shaderAtomExtensionName[to_base(ShaderType::COUNT)]             = Paths::Shaders::GLSL::g_comnAtomExt;

    for (U8 i = 0u; i < to_base(ShaderType::COUNT) + 1; ++i) {
        shaderAtomExtensionHash[i] = _ID(shaderAtomExtensionName[i].c_str());
    }

    const PlatformContext& ctx = parentCache->context();
    const Configuration& config = ctx.config();

    ShaderProgram::s_UseBindlessTextures = config.rendering.useBindlessTextures && ctx.gfx().GetDeviceInformation()._bindlessTexturesSupported;
    if (!InitGLSW(GFXDevice::GetDeviceInformation(), config)) {
        return ErrorCode::GLSL_INIT_ERROR;
    }
    Console::printfn(Locale::Get(_ID("GLSL_BINDLESS_TEXTURES_STATE")), ShaderProgram::s_UseBindlessTextures ? "True" : "False");

    return ErrorCode::NO_ERR;
}

ErrorCode ShaderProgram::PostInitAPI(ResourceCache* parentCache) {
    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "ImmediateModeEmulation.glsl";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "ImmediateModeEmulation.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    // Create an immediate mode rendering shader that simulates the fixed function pipeline
    {
        ResourceDescriptor immediateModeShader("ImmediateModeEmulation");
        immediateModeShader.threaded(false);
        immediateModeShader.propertyDescriptor(shaderDescriptor);
        s_imShader = CreateResource<ShaderProgram>(parentCache, immediateModeShader);
        assert(s_imShader != nullptr);
    }
    {
        shaderDescriptor._modules.back()._defines.emplace_back("WORLD_PASS", true);
        ResourceDescriptor immediateModeShader("ImmediateModeEmulation-World");
        immediateModeShader.threaded(false);
        immediateModeShader.propertyDescriptor(shaderDescriptor);
        s_imWorldShader = CreateResource<ShaderProgram>(parentCache, immediateModeShader);
        assert(s_imWorldShader != nullptr);
    }

    {
        shaderDescriptor._modules.back()._defines.emplace_back("OIT_PASS", true);
        ResourceDescriptor immediateModeShader("ImmediateModeEmulation-OIT");
        immediateModeShader.threaded(false);
        immediateModeShader.propertyDescriptor(shaderDescriptor);
        s_imWorldOITShader = CreateResource<ShaderProgram>(parentCache, immediateModeShader);
        assert(s_imWorldOITShader != nullptr);
    }
    shaderDescriptor._modules.clear();
    ResourceDescriptor shaderDesc("NULL");
    shaderDesc.threaded(false);
    shaderDesc.propertyDescriptor(shaderDescriptor);
    // Create a null shader (basically telling the API to not use any shaders when bound)
    s_nullShader = CreateResource<ShaderProgram>(parentCache, shaderDesc);

    // The null shader should never be nullptr!!!!
    if (s_nullShader == nullptr) {  // LoL -Ionut
        return ErrorCode::GLSL_INIT_ERROR;
    }

    return ErrorCode::NO_ERR;
}

bool ShaderProgram::OnShutdown() {
    // Make sure we unload all shaders
    s_nullShader.reset();
    s_imShader.reset();
    s_imWorldShader.reset();
    s_imWorldOITShader.reset();
    while (!s_recompileQueue.empty()) {
        s_recompileQueue.pop();
    }
    s_shaderPrograms.clear();
    s_lastRequestedShaderProgram = { -1, {} };

    FileWatcherManager::deallocateWatcher(s_shaderFileWatcherID);
    s_shaderFileWatcherID = -1;

    return DeInitGLSW();
}

bool ShaderProgram::OnThreadCreated(const GFXDevice& gfx, [[maybe_unused]] const std::thread::id& threadID) {
    return InitGLSW(GFXDevice::GetDeviceInformation(), gfx.context().config());
}

/// Whenever a new program is created, it's registered with the manager
void ShaderProgram::RegisterShaderProgram(ShaderProgram* shaderProgram) {
    size_t shaderHash = shaderProgram->descriptorHash();
    UnregisterShaderProgram(shaderHash);

    ScopedLock<SharedMutex> w_lock(s_programLock);
    s_shaderPrograms[shaderProgram->getGUID()] = { shaderProgram, shaderHash };
}

/// Unloading/Deleting a program will unregister it from the manager
bool ShaderProgram::UnregisterShaderProgram(size_t shaderHash) {

    ScopedLock<SharedMutex> w_lock(s_programLock);
    s_lastRequestedShaderProgram = { -1, {} };

    if (s_shaderPrograms.empty()) {
        // application shutdown?
        return true;
    }

    const ShaderProgramMap::const_iterator it = eastl::find_if(
        eastl::cbegin(s_shaderPrograms),
        eastl::cend(s_shaderPrograms),
        [shaderHash](const ShaderProgramMap::value_type& item) {
            return item.second.second == shaderHash;
        });

    if (it != eastl::cend(s_shaderPrograms)) {
        if (it->first == s_lastRequestedShaderProgram.first) {
            s_lastRequestedShaderProgram = { -1, {} };
        }

        s_shaderPrograms.erase(it);
        return true;
    }

    // application shutdown?
    return false;
}

ShaderProgram* ShaderProgram::FindShaderProgram(const I64 shaderHandle) {
    SharedLock<SharedMutex> r_lock(s_programLock);
    if (shaderHandle == s_lastRequestedShaderProgram.first) {
        return s_lastRequestedShaderProgram.second.first;
    }

    const auto& it = s_shaderPrograms.find(shaderHandle);
    if (it != eastl::cend(s_shaderPrograms)) {
        s_lastRequestedShaderProgram = { shaderHandle, it->second };

        return it->second.first;
    }
    s_lastRequestedShaderProgram = { -1, {} };
    return nullptr;
}

ShaderProgram* ShaderProgram::FindShaderProgram(const size_t shaderHash) {
    SharedLock<SharedMutex> r_lock(s_programLock);
    if (s_lastRequestedShaderProgram.first != -1 && s_lastRequestedShaderProgram.second.second == shaderHash) {
        return s_lastRequestedShaderProgram.second.first;
    }

    for (const auto& [handle, programEntry] : s_shaderPrograms) {
        if (programEntry.second == shaderHash) {
            s_lastRequestedShaderProgram = { handle, programEntry };

            return programEntry.first;
        }
    }

    s_lastRequestedShaderProgram = { -1, {} };
    return nullptr;
}

const ShaderProgram_ptr& ShaderProgram::DefaultShader() noexcept {
    return s_imShader;
}

const ShaderProgram_ptr& ShaderProgram::DefaultShaderWorld() noexcept {
    return s_imWorldShader;
}

const ShaderProgram_ptr& ShaderProgram::DefaultShaderOIT() noexcept {
    return s_imWorldOITShader;
}

const ShaderProgram_ptr& ShaderProgram::NullShader() noexcept {
    return s_nullShader;
}

const I64 ShaderProgram::NullShaderGUID() noexcept {
    return s_nullShader != nullptr ? s_nullShader->getGUID() : -1;
}

void ShaderProgram::RebuildAllShaders() {
    SharedLock<SharedMutex> r_lock(s_programLock);
    for (const auto& [handle, programEntry] : s_shaderPrograms) {
        s_recompileQueue.push(programEntry.first);
    }
}

vector<ResourcePath> ShaderProgram::GetAllAtomLocations() {
    static vector<ResourcePath> atomLocations;
    if (atomLocations.empty()) {
        // General
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation);
        // GLSL
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_comnAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_compAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_fragAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_geomAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_tescAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_teseAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_vertAtomLoc);
        // HLSL
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::HLSL::g_parentShaderLoc);

    }

    return atomLocations;
}

size_t ShaderProgram::DefinesHash(const ModuleDefines& defines) {
    if (defines.empty()) {
        return 0;
    }

    size_t hash = 7919;
    for (const auto& [defineString, appendPrefix] : defines) {
        Util::Hash_combine(hash, _ID(defineString.c_str()));
        Util::Hash_combine(hash, appendPrefix);
    }
    return hash;
}


const string& ShaderProgram::ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed) {
    ScopedLock<SharedMutex> w_lock(s_atomLock);
    return ShaderFileReadLocked(filePath, atomName, recurse, foundAtoms, wasParsed);
}

eastl::string ShaderProgram::PreprocessIncludes(const ResourcePath& name,
                                                const eastl::string& source,
                                                const I32 level,
                                                vector<ResourcePath>& foundAtoms,
                                                const bool lock) {
    if (level > s_maxHeaderRecursionLevel) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INCLUD_LIMIT")));
    }

    size_t lineNumber = 1;
    regexNamespace::smatch matches;

    string line;
    eastl::string output, includeString;
    istringstream input(source.c_str());

    while (std::getline(input, line)) {
        const std::string_view directive = !line.empty() ? std::string_view{line}.substr(1) : "";

        const bool isInclude = Util::BeginsWith(line, "#", true) && 
                               !Util::BeginsWith(directive, "version", true) &&
                               !Util::BeginsWith(directive, "extension", true) &&
                               !Util::BeginsWith(directive, "define", true) &&
                               !Util::BeginsWith(directive, "if", true) &&
                               !Util::BeginsWith(directive, "else", true) &&
                               !Util::BeginsWith(directive, "elif", true) &&
                               !Util::BeginsWith(directive, "endif", true) &&
                               !Util::BeginsWith(directive, "pragma", true) &&
                               regexNamespace::regex_search(line, matches, Paths::g_includePattern);
        if (!isInclude) {
            output.append(line.c_str());
        } else {
            const ResourcePath includeFile = ResourcePath(Util::Trim(matches[1].str()));
            foundAtoms.push_back(includeFile);

            ShaderType typeIndex = ShaderType::COUNT;
            bool found = false;
            // switch will throw warnings due to promotion to int
            const U64 extHash = _ID(Util::GetTrailingCharacters(includeFile.str(), 4).c_str());
            for (U8 i = 0; i < to_base(ShaderType::COUNT) + 1; ++i) {
                if (extHash == shaderAtomExtensionHash[i]) {
                    typeIndex = static_cast<ShaderType>(i);
                    found = true;
                    break;
                }
            }

            DIVIDE_ASSERT(found, "Invalid shader include type");
            bool wasParsed = false;
            if (lock) {
                includeString = ShaderFileRead(shaderAtomLocationPrefix[to_U32(typeIndex)], includeFile, true, foundAtoms, wasParsed).c_str();
            } else {
                includeString = ShaderFileReadLocked(shaderAtomLocationPrefix[to_U32(typeIndex)], includeFile, true, foundAtoms, wasParsed).c_str();
            }
            if (includeString.empty()) {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NO_INCLUDE_FILE")), name.c_str(), lineNumber, includeFile.c_str());
            }
            if (wasParsed) {
                output.append(includeString);
            } else {
                output.append(PreprocessIncludes(name, includeString, level + 1, foundAtoms, lock));
            }
        }

        output.append("\n");
        ++lineNumber;
    }

    return output;
}

/// Open the file found at 'filePath' matching 'atomName' and return it's source code
const string& ShaderProgram::ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed) {
    const U64 atomNameHash = _ID(atomName.c_str());
    // See if the atom was previously loaded and still in cache
    const AtomMap::iterator it = s_atoms.find(atomNameHash);
    
    // If that's the case, return the code from cache
    if (it != std::cend(s_atoms)) {
        const auto& atoms = s_atomIncludes[atomNameHash];
        foundAtoms.insert(end(foundAtoms), begin(atoms), end(atoms));
        wasParsed = true;
        return it->second;
    }

    wasParsed = false;
    // If we forgot to specify an atom location, we have nothing to return
    assert(!filePath.empty());

    // Open the atom file and add the code to the atom cache for future reference
    eastl::string output;
    if (readFile(filePath, atomName, output, FileType::TEXT) != FileError::NONE) {
        DIVIDE_UNEXPECTED_CALL();
    }

    vector<ResourcePath> atoms = {};
    vector<UniformDeclaration> uniforms = {};
    if (recurse) {
        output = PreprocessIncludes(atomName, output, 0, atoms, false);
    }

    foundAtoms.insert(end(foundAtoms), begin(atoms), end(atoms));
    const auto&[entry, result] = s_atoms.insert({ atomNameHash, output.c_str() });
    assert(result);
    s_atomIncludes.insert({atomNameHash, atoms});

    // Return the source code
    return entry->second;
}

bool ShaderProgram::ShaderFileRead(const ResourcePath& filePath, const ResourcePath& fileName, eastl::string& sourceCodeOut) {
    return readFile(filePath, ResourcePath(DecorateFileName(fileName.str())), sourceCodeOut, FileType::TEXT) == FileError::NONE;
}

bool ShaderProgram::ShaderFileWrite(const ResourcePath& filePath, const ResourcePath& fileName, const char* sourceCode) {
    return writeFile(filePath, ResourcePath(DecorateFileName(fileName.str())), sourceCode, strlen(sourceCode), FileType::TEXT) == FileError::NONE;
}

void ShaderProgram::DumpShaderTextCacheToDisk(const TextDumpEntry& entry) {
    if (!ShaderFileWrite(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationText, ResourcePath(entry._name), entry._sourceCode.c_str())) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

eastl::string ShaderProgram::GatherUniformDeclarations(const eastl::string & source, vector<UniformDeclaration>& foundUniforms) {
    static const regexNamespace::regex uniformPattern { R"(^\s*uniform\s+\s*([^),^;^\s]*)\s+([^),^;^\s]*\[*\s*\]*)\s*(?:=*)\s*(?:\d*.*)\s*(?:;+))" };

    eastl::string ret;
    ret.reserve(source.size());

    string line;
    regexNamespace::smatch matches;
    istringstream input(source.c_str());
    while (std::getline(input, line)) {
        if (Util::BeginsWith(line, "uniform", true) &&
            regexNamespace::regex_search(line, matches, uniformPattern))
        {
            foundUniforms.push_back(UniformDeclaration{
                Util::Trim(matches[1].str()), //type
                Util::Trim(matches[2].str())  //name
            });
        } else {
            ret.append(line.c_str());
            ret.append("\n");
        }
    }

    return ret;
}

void ShaderProgram::QueueShaderWriteToFile(const string& sourceCode, const Str256& fileName) {
    g_sDumpToFileQueue.enqueue({ fileName, sourceCode });
}

ShaderProgram::AtomUniformPair ShaderProgram::loadSourceCode(const Str128& stageName,
                                                             const Str8& extension,
                                                             const string& header,
                                                             size_t definesHash,
                                                             const bool reloadExisting,
                                                             Str256& fileNameOut,
                                                             eastl::string& sourceCodeOut) const
{
    AtomUniformPair ret = {};

    fileNameOut = definesHash != 0
                            ? Util::StringFormat("%s.%zu.%s", stageName.c_str(), definesHash, extension.c_str())
                            : Util::StringFormat("%s.%s", stageName.c_str(), extension.c_str());

    sourceCodeOut.resize(0);
    if (s_useShaderTextCache && !reloadExisting) {
        ShaderFileRead(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationText,
                       ResourcePath(fileNameOut),
                       sourceCodeOut);
    }

    if (sourceCodeOut.empty()) {
        // Use GLSW to read the appropriate part of the effect file
        // based on the specified stage and properties
        const char* sourceCodeStr = glswGetShader(stageName.c_str());
        if (sourceCodeStr != nullptr) {
            sourceCodeOut = sourceCodeStr;
        }

        // GLSW may fail for various reasons (not a valid effect stage, invalid name, etc)
        if (!sourceCodeOut.empty()) {
            // And replace in place with our program's headers created earlier
            Util::ReplaceStringInPlace(sourceCodeOut, "_CUSTOM_DEFINES__", header);
            sourceCodeOut = PreprocessIncludes(ResourcePath(resourceName()), sourceCodeOut, 0, ret._atoms, true);
            sourceCodeOut = Preprocessor::PreProcess(sourceCodeOut, fileNameOut.c_str());
            sourceCodeOut = GatherUniformDeclarations(sourceCodeOut, ret._uniforms);
        }
    }

    return ret;
}

void ShaderProgram::OnAtomChange(const std::string_view atomName, const FileUpdateEvent evt) {
    DIVIDE_ASSERT(evt != FileUpdateEvent::COUNT);

    // Do nothing if the specified file is "deleted". We do not want to break running programs
    if (evt == FileUpdateEvent::DELETE) {
        return;
    }
    // ADD and MODIFY events should get processed as usual
    {
        // Clear the atom from the cache
        ScopedLock<SharedMutex> w_lock(s_atomLock);
        if (s_atoms.erase(_ID(string{ atomName }.c_str())) == 1) {
            NOP();
        }
    }

    //Get list of shader programs that use the atom and rebuild all shaders in list;
    SharedLock<SharedMutex> r_lock(s_programLock);
    for (const auto& [_, programEntry] : s_shaderPrograms) {
        programEntry.first->onAtomChangeInternal(atomName, evt);
    }
}

void ShaderProgram::onAtomChangeInternal(const std::string_view atomName, const FileUpdateEvent evt) {
    NOP();
}

void ShaderProgram::setGLSWPath(const bool clearExisting) {
    // The context is thread_local so each call to this should be thread safe
    if (clearExisting) {
        glswClearCurrentContext();
    }
    glswSetPath((assetLocation() + "/" + Paths::Shaders::GLSL::g_parentShaderLoc).c_str(), ".glsl");
}
};
