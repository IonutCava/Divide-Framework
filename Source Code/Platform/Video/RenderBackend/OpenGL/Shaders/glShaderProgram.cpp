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
    moodycamel::BlockingConcurrentQueue<ValidationEntry> g_sValidationQueue;

    SharedMutex      g_deletionSetLock;
    eastl::set<GLuint> g_deletionSet;
}

void glShaderProgram::Idle(PlatformContext& platformContext)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    assert(Runtime::isMainThread());

    // One validation per Idle call
    ProcessValidationQueue();
}

void glShaderProgram::ProcessValidationQueue()
{
    static ValidationEntry s_validationOutputCache;

    if (g_sValidationQueue.try_dequeue(s_validationOutputCache))
    {
        {
            SharedLock<SharedMutex> w_lock(g_deletionSetLock);
            if (g_deletionSet.find(s_validationOutputCache._handle) != std::cend(g_deletionSet))
            {
                return;
            }
        }
        assert(s_validationOutputCache._handle != GLUtil::k_invalidObjectID);

        glValidateProgramPipeline(s_validationOutputCache._handle);

        GLint status = 1;
        if (s_validationOutputCache._stageMask != UseProgramStageMask::GL_COMPUTE_SHADER_BIT)
        {
            glGetProgramPipelineiv(s_validationOutputCache._handle, GL_VALIDATE_STATUS, &status);
        }

        // we print errors in debug and in release, but everything else only in debug
        // the validation log is only retrieved if we request it. (i.e. in release,
        // if the shader is validated, it isn't retrieved)
        if (status == 0)
        {
            // Query the size of the log
            GLint length = 0;
            glGetProgramPipelineiv(s_validationOutputCache._handle, GL_INFO_LOG_LENGTH, &length);
            // If we actually have something in the validation log
            if (length > 1)
            {
                string validationBuffer;
                validationBuffer.resize(length);
                glGetProgramPipelineInfoLog(s_validationOutputCache._handle, length, nullptr, &validationBuffer[0]);

                // To avoid overflowing the output buffers (both CEGUI and Console), limit the maximum output size
                if (validationBuffer.size() > g_validationBufferMaxSize)
                {
                    // On some systems, the program's disassembly is printed, and that can get quite large
                    validationBuffer.resize(std::strlen(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG"))) + g_validationBufferMaxSize);
                    // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                    validationBuffer.append(" ... ");
                }
                // Return the final message, whatever it may contain
                Console::errorfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), validationBuffer.c_str());
            }
            else
            {
                Console::errorfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ Couldn't retrieve info log! ]");
            }
        }
        else
        {
            Console::d_printfn(Locale::Get(_ID("GLSL_VALIDATING_PROGRAM")), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ OK! ]");
        }
    }
}

glShaderProgram::glShaderProgram(GFXDevice& context,
                                 const size_t descriptorHash,
                                 const Str256& name,
                                 const Str256& assetName,
                                 const ResourcePath& assetLocation,
                                 const ShaderProgramDescriptor& descriptor,
                                 ResourceCache& parentCache)
    : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, parentCache)
{
}

glShaderProgram::~glShaderProgram()
{
    unload();
}

bool glShaderProgram::unload() 
{
    if (_handle != GLUtil::k_invalidObjectID)
    {
        {
            LockGuard<SharedMutex> w_lock(g_deletionSetLock);
            g_deletionSet.insert(_handle);
        }

        if (GL_API::GetStateTracker()._activeShaderPipelineHandle == _handle)
        {
            if (GL_API::GetStateTracker().setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED)
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        glDeleteProgramPipelines(1, &_handle);
        _handle = GLUtil::k_invalidObjectID;
    }

    for ( glShaderEntry& shader : _shaderStage )
    {
        shader._shader->deregisterParent( this );
    }

    _shaderStage.clear();

     return ShaderProgram::unload();
}

void glShaderProgram::processValidation()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    if (!_validationQueued)
    {
        return;
    }

    _validationQueued = false;

    UseProgramStageMask stageMask = UseProgramStageMask::GL_NONE_BIT;
    for ( glShaderEntry& shader : _shaderStage)
    {
        if (!shader._shader->valid())
        {
            continue;
        }

        shader._shader->onParentValidation();
        stageMask |= shader._shader->stageMask();
    }

    if constexpr(Config::ENABLE_GPU_VALIDATION)
    {
        g_sValidationQueue.enqueue({ resourceName(), _handle, stageMask});
    }
}

