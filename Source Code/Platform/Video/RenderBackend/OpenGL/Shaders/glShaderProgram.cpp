#include "stdafx.h"

#include "config.h"

#include "Headers/glShaderProgram.h"
#include "Core/Headers/PlatformContext.h"

#include "Platform/Video/RenderBackend/OpenGL/glsw/Headers/glsw.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/FileUpdateMonitor.h"
#include "Platform/File/Headers/FileWatcherManager.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Headers/PlatformRuntime.h"

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

namespace {
    constexpr size_t g_validationBufferMaxSize = 64 * 1024;

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

    UpdateListener g_sFileWatcherListener(
        [](const std::string_view atomName, const FileUpdateEvent evt) {
            glShaderProgram::OnAtomChange(atomName, evt);
        }
    );

    moodycamel::BlockingConcurrentQueue<BinaryDumpEntry> g_sShaderBinaryDumpQueue;
    moodycamel::BlockingConcurrentQueue<TextDumpEntry>   g_sDumpToFileQueue;
    moodycamel::BlockingConcurrentQueue<ValidationEntry> g_sValidationQueue;
    std::atomic_bool                                     g_newValidationQueueEntry;
}

void glShaderProgram::InitStaticData() {
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

    std::atomic_init(&g_newValidationQueueEntry, false);
}

void glShaderProgram::DestroyStaticData() {
    static BinaryDumpEntry temp = {};
    while(g_sShaderBinaryDumpQueue.try_dequeue(temp)) {
        NOP();
    }
}

void glShaderProgram::OnStartup() {
    if_constexpr (!Config::Build::IS_SHIPPING_BUILD) {
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
}

void glShaderProgram::OnShutdown() {
    FileWatcherManager::deallocateWatcher(s_shaderFileWatcherID);
    s_shaderFileWatcherID = -1;
}

void glShaderProgram::ProcessValidationQueue() {
    bool expected = true;
    if (g_newValidationQueueEntry.compare_exchange_strong(expected, false)) {
        
        ValidationEntry s_validationOutputCache;
        if (g_sValidationQueue.try_dequeue(s_validationOutputCache)) {
            glValidateProgramPipeline(s_validationOutputCache._handle);

            GLint status = 1;
            if (s_validationOutputCache._stageMask != UseProgramStageMask::GL_COMPUTE_SHADER_BIT) {
                glGetProgramPipelineiv(s_validationOutputCache._handle, GL_VALIDATE_STATUS, &status);
            }

            // we print errors in debug and in release, but everything else only in debug
            // the validation log is only retrieved if we request it. (i.e. in release,
            // if the shader is validated, it isn't retrieved)
            if (status == 0) {
                // Query the size of the log
                GLint length = 0;
                glGetProgramPipelineiv(s_validationOutputCache._handle, GL_INFO_LOG_LENGTH, &length);
                // If we actually have something in the validation log
                if (length > 1) {
                    string validationBuffer;
                    validationBuffer.resize(length);
                    glGetProgramPipelineInfoLog(s_validationOutputCache._handle, length, nullptr, &validationBuffer[0]);

                    // To avoid overflowing the output buffers (both CEGUI and Console), limit the maximum output size
                    if (validationBuffer.size() > g_validationBufferMaxSize) {
                        // On some systems, the program's disassembly is printed, and that can get quite large
                        validationBuffer.resize(std::strlen(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG"))) + g_validationBufferMaxSize);
                        // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                        validationBuffer.append(" ... ");
                    }
                    // Return the final message, whatever it may contain
                    Console::errorfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), validationBuffer.c_str());
                } else {
                    Console::errorfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ Couldn't retrieve info log! ]");
                }
            } else {
                Console::d_printfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ OK! ]");
            }
        }
    }
}

