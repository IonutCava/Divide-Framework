#include "stdafx.h"

#include "Headers/glShaderProgram.h"
#include "Headers/glShader.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {
    constexpr size_t g_validationBufferMaxSize = 64 * 1024;

    moodycamel::BlockingConcurrentQueue<BinaryDumpEntry> g_sShaderBinaryDumpQueue;
    moodycamel::BlockingConcurrentQueue<ValidationEntry> g_sValidationQueue;

    SharedMutex      g_deletionSetLock;
    std::set<GLuint> g_deletionSet;
}
void glShaderProgram::InitStaticData() {
    glShader::InitStaticData();
}

void glShaderProgram::DestroyStaticData() {
    glShader::DestroyStaticData();
}

void glShaderProgram::Idle(PlatformContext& platformContext) {
    OPTICK_EVENT();

    assert(Runtime::isMainThread());

    // One validation per Idle call
    ProcessValidationQueue();

    // Schedule all of the shader "dump to binary file" operations
    static BinaryDumpEntry binaryOutputCache;
    if (g_sShaderBinaryDumpQueue.try_dequeue(binaryOutputCache)) {
        DumpShaderBinaryCacheToDisk(binaryOutputCache);
    }
}

void glShaderProgram::ProcessValidationQueue() {
    static ValidationEntry s_validationOutputCache;

    if (g_sValidationQueue.try_dequeue(s_validationOutputCache)) {
        {
            SharedLock<SharedMutex> w_lock(g_deletionSetLock);
            if (g_deletionSet.find(s_validationOutputCache._handle) != std::cend(g_deletionSet)) {
                return;
            }
        }
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


void glShaderProgram::DumpShaderBinaryCacheToDisk(const BinaryDumpEntry& entry) {
    {
        SharedLock<SharedMutex> w_lock(g_deletionSetLock);
        if (g_deletionSet.find(entry._handle) != std::cend(g_deletionSet)) {
            return;
        }
    }

    if (!glShader::DumpBinary(entry._handle, entry._name)) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_DUMP_BINARY"), entry._name.c_str()));
    }
}

glShaderProgram::glShaderProgram(GFXDevice& context,
                                 const size_t descriptorHash,
                                 const Str256& name,
                                 const Str256& assetName,
                                 const ResourcePath& assetLocation,
                                 const ShaderProgramDescriptor& descriptor)
    : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor),
      glObject(glObjectType::TYPE_SHADER_PROGRAM, context)
{
}

bool glShaderProgram::unload() {
    {
        ScopedLock<SharedMutex> w_lock(g_deletionSetLock);
        g_deletionSet.insert(_handle);
    }

    if (GL_API::GetStateTracker()->_activeShaderPipeline == _handle) {
        if (GL_API::GetStateTracker()->setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
            DIVIDE_UNEXPECTED_CALL();
        }
    }

    glDeleteProgramPipelines(1, &_handle);
    _handle = GLUtil::k_invalidObjectID;

    // Remove every shader attached to this program
    eastl::for_each(begin(_shaderStage),
                    end(_shaderStage),
                    [](glShader* shader) {
                        glShader::RemoveShader(shader);
                    });
    _shaderStage.clear();

     return ShaderProgram::unload();
}

void glShaderProgram::processValidation() {
    OPTICK_EVENT();

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
            g_sShaderBinaryDumpQueue.enqueue(BinaryDumpEntry{ shader->name(), shader->handle() });
        }

        shader->onParentValidation();

        stageMask |= shader->stageMask();
    }

    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        g_sValidationQueue.enqueue({ resourceName(), _handle, stageMask});
    }
}

ShaderResult glShaderProgram::validatePreBind(const bool rebind) {
    OPTICK_EVENT();

    if (!_stagesBound && rebind) {

        assert(getState() == ResourceState::RES_LOADED);
        ShaderResult ret = ShaderResult::OK;
        if (_handle == GLUtil::k_invalidObjectID) {
            glCreateProgramPipelines(1, &_handle);
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                glObjectLabel(GL_PROGRAM_PIPELINE, _handle, -1, resourceName().c_str());
            }
            // We can reuse previous handles
            ScopedLock<SharedMutex> w_lock(g_deletionSetLock);
            g_deletionSet.erase(_handle);
        }

        if (rebind) {
            assert(_handle != GLUtil::k_invalidObjectID);

            for (glShader* shader : _shaderStage) {
                ret = shader->uploadToGPU(_handle);
                if (ret != ShaderResult::OK) {
                    break;
                }

                // If a shader exists for said stage, attach it
                glUseProgramStages(_handle, shader->stageMask(), shader->handle());
            }

            if (ret == ShaderResult::OK) {
                _validationQueued = true;
                _stagesBound = true;
            }
        }

        return ret;
        
    }

    return ShaderResult::OK;
}

/// This should be called in the loading thread, but some issues are still present, and it's not recommended (yet)
void glShaderProgram::threadedLoad(const bool reloadExisting) {
    OPTICK_EVENT()

    assert(reloadExisting || _handle == GLUtil::k_invalidObjectID);
    hashMap<U64, PerFileShaderData> loadDataByFile{};
    reloadShaders(loadDataByFile, reloadExisting);

    // Pass the rest of the loading steps to the parent class
    ShaderProgram::threadedLoad(reloadExisting);
}

bool glShaderProgram::reloadShaders(hashMap<U64, PerFileShaderData>& fileData, const bool reloadExisting) {
    OPTICK_EVENT();

    if (ShaderProgram::reloadShaders(fileData, reloadExisting)) {
        _stagesBound = false;

        _shaderStage.clear();
        for (auto& [fileHash, loadDataPerFile] : fileData) {
            assert(!loadDataPerFile._modules.empty());

            glShader* shader = glShader::LoadShader(_context, loadDataPerFile._programName, loadDataPerFile._loadData);
            _shaderStage.push_back(shader);
        }

        return !_shaderStage.empty();
    }

    return false;
}

bool glShaderProgram::recompile(bool& skipped) {
    OPTICK_EVENT();

    if (!ShaderProgram::recompile(skipped)) {
        return false;
    }

    if (validatePreBind(false) != ShaderResult::OK) {
        return false;
    }

    skipped = false;

    // Remember bind state and unbind it if needed
    const bool wasBound = GL_API::GetStateTracker()->_activeShaderPipeline == _handle;
    if (wasBound) {
        if (GL_API::GetStateTracker()->setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED) {
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
    OPTICK_EVENT();

    // If the shader isn't ready or failed to link, stop here
    const ShaderResult ret = validatePreBind(true);
    if (ret != ShaderResult::OK) {
        return ret;
    }

    // Set this program as the currently active one
    if (GL_API::GetStateTracker()->setActiveShaderPipeline(_handle) == GLStateTracker::BindResult::JUST_BOUND) {
        // All of this needs to be run on an actual bind operation. If we are already bound, we assume we did all this
        processValidation();
        preparePushConstants();
    }

    return ShaderResult::OK;
}

};
