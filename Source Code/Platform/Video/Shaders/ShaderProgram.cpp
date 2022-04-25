#include "stdafx.h"

#include "Headers/ShaderProgram.h"
#include "Headers/GLSLToSPIRV.h"

#include "Managers/Headers/SceneManager.h"

#include "Rendering/Headers/Renderer.h"
#include "Geometry/Material/Headers/Material.h"
#include "Scenes/Headers/SceneShaderData.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/glsw/Headers/glsw.h"

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
#include <boost/regex.hpp>
#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp> // lexer class
#include <boost/wave/cpplexer/cpp_lex_token.hpp>    // token class
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Divide {
constexpr U16 BYTE_BUFFER_VERSION = 1u;

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

ShaderProgram::ShaderQueue ShaderProgram::s_recompileQueue;
ShaderProgram::ShaderProgramMap ShaderProgram::s_shaderPrograms;
ShaderProgram::LastRequestedShader ShaderProgram::s_lastRequestedShaderProgram = {};

Mutex ShaderProgram::s_programLock;
std::atomic_int ShaderProgram::s_shaderCount;

size_t ShaderProgramDescriptor::getHash() const {
    _hash = PropertyDescriptor::getHash();
    for (const ShaderModuleDescriptor& desc : _modules) {
        Util::Hash_combine(_hash, ShaderProgram::DefinesHash(desc._defines),
                                  std::string(desc._variant.c_str()),
                                  desc._sourceFile.data(),
                                  desc._moduleType);
    }
    return _hash;
}

UpdateListener g_sFileWatcherListener(
    [](const std::string_view atomName, const FileUpdateEvent evt) {
        ShaderProgram::OnAtomChange(atomName, evt);
    }
);

namespace {
 //Note: this doesn't care about arrays so those won't sort properly to reduce wastage
    const auto g_TypePriority = [](const U64 typeHash) -> I32 {
        switch (typeHash) {
            case _ID("dmat4")  :            //128 bytes
            case _ID("dmat4x3"): return 0;  // 96 bytes
            case _ID("dmat3")  : return 1;  // 72 bytes
            case _ID("dmat4x2"):            // 64 bytes
            case _ID("mat4")   : return 2;  // 64 bytes
            case _ID("dmat3x2"):            // 48 bytes
            case _ID("mat4x3") : return 3;  // 48 bytes
            case _ID("mat3")   : return 4;  // 36 bytes
            case _ID("dmat2")  :            // 32 bytes
            case _ID("dvec4")  :            // 32 bytes
            case _ID("mat4x2") : return 5;  // 32 bytes
            case _ID("dvec3")  :            // 24 bytes
            case _ID("mat3x2") : return 6;  // 24 bytes
            case _ID("mat2")   :            // 16 bytes
            case _ID("dvec2")  :            // 16 bytes
            case _ID("bvec4")  :            // 16 bytes
            case _ID("ivec4")  :            // 16 bytes
            case _ID("uvec4")  :            // 16 bytes
            case _ID("vec4")   : return 7;  // 16 bytes
            case _ID("bvec3")  :            // 12 bytes
            case _ID("ivec3")  :            // 12 bytes
            case _ID("uvec3")  :            // 12 bytes
            case _ID("vec3")   : return 8;  // 12 bytes
            case _ID("double") :            //  8 bytes
            case _ID("bvec2")  :            //  8 bytes
            case _ID("ivec2")  :            //  8 bytes
            case _ID("uvec2")  :            //  8 bytes
            case _ID("vec2")   : return 9;  //  8 bytes
            case _ID("int")    :            //  4 bytes
            case _ID("uint")   :            //  4 bytes
            case _ID("float")  : return 10; //  4 bytes
            // No real reason for this, but generated shader code looks cleaner
            case _ID("bool")   : return 11; //  4 bytes
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        return 999;
    };
};

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

bool InitGLSW(const RenderAPI renderingAPI, const DeviceInformation& deviceInfo, const Configuration& config) {
    const auto AppendToShaderHeader = [](const ShaderType type, const string& entry) {
        glswAddDirectiveToken(type != ShaderType::COUNT ? Names::shaderTypes[to_U8(type)] : "", entry.c_str());
    };

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

    if (renderingAPI == RenderAPI::OpenGL) {

    } else if (renderingAPI == RenderAPI::Vulkan) {

    } else {
        DIVIDE_UNEXPECTED_CALL();
    }

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
    AppendToShaderHeader(ShaderType::COUNT, renderingAPI == RenderAPI::OpenGL ? "#version 460 core" : "#version 450");
    AppendToShaderHeader(ShaderType::COUNT, "/*Copyright 2009-2022 DIVIDE-Studio*/");

    if (renderingAPI == RenderAPI::OpenGL) {
        if (ShaderProgram::s_UseBindlessTextures) {
            AppendToShaderHeader(ShaderType::COUNT, "#extension  GL_ARB_bindless_texture : require");
        }

        //AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_gpu_shader5 : require");
        AppendToShaderHeader(ShaderType::COUNT, "#define SPECIFY_SET(SET)");
    } else {
        AppendToShaderHeader(ShaderType::COUNT, "#define SPECIFY_SET(SET) set = SET");
    }

    AppendToShaderHeader(ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(SPECIFY_SET(SET) binding = BINDING)");
    AppendToShaderHeader(ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET(SET, BINDING, OFFSET) layout(SPECIFY_SET(SET) binding = BINDING, offset = OFFSET)");
    AppendToShaderHeader(ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(SPECIFY_SET(SET) binding = BINDING, LAYOUT)");
    AppendToShaderHeader(ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET_LAYOUT(SET, BINDING, OFFSET, LAYOUT) layout(SPECIFY_SET(SET) binding = BINDING, offset = OFFSET, LAYOUT)");

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

    AppendToShaderHeader(ShaderType::VERTEX,   "#define BUFFER_BONE_TRANSFORMS " + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define BUFFER_BONE_TRANSFORMS_PREV " + Util::to_string(to_base(ShaderBufferLocation::BONE_TRANSFORMS_PREV)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define MAX_BONE_COUNT_PER_NODE " + Util::to_string(Config::MAX_BONE_COUNT_PER_NODE));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_POSITION " + Util::to_string(to_base(AttribLocation::POSITION)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_TEXCOORD " + Util::to_string(to_base(AttribLocation::TEXCOORD)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_NORMAL " + Util::to_string(to_base(AttribLocation::NORMAL)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_TANGENT " + Util::to_string(to_base(AttribLocation::TANGENT)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_COLOR " + Util::to_string(to_base(AttribLocation::COLOR)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_BONE_WEIGHT " + Util::to_string(to_base(AttribLocation::BONE_WEIGHT)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_BONE_INDICE " + Util::to_string(to_base(AttribLocation::BONE_INDICE)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_WIDTH " + Util::to_string(to_base(AttribLocation::WIDTH)));
    AppendToShaderHeader(ShaderType::VERTEX,   "#define ATTRIB_GENERIC " + Util::to_string(to_base(AttribLocation::GENERIC)));
    AppendToShaderHeader(ShaderType::COUNT,    "#define ATTRIB_FREE_START 12");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define MAX_SHININESS " + Util::to_string(Material::MAX_SHININESS));

    const string interfaceLocationString = "layout(location = 0) ";

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

    AppendToShaderHeader(ShaderType::GEOMETRY, "#if !defined(INPUT_PRIMITIVE_SIZE)");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#   define INPUT_PRIMITIVE_SIZE 1");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#endif //!INPUT_PRIMITIVE_SIZE");

    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#if !defined(TESSELLATION_OUTPUT_VERTICES)");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#   define TESSELLATION_OUTPUT_VERTICES 4");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#endif //!TESSELLATION_OUTPUT_VERTICES");

    // Vertex shader output
    AppendToShaderHeader(ShaderType::VERTEX, interfaceLocationString + "out Data {");
    addVaryings(ShaderType::VERTEX);
    AppendToShaderHeader(ShaderType::VERTEX, "} _out;\n");

    // Tessellation Control shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, interfaceLocationString + "in Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _in[gl_MaxPatchVertices];\n");

    // Tessellation Control shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, interfaceLocationString + "out Data {");
    addVaryings(ShaderType::TESSELLATION_CTRL);
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "} _out[TESSELLATION_OUTPUT_VERTICES];\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, getPassData(ShaderType::TESSELLATION_CTRL));

    // Tessellation Eval shader input
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, interfaceLocationString + "in Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _in[gl_MaxPatchVertices];\n");

    // Tessellation Eval shader output
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, interfaceLocationString + "out Data {");
    addVaryings(ShaderType::TESSELLATION_EVAL);
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "} _out;\n");

    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, getPassData(ShaderType::TESSELLATION_EVAL));

    // Geometry shader input
    AppendToShaderHeader(ShaderType::GEOMETRY, interfaceLocationString + "in Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _in[INPUT_PRIMITIVE_SIZE];\n");

    // Geometry shader output
    AppendToShaderHeader(ShaderType::GEOMETRY, interfaceLocationString + "out Data {");
    addVaryings(ShaderType::GEOMETRY);
    AppendToShaderHeader(ShaderType::GEOMETRY, "} _out;\n");

    AppendToShaderHeader(ShaderType::GEOMETRY, getPassData(ShaderType::GEOMETRY));

    // Fragment shader input
    AppendToShaderHeader(ShaderType::FRAGMENT, interfaceLocationString + "in Data {");
    addVaryings(ShaderType::FRAGMENT);
    AppendToShaderHeader(ShaderType::FRAGMENT, "} _in;\n");

    AppendToShaderHeader(ShaderType::VERTEX, "#define VAR _out");
    AppendToShaderHeader(ShaderType::TESSELLATION_CTRL, "#define VAR _in[gl_InvocationID]");
    AppendToShaderHeader(ShaderType::TESSELLATION_EVAL, "#define VAR _in[0]");
    AppendToShaderHeader(ShaderType::GEOMETRY, "#define VAR _in");
    AppendToShaderHeader(ShaderType::FRAGMENT, "#define VAR _in");

    AppendToShaderHeader(ShaderType::COUNT, "//_CUSTOM_UNIFORMS_\\");

    // Check initialization status for GLSL and glsl-optimizer
    return glswState == 1;
}

 ShaderProgram::UniformBlockUploader::UniformBlockUploader(GFXDevice& context, const ShaderProgram::UniformBlockUploaderDescriptor& descriptor) 
 {
    _descriptor = descriptor;

    const auto GetSizeOf = [](const GFX::PushConstantType type) noexcept -> size_t {
        switch (type) {
            case GFX::PushConstantType::INT: return sizeof(I32);
            case GFX::PushConstantType::UINT: return sizeof(U32);
            case GFX::PushConstantType::FLOAT: return sizeof(F32);
            case GFX::PushConstantType::DOUBLE: return sizeof(D64);
        };

        DIVIDE_UNEXPECTED_CALL_MSG("Unexpected push constant type");
        return 0u;
    };

    if (_descriptor._reflectionData._blockMembers.empty()) {
        return;
    }

    const size_t activeMembers = _descriptor._reflectionData._blockMembers.size();
    _blockMembers.resize(activeMembers);
    if (_descriptor._reflectionData._blockSize > _uniformBlockSizeAligned || _buffer == nullptr) {
        _uniformBlockSizeAligned = Util::GetAlignmentCorrected(_descriptor._reflectionData._blockSize, ShaderBuffer::AlignmentRequirement(ShaderBuffer::Usage::CONSTANT_BUFFER));
        _localDataCopy.resize(_uniformBlockSizeAligned);

        ShaderBufferDescriptor bufferDescriptor{};
        bufferDescriptor._name = _descriptor._reflectionData._targetBlockName.c_str();
        bufferDescriptor._name.append("_");
        bufferDescriptor._name.append(_descriptor._parentShaderName.c_str());
        bufferDescriptor._usage = ShaderBuffer::Usage::CONSTANT_BUFFER;
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._ringBufferLength = 3;
        bufferDescriptor._bufferParams._elementSize = _uniformBlockSizeAligned;
        _buffer = context.newSB(bufferDescriptor);
    } else {
        _buffer->clearData();
    }

    std::memset(_localDataCopy.data(), 0, _localDataCopy.size());

    size_t requiredExtraMembers = 0;
    for (size_t member = 0; member < activeMembers; ++member) {
        BlockMember& bMember = _blockMembers[member];
        bMember._externalData = _descriptor._reflectionData._blockMembers[member];
        bMember._nameHash = _ID(bMember._externalData._name.c_str());
        bMember._elementSize = GetSizeOf(bMember._externalData._type);
        if (bMember._externalData._matrixDimensions.x > 0 || bMember._externalData._matrixDimensions.y > 0) {
            bMember._elementSize = bMember._externalData._matrixDimensions.x * bMember._externalData._matrixDimensions.y * bMember._elementSize;
        } else {
            bMember._elementSize = bMember._externalData._vectorDimensions * bMember._elementSize;
        }

        bMember._size = bMember._elementSize;
        if (bMember._externalData._arrayInnerSize > 0) {
            bMember._size *= bMember._externalData._arrayInnerSize;
        }
        if (bMember._externalData._arrayOuterSize > 0) {
            bMember._size *= bMember._externalData._arrayOuterSize;
        }

        requiredExtraMembers += (bMember._externalData._arrayInnerSize * bMember._externalData._arrayOuterSize);
    }

    vector<BlockMember> arrayMembers;
    arrayMembers.reserve(requiredExtraMembers);

    for (size_t member = 0; member < activeMembers; ++member) {
        const BlockMember& bMember = _blockMembers[member];
        size_t offset = 0u;
        if (bMember._externalData._arrayInnerSize > 0) {
            for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                for (size_t j = 0; j < bMember._externalData._arrayInnerSize; ++j) {
                    BlockMember newMember = bMember;
                    newMember._externalData._name = Util::StringFormat("%s[%d][%d]", bMember._externalData._name.c_str(), i, j);
                    newMember._nameHash = _ID(newMember._externalData._name.c_str());
                    newMember._externalData._arrayOuterSize -= i;
                    newMember._externalData._arrayInnerSize -= j;
                    newMember._size -= offset;
                    newMember._externalData._offset = offset;
                    offset += bMember._elementSize;
                    arrayMembers.push_back(newMember);
                }
            }
            for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                BlockMember newMember = bMember;
                newMember._externalData._name = Util::StringFormat("%s[%d]", bMember._externalData._name.c_str(), i);
                newMember._nameHash = _ID(newMember._externalData._name.c_str());
                newMember._externalData._arrayOuterSize -= i;
                newMember._size -= i * (bMember._externalData._arrayInnerSize * bMember._elementSize);
                newMember._externalData._offset = i * (bMember._externalData._arrayInnerSize * bMember._elementSize);
                arrayMembers.push_back(newMember);
            }
        } else if (bMember._externalData._arrayOuterSize > 0) {
            for (size_t i = 0; i < bMember._externalData._arrayOuterSize; ++i) {
                BlockMember newMember = bMember;
                newMember._externalData._name = Util::StringFormat("%s[%d]", bMember._externalData._name.c_str(), i);
                newMember._nameHash = _ID(newMember._externalData._name.c_str());
                newMember._externalData._arrayOuterSize -= i;
                newMember._size -= offset;
                newMember._externalData._offset = offset;
                offset += bMember._elementSize;
                arrayMembers.push_back(newMember);
            }
        }
    }

    if (!arrayMembers.empty()) {
        _blockMembers.insert(end(_blockMembers), begin(arrayMembers), end(arrayMembers));
    }
}

void ShaderProgram::UniformBlockUploader::uploadPushConstant(const GFX::PushConstant& constant, bool force) noexcept {
    if (constant.type() == GFX::PushConstantType::COUNT || constant.bindingHash() == 0u) {
        return;
    }

    if (_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr) {
        for (BlockMember& member : _blockMembers) {
            if (member._nameHash == constant.bindingHash()) {
                DIVIDE_ASSERT(constant.dataSize() <= member._size);

                      Byte* dst = &_localDataCopy.data()[member._externalData._offset];
                const Byte* src = constant.data();
                const size_t numBytes = constant.dataSize();

                if (std::memcmp(dst, src, numBytes) != 0) {
                    std::memcpy(dst, src, numBytes);
                    _uniformBlockDirty = true;
                }

                return;
            }
        }
    }
}

void ShaderProgram::UniformBlockUploader::commit() {
    if (!_uniformBlockDirty) {
        return;
    }

    DIVIDE_ASSERT(_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr);
    const bool rebind = _needsQueueIncrement;
    if (_needsQueueIncrement) {
        _buffer->incQueue();
        _needsQueueIncrement = false;
    }
    _buffer->writeData(_localDataCopy.data());
    _needsQueueIncrement = true;
    _uniformBlockDirty = false;
    if (rebind) {
        prepare();
    }
}

void ShaderProgram::UniformBlockUploader::prepare() {
    if (_descriptor._reflectionData._targetBlockBindingIndex != Reflection::INVALID_BINDING_INDEX && _buffer != nullptr) {
        _buffer->bind(to_U8(_descriptor._reflectionData._targetBlockBindingIndex));
    }
}

ShaderProgram::ShaderProgram(GFXDevice& context, 
                             const size_t descriptorHash,
                             const Str256& shaderName,
                             const Str256& shaderFileName,
                             const ResourcePath& shaderFileLocation,
                             ShaderProgramDescriptor descriptor,
                             ResourceCache& parentCache)
    : CachedResource(ResourceType::GPU_OBJECT, descriptorHash, shaderName, ResourcePath(shaderFileName), shaderFileLocation),
      GraphicsResource(context, Type::SHADER_PROGRAM, getGUID(), _ID(shaderName.c_str())),
      _descriptor(MOV(descriptor)),
      _parentCache(parentCache)
{
    if (shaderFileName.empty()) {
        assetName(ResourcePath(resourceName().c_str()));
    }
    s_shaderCount.fetch_add(1, std::memory_order_relaxed);
}

ShaderProgram::~ShaderProgram()
{
    _parentCache.remove(this);
    Console::d_printfn(Locale::Get(_ID("SHADER_PROGRAM_REMOVE")), resourceName().c_str());
    s_shaderCount.fetch_sub(1, std::memory_order_relaxed);
}

void ShaderProgram::threadedLoad([[maybe_unused]] const bool reloadExisting) {
    RegisterShaderProgram(this);
    CachedResource::load();
};

bool ShaderProgram::load() {
    Start(*CreateTask([this](const Task&) {threadedLoad(false); }), _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
    return true;
}

bool ShaderProgram::unload() {
    // Our GPU Arena will clean up the memory, but we should still destroy these
    _uniformBlockBuffers.clear();
    // Unregister the program from the manager
    if (UnregisterShaderProgram(handle())) {
        handle(INVALID_HANDLE);
    }

    return true;
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
        if (!program->recompile(skipped)) {
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

    ScopedLock<Mutex> lock(s_programLock);

    // Find the shader program
    for (const ShaderProgramMapEntry& entry: s_shaderPrograms) {
       
        ShaderProgram* program = entry._program;
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

    const ResourcePath locPrefix{ Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::GLSL::g_GLSLShaderLoc };

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
    shaderAtomExtensionName[to_base(ShaderType::COUNT)]             = "." + Paths::Shaders::GLSL::g_comnAtomExt;

    for (U8 i = 0u; i < to_base(ShaderType::COUNT) + 1; ++i) {
        shaderAtomExtensionHash[i] = _ID(shaderAtomExtensionName[i].c_str());
    }

    const PlatformContext& ctx = parentCache->context();
    const Configuration& config = ctx.config();

    ShaderProgram::s_UseBindlessTextures = config.rendering.useBindlessTextures && ctx.gfx().GetDeviceInformation()._bindlessTexturesSupported;
    if (!InitGLSW(ctx.gfx().renderAPI(), GFXDevice::GetDeviceInformation(), config)) {
        return ErrorCode::GLSL_INIT_ERROR;
    }

    SpirvHelper::Init();

    Console::printfn(Locale::Get(_ID("GLSL_BINDLESS_TEXTURES_STATE")), ShaderProgram::s_UseBindlessTextures ? "True" : "False");

    return ErrorCode::NO_ERR;
}

bool ShaderProgram::OnShutdown() {
    SpirvHelper::Finalize();

    while (!s_recompileQueue.empty()) {
        s_recompileQueue.pop();
    }
    s_shaderPrograms.fill({});
    s_lastRequestedShaderProgram = {};

    FileWatcherManager::deallocateWatcher(s_shaderFileWatcherID);
    s_shaderFileWatcherID = -1;

    return glswShutdown() == 1;
}

bool ShaderProgram::OnThreadCreated(const GFXDevice& gfx, [[maybe_unused]] const std::thread::id& threadID) {
    return InitGLSW(gfx.renderAPI(), GFXDevice::GetDeviceInformation(), gfx.context().config());
}

/// Whenever a new program is created, it's registered with the manager
void ShaderProgram::RegisterShaderProgram(ShaderProgram* shaderProgram) {
    const auto cleanOldShaders = []() {
        DIVIDE_UNEXPECTED_CALL_MSG("Not Implemented!");
    };

    assert(shaderProgram != nullptr);

    ScopedLock<Mutex> lock(s_programLock);
    if (shaderProgram->handle() != INVALID_HANDLE) {
        const ShaderProgramMapEntry& existingEntry = s_shaderPrograms[shaderProgram->handle()._id];
        if (existingEntry._generation == shaderProgram->handle()._generation) {
            // Nothing to do. Probably a reload of some kind.
            assert(existingEntry._program != nullptr && existingEntry._program->getGUID() == shaderProgram->getGUID());
            return;
        }
    }

    U16 idx = 0u;
    bool retry = false;
    for (ShaderProgramMapEntry& entry: s_shaderPrograms) {
        if (entry._program == nullptr && entry._generation < U8_MAX) {
            entry._program = shaderProgram;
            shaderProgram->_handle = { idx, entry._generation };
            return;
        }
        if (++idx == 0u) {
            if (retry) {
                // We only retry once!
                DIVIDE_UNEXPECTED_CALL();
            }
            retry = true;
            // Handle overflow
            cleanOldShaders();
        }
    }
}

/// Unloading/Deleting a program will unregister it from the manager
bool ShaderProgram::UnregisterShaderProgram(const Handle shaderHandle) {

    if (shaderHandle != INVALID_HANDLE) {
        ScopedLock<Mutex> lock(s_programLock);
        ShaderProgramMapEntry& entry = s_shaderPrograms[shaderHandle._id];
        if (entry._generation == shaderHandle._generation) {
            if (entry._program && entry._program == s_lastRequestedShaderProgram._program) {
                s_lastRequestedShaderProgram = {};
            }
            entry._program = nullptr;
            if (entry._generation < U8_MAX) {
                entry._generation += 1u;
            }
            return true;
        }
    }

    // application shutdown?
    return false;
}

ShaderProgram* ShaderProgram::FindShaderProgram(const Handle shaderHandle) {
    ScopedLock<Mutex> lock(s_programLock);

    if (shaderHandle == s_lastRequestedShaderProgram._handle) {
        return s_lastRequestedShaderProgram._program;
    }

    assert(shaderHandle._id != U16_MAX && shaderHandle._generation != U8_MAX);
    const ShaderProgramMapEntry& entry = s_shaderPrograms[shaderHandle._id];
    if (entry._generation == shaderHandle._generation) {
        s_lastRequestedShaderProgram = { entry._program, shaderHandle };

        return entry._program;
    }

    s_lastRequestedShaderProgram = {};
    return nullptr;
}

void ShaderProgram::RebuildAllShaders() {
    ScopedLock<Mutex> lock(s_programLock);
    for (const ShaderProgramMapEntry& entry : s_shaderPrograms) {
        if (entry._program != nullptr) {
            s_recompileQueue.push(entry._program);
        }
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
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_comnAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_compAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_fragAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_geomAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_tescAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_teseAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                   Paths::Shaders::GLSL::g_vertAtomLoc);

    }

    return atomLocations;
}

size_t ShaderProgram::DefinesHash(const ModuleDefines& defines) {
    if (defines.empty()) {
        return 0u;
    }

    size_t hash = 7919;
    for (const auto& [defineString, appendPrefix] : defines) {
        Util::Hash_combine(hash, _ID(defineString.c_str()));
        Util::Hash_combine(hash, appendPrefix);
    }
    return hash;
}
const string& ShaderProgram::ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<U64>& foundAtomIDsInOut, bool& wasParsed) {
    ScopedLock<SharedMutex> w_lock(s_atomLock);
    return ShaderFileReadLocked(filePath, atomName, recurse, foundAtomIDsInOut, wasParsed);
}

eastl::string ShaderProgram::PreprocessIncludes(const ResourcePath& name,
                                                const eastl::string& source,
                                                const I32 level,
                                                vector<U64>& foundAtomIDsInOut,
                                                const bool lock) {
    if (level > s_maxHeaderRecursionLevel) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INCLUD_LIMIT")));
    }

    size_t lineNumber = 1;
    boost::smatch matches;

    string line;
    eastl::string output, includeString;
    istringstream input(source.c_str());

    const boost::regex searchPatern = boost::regex{ Paths::g_includePattern.c_str() };

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
                               boost::regex_search(line, matches, searchPatern);
        if (!isInclude) {
            output.append(line.c_str());
        } else {
            const ResourcePath includeFile = ResourcePath(Util::Trim(matches[1].str()));
            foundAtomIDsInOut.push_back(_ID(includeFile.c_str()));

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
                includeString = ShaderFileRead(shaderAtomLocationPrefix[to_U32(typeIndex)], includeFile, true, foundAtomIDsInOut, wasParsed).c_str();
            } else {
                includeString = ShaderFileReadLocked(shaderAtomLocationPrefix[to_U32(typeIndex)], includeFile, true, foundAtomIDsInOut, wasParsed).c_str();
            }
            if (includeString.empty()) {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NO_INCLUDE_FILE")), name.c_str(), lineNumber, includeFile.c_str());
            }
            if (wasParsed) {
                output.append(includeString);
            } else {
                output.append(PreprocessIncludes(name, includeString, level + 1, foundAtomIDsInOut, lock));
            }
        }

        output.append("\n");
        ++lineNumber;
    }

    return output;
}