void glShaderProgram::DumpShaderTextCacheToDisk(const TextDumpEntry& entry) {
    if (!ShaderFileWrite(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationText, ResourcePath(entry._name), entry._sourceCode.c_str())) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void glShaderProgram::DumpShaderBinaryCacheToDisk(const BinaryDumpEntry& entry) {
    if (!glShader::DumpBinary(entry._handle, entry._name)) {
        DIVIDE_UNEXPECTED_CALL();
    }
}

void glShaderProgram::Idle(PlatformContext& platformContext) {
    OPTICK_EVENT();

    assert(Runtime::isMainThread());

    // One validation per Idle call
    ProcessValidationQueue();

    // Schedule all of the shader "dump to text file" operations
    static TextDumpEntry textOutputCache;
    while(g_sDumpToFileQueue.try_dequeue(textOutputCache)) {
        DIVIDE_ASSERT(!textOutputCache._name.empty() && !textOutputCache._sourceCode.empty());
        Start(*CreateTask([cache = MOV(textOutputCache)](const Task &) { DumpShaderTextCacheToDisk(cache); }), platformContext.taskPool(TaskPoolType::LOW_PRIORITY));
    }

    // Schedule all of the shader "dump to binary file" operations
    static BinaryDumpEntry binaryOutputCache;
    while(g_sShaderBinaryDumpQueue.try_dequeue(binaryOutputCache)) {
        Start(*CreateTask([cache = MOV(binaryOutputCache)](const Task&) { DumpShaderBinaryCacheToDisk(cache); }), platformContext.taskPool(TaskPoolType::LOW_PRIORITY));
    }
}

glShaderProgram::glShaderProgram(GFXDevice& context,
                                 const size_t descriptorHash,
                                 const Str256& name,
                                 const Str256& assetName,
                                 const ResourcePath& assetLocation,
                                 const ShaderProgramDescriptor& descriptor,
                                 const bool asyncLoad)
    : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, asyncLoad),
      glObject(glObjectType::TYPE_SHADER_PROGRAM, context)
{
}

glShaderProgram::~glShaderProgram()
{
    unload();
    GL_API::DeleteShaderPipelines(1, &_handle);
}

bool glShaderProgram::unload() {
    // Remove every shader attached to this program
    eastl::for_each(begin(_shaderStage),
                    end(_shaderStage),
                    [](glShader* shader) {
                        glShader::removeShader(shader);
                    });
    _shaderStage.clear();

     return ShaderProgram::unload();
}

ShaderResult glShaderProgram::rebindStages() {
    assert(isValid());

    for (glShader* shader : _shaderStage) {
        const ShaderResult ret = shader->uploadToGPU();
        if (ret != ShaderResult::OK) {
            return ret;
        }

        // If a shader exists for said stage, attach it
        glUseProgramStages(_handle, shader->stageMask(), shader->getProgramHandle());
    }

    return ShaderResult::OK;
}

void glShaderProgram::queueValidation() {
    if (_validationQueued) {
        _validationQueued = false;

        UseProgramStageMask stageMask = UseProgramStageMask::GL_NONE_BIT;
        for (const glShader* shader : _shaderStage) {

            if (!shader->valid()) {
                continue;
            }

            if (!shader->loadedFromBinary()) {
                g_sShaderBinaryDumpQueue.enqueue(BinaryDumpEntry{ shader->name(), shader->getProgramHandle() });
            }

            stageMask |= shader->stageMask();
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            g_sValidationQueue.enqueue({ resourceName(), _handle, stageMask });
            g_newValidationQueueEntry.store(true);
        }
    }
}

ShaderResult glShaderProgram::validatePreBind() {
    if (!isValid()) {
        OPTICK_EVENT();

        assert(getState() == ResourceState::RES_LOADED);
        glCreateProgramPipelines(1, &_handle);
        glObjectLabel(GL_PROGRAM_PIPELINE, _handle, -1, resourceName().c_str());

        const ShaderResult ret = rebindStages();
        if (ret == ShaderResult::OK) {
            _validationQueued = true;
        }

        return ret;
        
    }

    return ShaderResult::OK;
}

/// This should be called in the loading thread, but some issues are still present, and it's not recommended (yet)
void glShaderProgram::threadedLoad(const bool reloadExisting) {
    OPTICK_EVENT()

    if (!weak_from_this().expired()) {
        RegisterShaderProgram(std::dynamic_pointer_cast<ShaderProgram>(shared_from_this()).get());
    }

    // NULL shader means use shaderProgram(0), so bypass the normal loading routine
    if (getGUID() == ShaderProgram::NullShaderGUID()) {
        _handle = 0;
    } else {
        reloadShaders(reloadExisting);
    }
    // Pass the rest of the loading steps to the parent class
    if (!reloadExisting) {
        ShaderProgram::load();
    }
}