ShaderResult glShaderProgram::validatePreBind(const bool rebind)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
    ShaderResult ret = ShaderProgram::validatePreBind(rebind);
    if ( ret == ShaderResult::OK)
    {
        if (!_stagesBound && rebind)
        {
            assert(getState() == ResourceState::RES_LOADED);
            if (_handle == GLUtil::k_invalidObjectID) {
                glCreateProgramPipelines(1, &_handle);
                if constexpr(Config::ENABLE_GPU_VALIDATION) {
                    glObjectLabel(GL_PROGRAM_PIPELINE, _handle, -1, resourceName().c_str());
                }
                // We can reuse previous handles
                LockGuard<SharedMutex> w_lock(g_deletionSetLock);
                g_deletionSet.erase(_handle);
            }

            if (rebind) {
                assert(_handle != GLUtil::k_invalidObjectID);

                for ( glShaderEntry& shader : _shaderStage)
                {
                    ret = shader._shader->uploadToGPU(_handle);
                    if (ret != ShaderResult::OK) {
                        _stagesBound = true;
                        break;
                    }

                    // If a shader exists for said stage, attach it
                    glUseProgramStages(_handle, shader._shader->stageMask(), shader._shader->handle());
                }

                if (ret == ShaderResult::OK)
                {
                    _validationQueued = true;
                    _stagesBound = true;
                }
            }
        }
    }
    return ret;
}

bool glShaderProgram::loadInternal(hashMap<U64, PerFileShaderData>& fileData, const bool overwrite ) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    if (ShaderProgram::loadInternal(fileData, overwrite))
    {
        _stagesBound = false;

        for (auto& [fileHash, loadDataPerFile] : fileData)
        {
            assert(!loadDataPerFile._modules.empty());

            bool found = false;
            U32 targetGeneration = 0u;
            for ( glShaderEntry& stage : _shaderStage )
            {
                if ( stage._fileHash == _ID ( loadDataPerFile._programName.c_str()) )
                {
                    targetGeneration = overwrite ? stage._generation + 1u : stage._generation;
                    stage = glShader::LoadShader( _context, this, loadDataPerFile._programName, targetGeneration, loadDataPerFile._loadData );
                    found = true;
                    break;
                }
            }
            if (!found )
            {
                _shaderStage.push_back( glShader::LoadShader(_context, this, loadDataPerFile._programName, targetGeneration, loadDataPerFile._loadData) );
            }
        }

        return !_shaderStage.empty();
    }

    return false;
}

/// Bind this shader program
ShaderResult glShaderProgram::bind()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // If the shader isn't ready or failed to link, stop here
    const ShaderResult ret = validatePreBind(true);
    if (ret != ShaderResult::OK)
    {
        return ret;
    }

    // Set this program as the currently active one
    if (GL_API::GetStateTracker().setActiveShaderPipeline(_handle) == GLStateTracker::BindResult::JUST_BOUND)
    {
        // All of this needs to be run on an actual bind operation. If we are already bound, we assume we did all this
        processValidation();
    }

    return ShaderResult::OK;
}

void glShaderProgram::uploadPushConstants(const PushConstantsStruct& pushConstants)
{
    if (pushConstants._set)
    {
        for ( glShaderEntry& shader : _shaderStage)
        {
            if (!shader._shader->valid())
            {
                continue;
            }

            shader._shader->uploadPushConstants(pushConstants);
        }
    }
}
};
