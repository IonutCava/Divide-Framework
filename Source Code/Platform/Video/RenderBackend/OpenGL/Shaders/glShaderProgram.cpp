#include "stdafx.h"

#include "Headers/glShaderProgram.h"
#include "Headers/glShader.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

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

    moodycamel::BlockingConcurrentQueue<BinaryDumpEntry> g_sShaderBinaryDumpQueue;
    moodycamel::BlockingConcurrentQueue<ValidationEntry> g_sValidationQueue;
    std::atomic_bool                                     g_newValidationQueueEntry;
}

void glShaderProgram::InitStaticData() {
     std::atomic_init(&g_newValidationQueueEntry, false);
}

void glShaderProgram::DestroyStaticData() {
    static BinaryDumpEntry temp = {};
    while(g_sShaderBinaryDumpQueue.try_dequeue(temp)) {
        NOP();
    }
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
    if (GL_API::GetStateTracker()._activeShaderPipeline == _handle) {
        if (GL_API::GetStateTracker().setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    glDeleteProgramPipelines(1, &_handle);
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
    assert(_handle != GLUtil::k_invalidObjectID);

    for (glShader* shader : _shaderStage) {
        const ShaderResult ret = shader->uploadToGPU(_handle);
        if (ret != ShaderResult::OK) {
            return ret;
        }

        // If a shader exists for said stage, attach it
        glUseProgramStages(_handle, shader->stageMask(), shader->getProgramHandle());
    }

    return ShaderResult::OK;
}

void glShaderProgram::processValidation() {
    if (!_validationQueued) {
        return;
    }

    _validationQueued = false;

    UseProgramStageMask stageMask = UseProgramStageMask::GL_NONE_BIT;
    for (glShader* shader : _shaderStage) {

        if (!shader->valid()) {
            continue;
        }

        if (!shader->loadedFromBinary()) {
            g_sShaderBinaryDumpQueue.enqueue(BinaryDumpEntry{ shader->name(), shader->getProgramHandle() });
        }

        shader->onParentValidation();

        stageMask |= shader->stageMask();
    }

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        g_sValidationQueue.enqueue({ resourceName(), _handle, stageMask });
        g_newValidationQueueEntry.store(true);
    }
}

ShaderResult glShaderProgram::validatePreBind(const bool rebind) {
    OPTICK_EVENT();

    if (!_stagesBound && rebind) {
        glBufferLockManager::DriverBusy(true);
        assert(getState() == ResourceState::RES_LOADED);
        ShaderResult ret = ShaderResult::OK;
        if (_handle == GLUtil::k_invalidObjectID) {
            if (getGUID() == ShaderProgram::NullShaderGUID()) {
                _handle = 0u;
            } else {
                glCreateProgramPipelines(1, &_handle);
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    glObjectLabel(GL_PROGRAM_PIPELINE, _handle, -1, resourceName().c_str());
                }
            }
        }
        if (rebind) {
            ret = rebindStages();
            if (ret == ShaderResult::OK) {
                _validationQueued = true;
                _stagesBound = true;
            }
        }
        glBufferLockManager::DriverBusy(false);

        return ret;
        
    }

    return ShaderResult::OK;
}

/// This should be called in the loading thread, but some issues are still present, and it's not recommended (yet)
void glShaderProgram::threadedLoad(const bool reloadExisting) {
    OPTICK_EVENT()

    _stagesBound = false;
    assert(reloadExisting || _handle == GLUtil::k_invalidObjectID);
    // NULL shader means use shaderProgram(0), so bypass the normal loading routine
    if (getGUID() != ShaderProgram::NullShaderGUID()) {
        reloadShaders(reloadExisting);
    }

    // Pass the rest of the loading steps to the parent class
    ShaderProgram::threadedLoad(reloadExisting);
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

    setGLSWPath(reloadExisting);

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
                    glShader::loadShader(tempShader, false, MOV(loadData));
                    _stagesBound = false;
                    break;
                }
            }
        } else {
            glShader* shader = glShader::getShader(programName);
            if (shader == nullptr) {
                shader = glShader::loadShader(_context, programName, MOV(loadData));
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

bool glShaderProgram::recompile(bool& skipped) {
    if (!ShaderProgram::recompile(skipped)) {
        return false;
    }

    if (validatePreBind(false) != ShaderResult::OK) {
        return false;
    }

    skipped = false;
    if (getGUID() == ShaderProgram::NullShaderGUID()) {
        _handle = 0u;
        return true;
    }

    // Remember bind state and unbind it if needed
    const bool wasBound = GL_API::GetStateTracker()._activeShaderPipeline == _handle;
    if (wasBound) {
        if (GL_API::GetStateTracker().setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }
    threadedLoad(true);
    // Restore bind state
    if (wasBound) {
        bind();
    }

    return true;
}

/// Bind this shader program
ShaderResult glShaderProgram::bind() {
    OPTICK_EVENT()

    // If the shader isn't ready or failed to link, stop here
    const ShaderResult ret = validatePreBind(true);
    if (ret != ShaderResult::OK) {
        return ret;
    }

    // Set this program as the currently active one
    if (GL_API::GetStateTracker().setActiveShaderPipeline(_handle) == GLStateTracker::BindResult::JUST_BOUND) {
        // All of this needs to be run on an actual bind operation. If we are already bound, we assume we did all this
        processValidation();
        for (const glShader* shader : _shaderStage) {
            shader->prepare();
        }
    }

    return ShaderResult::OK;
}

void glShaderProgram::uploadPushConstants(const PushConstants& constants) {
    OPTICK_EVENT()

    assert(_handle != GLUtil::k_invalidObjectID);
    for (const glShader* shader : _shaderStage) {
        if (shader->valid()) {
            shader->uploadPushConstants(constants);
        }
    }
}
void glShaderProgram::onAtomChangeInternal(const std::string_view atomName, const FileUpdateEvent evt) {
    ShaderProgram::onAtomChangeInternal(atomName, evt);

    const U64 atomNameHash = _ID(string{ atomName }.c_str());

    for (glShader* shader : _shaderStage) {
        for (const glShader::LoadData& it : shader->_loadData._data) {
            for (const U64 atomHash : it.atoms) {
                if (atomHash == atomNameHash) {
                    s_recompileQueue.push(this);
                    break;
                }
            }
        }
    }
}

};