/// Open the file found at 'filePath' matching 'atomName' and return it's source code
const string& ShaderProgram::ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<U64>& foundAtomIDsInOut, bool& wasParsed) {
    const U64 atomNameHash = _ID(atomName.c_str());
    // See if the atom was previously loaded and still in cache
    const AtomMap::iterator it = s_atoms.find(atomNameHash);
    
    // If that's the case, return the code from cache
    if (it != std::cend(s_atoms)) {
        const auto& atoms = s_atomIncludes[atomNameHash];
        for (const auto& atom : atoms) {
            foundAtomIDsInOut.push_back(_ID(atom.c_str()));
        }
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
        output = PreprocessIncludes(atomName, output, 0, foundAtomIDsInOut, false);
    }

    for (const auto& atom : atoms) {
        foundAtomIDsInOut.push_back(_ID(atom.c_str()));
    }

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
    static const boost::regex uniformPattern { R"(^\s*uniform\s+\s*([^),^;^\s]*)\s+([^),^;^\s]*\[*\s*\]*)\s*(?:=*)\s*(?:\d*.*)\s*(?:;+))" };

    eastl::string ret;
    ret.reserve(source.size());

    string line;
    boost::smatch matches;
    istringstream input(source.c_str());
    while (std::getline(input, line)) {
        if (Util::BeginsWith(line, "uniform", true) &&
            boost::regex_search(line, matches, uniformPattern))
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

void ShaderProgram::ParseGLSLSource(Reflection::Data& reflectionDataInOut, LoadData& dataInOut, const bool targetVulkan, const bool reloadExisting) {
    if (dataInOut._codeSource == ShaderProgram::LoadData::SourceCodeSource::SOURCE_FILES) {
        QueueShaderWriteToFile(dataInOut._sourceCodeGLSL.c_str(), dataInOut._fileName);
    }
    if (dataInOut._codeSource != ShaderProgram::LoadData::SourceCodeSource::SOURCE_FILES) {
        STUBBED("ToDo: Add shader loading from SPIRV sources");
    }

    vk::ShaderStageFlagBits type = vk::ShaderStageFlagBits::eVertex;

    switch (dataInOut._type) {
        default:
        case ShaderType::VERTEX:            type = vk::ShaderStageFlagBits::eVertex;                 break;
        case ShaderType::TESSELLATION_CTRL: type = vk::ShaderStageFlagBits::eTessellationControl;    break;
        case ShaderType::TESSELLATION_EVAL: type = vk::ShaderStageFlagBits::eTessellationEvaluation; break;
        case ShaderType::GEOMETRY:          type = vk::ShaderStageFlagBits::eGeometry;               break;
        case ShaderType::FRAGMENT:          type = vk::ShaderStageFlagBits::eFragment;               break;
        case ShaderType::COMPUTE:           type = vk::ShaderStageFlagBits::eCompute;                break;
    };
    const ResourcePath spvPath = Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::g_SPIRVShaderLoc;
    const ResourcePath spvTarget{ DecorateFileName(dataInOut._fileName) + "." + Paths::Shaders::g_SPIRVExt };
    const ResourcePath spvTargetReflect{ spvTarget + ".refl" };

    bool usedCache = false;
    if (!reloadExisting && LoadReflectionData(spvPath, spvTargetReflect, reflectionDataInOut)) {
        vector<Byte> tempData;
        if (readFile(spvPath, spvTarget, tempData, FileType::BINARY) == FileError::NONE) {
            dataInOut._sourceCodeSpirV.resize(tempData.size() / sizeof(U32));
            memcpy(dataInOut._sourceCodeSpirV.data(), tempData.data(), tempData.size());
            usedCache = true;
        }
    }
   
    if (!usedCache) {
        if (!SpirvHelper::GLSLtoSPV(type, dataInOut._sourceCodeGLSL.c_str(), dataInOut._sourceCodeSpirV, targetVulkan, reflectionDataInOut) || dataInOut._sourceCodeSpirV.empty()) {
            Console::errorfn(Locale::Get(_ID("ERROR_SHADER_CONVERSION_SPIRV_FAILED")), dataInOut._fileName.c_str());
            dataInOut._sourceCodeSpirV.clear();
        } else {
            if (SaveReflectionData(spvPath, spvTargetReflect, reflectionDataInOut)) {
                if (writeFile(spvPath, ResourcePath(spvTarget), dataInOut._sourceCodeSpirV.data(), dataInOut._sourceCodeSpirV.size() * sizeof(U32), FileType::BINARY) != FileError::NONE) {
                    Console::errorfn(Locale::Get(_ID("ERROR_SHADER_SAVE_SPIRV_FAILED")), dataInOut._fileName.c_str());
                }
            }
        }
    }
}

bool ShaderProgram::SaveReflectionData(const ResourcePath& path, const ResourcePath& file, const Reflection::Data& reflectionDataIn) {
    ByteBuffer buffer;
    buffer << BYTE_BUFFER_VERSION;
    buffer << reflectionDataIn._targetBlockName;
    buffer << reflectionDataIn._blockSize;
    buffer << reflectionDataIn._blockMembers.size();
    for (const auto& member : reflectionDataIn._blockMembers) {
        buffer << std::string(member._name.c_str());
        buffer << to_base(member._type);
        buffer << member._offset;
        buffer << member._arrayInnerSize;
        buffer << member._arrayOuterSize;
        buffer << member._vectorDimensions;
        buffer << member._matrixDimensions.x;
        buffer << member._matrixDimensions.y;
    }

    return buffer.dumpToFile(path.c_str(), file.c_str());
}

bool ShaderProgram::LoadReflectionData(const ResourcePath& path, const ResourcePath& file, Reflection::Data& reflectionDataOut) {
    ByteBuffer buffer;
    if (buffer.loadFromFile(path.c_str(), file.c_str())) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        buffer >> tempVer;
        if (tempVer == BYTE_BUFFER_VERSION) {
            buffer >> reflectionDataOut._targetBlockName;
            buffer >> reflectionDataOut._blockSize;

            size_t memberCount = 0u;
            buffer >> memberCount;
            reflectionDataOut._blockMembers.reserve(memberCount);

            std::string tempStr;
            std::underlying_type_t<GFX::PushConstantType> tempType{0u};
            
            for (size_t i = 0; i < memberCount; ++i) {
                Reflection::BlockMember& tempMember = reflectionDataOut._blockMembers.emplace_back();
                buffer >> tempStr;
                tempMember._name = tempStr.c_str();
                buffer >> tempType;
                tempMember._type = static_cast<GFX::PushConstantType>(tempType);
                buffer >> tempMember._offset;
                buffer >> tempMember._arrayInnerSize;
                buffer >> tempMember._arrayOuterSize;
                buffer >> tempMember._vectorDimensions;
                buffer >> tempMember._matrixDimensions.x;
                buffer >> tempMember._matrixDimensions.y;
            }
            return true;
        }
    }

    return false;
}

bool ShaderProgram::reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting) {
    const auto g_cmp = [](const ShaderProgram::UniformDeclaration& lhs, const ShaderProgram::UniformDeclaration& rhs) {
        const I32 lhsPriority = g_TypePriority(_ID(lhs._type.c_str()));
        const I32 rhsPriority = g_TypePriority(_ID(rhs._type.c_str()));
        if (lhsPriority != rhsPriority) {
            return lhsPriority < rhsPriority;
        }

        return lhs._name < rhs._name;
    };

    setGLSWPath(reloadExisting);
    _uniformBlockBuffers.clear();
    _usedAtomIDs.resize(0);

    for (const ShaderModuleDescriptor& shaderDescriptor : _descriptor._modules) {
        const U64 fileHash = _ID(shaderDescriptor._sourceFile.data());
        fileData[fileHash]._modules.push_back(shaderDescriptor);
        ShaderModuleDescriptor& newDescriptor = fileData[fileHash]._modules.back();
        newDescriptor._defines.insert(end(newDescriptor._defines), begin(_descriptor._globalDefines), end(_descriptor._globalDefines));
    }

    U32 blockOffset = 0u;
    for (auto& [fileHash, loadDataPerFile] : fileData) {
        eastl::set<ShaderProgram::UniformDeclaration, decltype(g_cmp)> stageUniforms(g_cmp);

        assert(!loadDataPerFile._modules.empty());

        for (const ShaderModuleDescriptor& data : loadDataPerFile._modules) {
            const ShaderType type = data._moduleType;
            assert(type != ShaderType::COUNT);

            ShaderProgram::LoadData& stageData = loadDataPerFile._loadData._data.emplace_back();
            stageData._type = data._moduleType;
            stageData._name = Str256(data._sourceFile.substr(0, data._sourceFile.find_first_of(".")));
            stageData._name.append(".");
            stageData._name.append(Names::shaderTypes[to_U8(type)]);
            if (!data._variant.empty()) {
                stageData._name.append("." + data._variant);
            }
            stageData._definesHash = DefinesHash(data._defines);
            stageData._fileName = Util::StringFormat("%s.%zu.%s", stageData._name, stageData._definesHash, shaderAtomExtensionName[to_U8(type)]);

            loadSourceCode(data._defines, reloadExisting, stageData);
            if (!stageData._uniforms.empty()) {
                stageUniforms.insert(begin(stageData._uniforms), end(stageData._uniforms));
            }

            if (!loadDataPerFile._programName.empty()) {
                loadDataPerFile._programName.append("-");
            }
            loadDataPerFile._programName.append(stageData._fileName);

            _usedAtomIDs.emplace_back(_ID(data._sourceFile.c_str()));

            if (stageData._sourceCodeGLSL.empty() && stageData._sourceCodeSpirV.empty()) {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        loadDataPerFile._loadData._reflectionData._targetBlockBindingIndex = blockOffset++;
        loadDataPerFile._loadData._reflectionData._targetBlockName = Util::StringFormat("dvd_UniformBlock_%lld", loadDataPerFile._loadData._reflectionData._targetBlockBindingIndex);
        loadDataPerFile._loadData._reflectionData._targetBlockBindingIndex += to_U32(ShaderBufferLocation::UNIFORM_BLOCK);

        if (!stageUniforms.empty()) {
            string& uniformBlock = loadDataPerFile._loadData._uniformBlock;

            uniformBlock = _context.renderAPI() == RenderAPI::OpenGL ? "layout( " : "layout( set = 0, ";
            uniformBlock.append("binding = %d, std140 ) uniform %s {");

            for (const UniformDeclaration& uniform : stageUniforms) {
                uniformBlock.append(Util::StringFormat("\n    %s %s;", uniform._type.c_str(), uniform._name.c_str()));
            }
            uniformBlock.append(Util::StringFormat("\n} %s;", UNIFORM_BLOCK_NAME));

            for (const UniformDeclaration& uniform : stageUniforms) {
                const auto rawName = uniform._name.substr(0, uniform._name.find_first_of("[")).to_string();
                uniformBlock.append(Util::StringFormat("\n#define %s %s.%s", rawName.c_str(), UNIFORM_BLOCK_NAME, rawName.c_str()));
            }

            uniformBlock = Util::StringFormat(uniformBlock, loadDataPerFile._loadData._reflectionData._targetBlockBindingIndex, loadDataPerFile._loadData._reflectionData._targetBlockName.c_str());
        }

        for (LoadData& it : loadDataPerFile._loadData._data) {
            if (it._codeSource == ShaderProgram::LoadData::SourceCodeSource::SOURCE_FILES) {
                Util::ReplaceStringInPlace(it._sourceCodeGLSL, "//_CUSTOM_UNIFORMS_\\", loadDataPerFile._loadData._uniformBlock);
            }
            ParseGLSLSource(loadDataPerFile._loadData._reflectionData, it, false, reloadExisting);
        }

        initUniformUploader(loadDataPerFile);
    }

    return true;
}

void ShaderProgram::initUniformUploader(const PerFileShaderData& shaderFileData) {
    const ShaderLoadData& loadData = shaderFileData._loadData;

    if (!loadData._reflectionData._blockMembers.empty()) {
        UniformBlockUploaderDescriptor descriptor{};
        descriptor._parentShaderName = shaderFileData._programName.c_str();
        descriptor._reflectionData = loadData._reflectionData;
        _uniformBlockBuffers.emplace_back(_context, descriptor);
    }
}

void ShaderProgram::uploadPushConstants(const PushConstants& constants) {
    OPTICK_EVENT()

    for (auto& blockBuffer : _uniformBlockBuffers) {
        for (const GFX::PushConstant& constant : constants.data()) {
            blockBuffer.uploadPushConstant(constant);
        }
        blockBuffer.commit();
    }
}

void ShaderProgram::preparePushConstants() {
    for (auto& blockBuffer : _uniformBlockBuffers) {
        blockBuffer.prepare();
    }
}

void ShaderProgram::QueueShaderWriteToFile(const string& sourceCode, const Str256& fileName) {
    g_sDumpToFileQueue.enqueue({ fileName, sourceCode });
}

void ShaderProgram::loadSourceCode(const ModuleDefines& defines, bool reloadExisting, LoadData& loadDataInOut) {
    auto& glslCodeOut = loadDataInOut._sourceCodeGLSL;
    auto& spirvCodeOut = loadDataInOut._sourceCodeSpirV;

    glslCodeOut.resize(0);
    spirvCodeOut.resize(0);

    if (s_useShaderTextCache && !reloadExisting) {
        ShaderFileRead(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationText,
                       ResourcePath(loadDataInOut._fileName),
                       glslCodeOut);
        loadDataInOut._codeSource = LoadData::SourceCodeSource::TEXT_CACHE;
    }
    if (glslCodeOut.empty()) {
        // Read code from SPIRV files
        loadDataInOut._codeSource = LoadData::SourceCodeSource::SPIRV_CACHE;
    }
    if (glslCodeOut.empty() && spirvCodeOut.empty()) {
        // Use GLSW to read the appropriate part of the effect file
        // based on the specified stage and properties
        const char* sourceCodeStr = glswGetShader(loadDataInOut._name.c_str());
        if (sourceCodeStr != nullptr) {
            glslCodeOut.append(sourceCodeStr);
        }

        // GLSW may fail for various reasons (not a valid effect stage, invalid name, etc)
        if (!glslCodeOut.empty()) {

            string header;
            for (const auto& [defineString, appendPrefix] : defines) {
                // Placeholders are ignored
                if (defineString == "DEFINE_PLACEHOLDER") {
                    continue;
                }

                // We manually add define dressing if needed
                header.append((appendPrefix ? "#define " : "") + defineString + '\n');
            }

            for (const auto& [defineString, appendPrefix] : defines) {
                // Placeholders are ignored
                if (!appendPrefix || defineString == "DEFINE_PLACEHOLDER") {
                    continue;
                }

                // We also add a comment so that we can check what defines we have set because
                // the shader preprocessor strips defines before sending the code to the GPU
                header.append("/*Engine define: [ " + defineString + " ]*/\n");
            }
            // And replace in place with our program's headers created earlier
            Util::ReplaceStringInPlace(glslCodeOut, "_CUSTOM_DEFINES__", header);
            glslCodeOut = PreprocessIncludes(ResourcePath(resourceName()), glslCodeOut, 0, _usedAtomIDs, true);
            glslCodeOut = Preprocessor::PreProcess(glslCodeOut, loadDataInOut._fileName.c_str());
            glslCodeOut = GatherUniformDeclarations(glslCodeOut, loadDataInOut._uniforms);

            loadDataInOut._codeSource = LoadData::SourceCodeSource::SOURCE_FILES;
        }
    }
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
    ScopedLock<Mutex> lock(s_programLock);
    for (const ShaderProgramMapEntry& entry : s_shaderPrograms) {
        if (entry._program != nullptr) {
            entry._program->onAtomChangeInternal(atomName, evt);
        }
    }
}

void ShaderProgram::onAtomChangeInternal(const std::string_view atomName, const FileUpdateEvent evt) {
    const U64 atomNameHash = _ID(string{ atomName }.c_str());

    for (const U64 atomID : _usedAtomIDs) {
        if (atomID == atomNameHash) {
            s_recompileQueue.push(this);
            break;
        }
    }
}

void ShaderProgram::setGLSWPath(const bool clearExisting) {
    // The context is thread_local so each call to this should be thread safe
    if (clearExisting) {
        glswClearCurrentContext();
    }
    glswSetPath((assetLocation() + "/" + Paths::Shaders::GLSL::g_GLSLShaderLoc).c_str(), ".glsl");
}
};