glShaderProgram::AtomUniformPair glShaderProgram::loadSourceCode(const Str128& stageName,
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

/// Creation of a new shader pipeline. Pass in a shader token and use glsw to load the corresponding effects
bool glShaderProgram::load() {
    if (_asyncLoad) {
        Start(*CreateTask([this](const Task &) {threadedLoad(false); }), _context.context().taskPool(TaskPoolType::HIGH_PRIORITY));
    } else {
        threadedLoad(false);
    }

    return true;
}

bool glShaderProgram::reloadShaders(const bool reloadExisting) {
    static const auto g_cmp = [](const ShaderProgram::UniformDeclaration& lhs, const ShaderProgram::UniformDeclaration& rhs) {
        const I32 lhsPriority = g_TypePriority(_ID(lhs._type.c_str()));
        const I32 rhsPriority = g_TypePriority(_ID(rhs._type.c_str()));
        if (lhsPriority != rhsPriority) {
            return lhsPriority < rhsPriority;
        }

        return lhs._name < rhs._name;
    };

    using UniformList = eastl::set<ShaderProgram::UniformDeclaration, decltype(g_cmp)>;
    UniformList allUniforms(g_cmp);

    //glswClearCurrentContext();
    glswSetPath((assetLocation() + "/" + Paths::Shaders::GLSL::g_parentShaderLoc).c_str(), ".glsl");

    U64 batchCounter = 0;
    hashMap<U64, vector<ShaderModuleDescriptor>> modulesByFile;
    for (const ShaderModuleDescriptor& shaderDescriptor : _descriptor._modules) {
        const U64 fileHash = shaderDescriptor._batchSameFile ? _ID(shaderDescriptor._sourceFile.data()) : batchCounter++;
        vector<ShaderModuleDescriptor>& modules = modulesByFile[fileHash];
        modules.push_back(shaderDescriptor);
    }

    U8 uniformIndex = 0u;
    for (const auto& it : modulesByFile) {
        const vector<ShaderModuleDescriptor>& modules = it.second;
        assert(!modules.empty());

        glShader::ShaderLoadData loadData;

        Str256 programName = modules.front()._sourceFile.data();
        programName = Str256(programName.substr(0, programName.find_first_of(".")));

        bool hasData = false;
        for (const ShaderModuleDescriptor& shaderDescriptor : modules) {
            const ShaderType type = shaderDescriptor._moduleType;
            assert(type != ShaderType::COUNT);

            const size_t definesHash = DefinesHash(shaderDescriptor._defines);

            const U8 shaderIdx = to_U8(type);
            string header;
            for (const auto& [defineString, appendPrefix] : shaderDescriptor._defines) {
                // Placeholders are ignored
                if (defineString != "DEFINE_PLACEHOLDER") {
                    // We manually add define dressing if needed
                    header.append(appendPrefix ? "#define " : "");
                    header.append(defineString + "\n");

                    if (appendPrefix) {
                        // We also add a comment so that we can check what defines we have set because
                        // the shader preprocessor strips defines before sending the code to the GPU
                        header.append("/*Engine define: [ " + defineString + " ]*/\n");
                    }
                }
            }

            programName.append("-");
            programName.append(Names::shaderTypes[shaderIdx]);

            if (!shaderDescriptor._variant.empty()) {
                programName.append("." + shaderDescriptor._variant);
            }
            
            if (!shaderDescriptor._defines.empty()) {
                programName.append(Util::StringFormat(".%zu", definesHash));
            }

            glShader::LoadData& stageData = loadData._data[shaderIdx];
            stageData._type = shaderDescriptor._moduleType;
            stageData._name = Str128(string(shaderDescriptor._sourceFile.data()).substr(0, shaderDescriptor._sourceFile.find_first_of(".")));
            stageData._name.append(".");
            stageData._name.append(Names::shaderTypes[shaderIdx]);

            if (!shaderDescriptor._variant.empty()) {
                stageData._name.append("." + shaderDescriptor._variant);
            }

            eastl::string sourceCode;
            const AtomUniformPair programData = loadSourceCode(stageData._name,
                                                               shaderAtomExtensionName[shaderIdx],
                                                               header,
                                                               definesHash,
                                                               reloadExisting,
                                                               stageData._fileName,
                                                               sourceCode);

            if (sourceCode.empty()) {
                continue;
            }

            stageData._hasUniforms = !programData._uniforms.empty();
            if (stageData._hasUniforms) {
                allUniforms.insert(begin(programData._uniforms), end(programData._uniforms));
            }

            stageData.atoms.insert(_ID(shaderDescriptor._sourceFile.data()));
            for (const auto& atomIt : programData._atoms) {
                stageData.atoms.insert(_ID(atomIt.c_str()));
            }
            stageData.sourceCode.push_back(sourceCode);
            hasData = true;
        }

        if (!hasData) {
            continue;
        }

        if (!allUniforms.empty()) {
            _hasUniformBlockBuffer = true;
            loadData._uniformBlock = "layout(binding = %d, std140) uniform %s {";

            for (const UniformDeclaration& uniform : allUniforms) {
                loadData._uniformBlock.append(Util::StringFormat(g_useUniformConstantBuffer ? "\n    %s %s;" : "\n    %s UBM%s;", uniform._type.c_str(), uniform._name.c_str()));
            }
            loadData._uniformBlock.append("\n};");
            loadData._uniformIndex = uniformIndex++;

            if_constexpr (!g_useUniformConstantBuffer) {
                for (const UniformDeclaration& uniform : allUniforms) {
                    loadData._uniformBlock.append(Util::StringFormat("\nuniform %s %s;", uniform._type.c_str(), uniform._name.c_str()));
                }
            }
            allUniforms.clear();
        }

        if (reloadExisting) {
            const U64 targetNameHash = _ID(programName.c_str());
            for (glShader* tempShader : _shaderStage) {
                if (tempShader->nameHash() == targetNameHash) {
                    glShader::loadShader(tempShader, false, loadData);
                    _validationQueued = rebindStages() == ShaderResult::OK;
                    break;
                }
            }
        } else {
            glShader* shader = glShader::getShader(programName);
            if (shader == nullptr) {
                shader = glShader::loadShader(_context, programName, loadData);
                assert(shader != nullptr);
            } else {
                shader->AddRef();
                Console::d_printfn(Locale::Get(_ID("SHADER_MANAGER_GET_INC")), shader->name().c_str(), shader->GetRef());
            }
            _shaderStage.push_back(shader);
        }
    }

    return !_shaderStage.empty();
}

void glShaderProgram::QueueShaderWriteToFile(const string& sourceCode, const Str256& fileName) {
    g_sDumpToFileQueue.enqueue({ fileName, sourceCode });
}

bool glShaderProgram::recompile(const bool force, bool& skipped) {
    if (!ShaderProgram::recompile(force, skipped)) {
        return false;
    }

    if (!isValid()) {
        return false;
    }

    if (force || shouldRecompile()) {
        shouldRecompile(false);
        skipped = false;

        if (getGUID() == ShaderProgram::NullShaderGUID()) {
            _handle = 0u;
            return true;
        }

        // Remember bind state and unbind it if needed
        const bool wasBound = GL_API::getStateTracker()._activeShaderPipeline == _handle;
        if (wasBound) {
            GL_API::getStateTracker().setActiveShaderPipeline(0u);
        }
        threadedLoad(true);
        // Restore bind state
        if (wasBound) {
            bind();
        }
    }

    return true;
}

/// Check every possible combination of flags to make sure this program can be used for rendering
bool glShaderProgram::isValid() const noexcept {
    // null shader is a valid shader
    return _handle != GLUtil::k_invalidObjectID;
}

/// Bind this shader program
ShaderResult glShaderProgram::bind() {
    OPTICK_EVENT()

    // If the shader isn't ready or failed to link, stop here
    const ShaderResult ret = validatePreBind();
    if (ret != ShaderResult::OK) {
        return ret;
    }

    // Set this program as the currently active one
    if (GL_API::getStateTracker().setActiveShaderPipeline(_handle)) {
        // All of this needs to be run on an actual bind operation. If we are already bound, we assume we did all this
        queueValidation();
        for (const glShader* shader : _shaderStage) {
            if (shader->valid()) {
                shader->prepare();
            }
            if (shader->shouldRecompile()) {
                shouldRecompile(true);
            }
        }
    }

    return ShaderResult::OK;
}

void glShaderProgram::uploadPushConstants(const PushConstants& constants) {
    OPTICK_EVENT()

    assert(isValid());
    for (const glShader* shader : _shaderStage) {
        if (shader->valid()) {
            shader->uploadPushConstants(constants);
        }
    }
}

eastl::string glShaderProgram::GatherUniformDeclarations(const eastl::string & source, vector<UniformDeclaration>& foundUniforms) {
    static const std::regex uniformPattern { R"(^\s*uniform\s+\s*([^),^;^\s]*)\s+([^),^;^\s]*\[*\s*\]*)\s*(?:=*)\s*(?:\d*.*)\s*(?:;+))" };

    eastl::string ret;
    ret.reserve(source.size());

    string line;
    std::smatch matches;
    istringstream input(source.c_str());
    while (std::getline(input, line)) {
        if (std::regex_search(line, matches, uniformPattern)) {
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

eastl::string glShaderProgram::PreprocessIncludes(const ResourcePath& name,
                                                  const eastl::string& source,
                                                  const GLint level,
                                                  vector<ResourcePath>& foundAtoms,
                                                  const bool lock) {
    if (level > 32) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_INCLUD_LIMIT")));
    }

    size_t lineNumber = 1;
    std::smatch matches;

    string line;
    eastl::string output, includeString;
    istringstream input(source.c_str());

    while (std::getline(input, line)) {
        if (!std::regex_search(line, matches, Paths::g_includePattern)) {
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

const string& glShaderProgram::ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed) {
    ScopedLock<SharedMutex> w_lock(s_atomLock);
    return ShaderFileReadLocked(filePath, atomName, recurse, foundAtoms, wasParsed);
}

/// Open the file found at 'filePath' matching 'atomName' and return it's source code
const string& glShaderProgram::ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed) {
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

bool glShaderProgram::ShaderFileRead(const ResourcePath& filePath, const ResourcePath& fileName, eastl::string& sourceCodeOut) {
    return readFile(filePath, ResourcePath(decorateFileName(fileName.str())), sourceCodeOut, FileType::TEXT) == FileError::NONE;
}

bool glShaderProgram::ShaderFileWrite(const ResourcePath& filePath, const ResourcePath& fileName, const char* sourceCode) {
    return writeFile(filePath, ResourcePath(decorateFileName(fileName.str())), sourceCode, strlen(sourceCode), FileType::TEXT) == FileError::NONE;
}

void glShaderProgram::OnAtomChange(const std::string_view atomName, const FileUpdateEvent evt) {
    DIVIDE_ASSERT(evt != FileUpdateEvent::COUNT);

    // Do nothing if the specified file is "deleted". We do not want to break running programs
    if (evt == FileUpdateEvent::DELETE) {
        return;
    }
    // ADD and MODIFY events should get processed as usual

    const U64 atomNameHash = _ID(string{ atomName }.c_str());
    {
        // Clear the atom from the cache
        ScopedLock<SharedMutex> w_lock(s_atomLock);
        if (s_atoms.erase(atomNameHash) == 1) {
            NOP();
        }
    }

    //Get list of shader programs that use the atom and rebuild all shaders in list;
    SharedLock<SharedMutex> r_lock(s_programLock);
    for (const auto& [_, programEntry] : s_shaderPrograms) {

        const auto& shaders = static_cast<glShaderProgram*>(programEntry.first)->_shaderStage;

        for (glShader* shader : shaders) {
            for (const auto& it : shader->_loadData._data) {
                for (const U64 atomHash : it.atoms) {
                    if (atomHash == atomNameHash) {
                        s_recompileQueue.push(programEntry.first);
                        goto NEXT_SHADER_PROGRAM;
                    }
                }
            }
        }
        NEXT_SHADER_PROGRAM:
        NOP();
    }
}

};
