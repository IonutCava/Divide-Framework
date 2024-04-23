

#include "Headers/glShaderProgram.h"
#include "Headers/glShader.h"

#include "Core/Headers/PlatformContext.h"
#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

namespace
{
    constexpr size_t g_validationBufferMaxSize = 64 * 1024;
    NO_DESTROY moodycamel::BlockingConcurrentQueue<ValidationEntry> g_sValidationQueue;

    SharedMutex      g_deletionSetLock;
    NO_DESTROY eastl::set<gl46core::GLuint> g_deletionSet;
}

void glShaderProgram::Idle( [[maybe_unused]] PlatformContext& platformContext )
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    // One validation per Idle call
    ProcessValidationQueue();
}

void glShaderProgram::ProcessValidationQueue()
{
    NO_DESTROY thread_local ValidationEntry s_validationOutputCache;

    if (!g_sValidationQueue.try_dequeue(s_validationOutputCache))
    {
        return;
    }
    {
        SharedLock<SharedMutex> w_lock(g_deletionSetLock);
        if (g_deletionSet.find(s_validationOutputCache._handle) != std::cend(g_deletionSet))
        {
            return;
        }
    }

    assert(s_validationOutputCache._handle != GL_NULL_HANDLE);
    
    gl46core::glValidateProgramPipeline(s_validationOutputCache._handle);

    gl46core::GLint status = 1;
    if (s_validationOutputCache._stageMask != gl46core::UseProgramStageMask::GL_COMPUTE_SHADER_BIT)
    {
        gl46core::glGetProgramPipelineiv(s_validationOutputCache._handle, gl46core::GL_VALIDATE_STATUS, &status);
    }

    // we print errors in debug and in release, but everything else only in debug
    // the validation log is only retrieved if we request it. (i.e. in release,
    // if the shader is validated, it isn't retrieved)
    if (status == 0)
    {
        // Query the size of the log
        gl46core::GLint length = 0;
        gl46core::glGetProgramPipelineiv(s_validationOutputCache._handle, gl46core::GL_INFO_LOG_LENGTH, &length);
        // If we actually have something in the validation log
        if (length > 1)
        {
            string validationBuffer;
            validationBuffer.resize(length);
            gl46core::glGetProgramPipelineInfoLog(s_validationOutputCache._handle, length, nullptr, &validationBuffer[0]);

            // To avoid overflowing the output buffers (both CEGUI and Console), limit the maximum output size
            if (validationBuffer.size() > g_validationBufferMaxSize)
            {
                // On some systems, the program's disassembly is printed, and that can get quite large
                validationBuffer.resize(std::strlen(LOCALE_STR("GLSL_LINK_PROGRAM_LOG")) + g_validationBufferMaxSize);
                // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                validationBuffer.append(" ... ");
            }
            // Return the final message, whatever it may contain
            Console::errorfn(LOCALE_STR("GLSL_VALIDATING_PROGRAM"), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), validationBuffer.c_str());
        }
        else
        {
            Console::errorfn(LOCALE_STR("GLSL_VALIDATING_PROGRAM"), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ Couldn't retrieve info log! ]");
        }
    }
    else
    {
        Console::d_printfn(LOCALE_STR("GLSL_VALIDATING_PROGRAM"), s_validationOutputCache._handle, s_validationOutputCache._name.c_str(), "[ OK! ]");
    }
}

glShaderProgram::glShaderProgram(GFXDevice& context,
                                 const size_t descriptorHash,
                                 const std::string_view name,
                                 std::string_view assetName,
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
    if (_glHandle != GL_NULL_HANDLE)
    {
        {
            LockGuard<SharedMutex> w_lock(g_deletionSetLock);
            g_deletionSet.insert(_glHandle);
        }

        if (GL_API::GetStateTracker()._activeShaderPipelineHandle == _glHandle)
        {
            if (GL_API::GetStateTracker().setActiveShaderPipeline(0u) == GLStateTracker::BindResult::FAILED)
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        gl46core::glDeleteProgramPipelines(1, &_glHandle);
        _glHandle = GL_NULL_HANDLE;
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

    gl46core::UseProgramStageMask stageMask = gl46core::UseProgramStageMask::GL_NONE_BIT;
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
        g_sValidationQueue.enqueue({ resourceName(), _glHandle, stageMask});
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
            if (_glHandle == GL_NULL_HANDLE)
            {
                gl46core::glCreateProgramPipelines(1, &_glHandle);
                if constexpr(Config::ENABLE_GPU_VALIDATION)
                {
                    gl46core::glObjectLabel( gl46core::GL_PROGRAM_PIPELINE,
                                             _glHandle,
                                             -1,
                                             resourceName().c_str() );
                }
                // We can reuse previous handles
                LockGuard<SharedMutex> w_lock(g_deletionSetLock);
                g_deletionSet.erase(_glHandle);
            }

            if (rebind)
            {
                assert(_glHandle != GL_NULL_HANDLE);

                for ( glShaderEntry& shader : _shaderStage)
                {
                    ret = shader._shader->uploadToGPU();
                    if (ret != ShaderResult::OK) {
                        _stagesBound = true;
                        break;
                    }

                    // If a shader exists for said stage, attach it
                    gl46core::glUseProgramStages(_glHandle, shader._shader->stageMask(), shader._shader->handle());
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

bool glShaderProgram::loadInternal(hashMap<U64, PerFileShaderData>& fileData, const bool overwrite )
{
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
                    stage = glShader::LoadShader( _context, this, loadDataPerFile._programName.c_str(), targetGeneration, loadDataPerFile._loadData );
                    found = true;
                    break;
                }
            }
            if (!found )
            {
                _shaderStage.push_back( glShader::LoadShader(_context, this, loadDataPerFile._programName.c_str(), targetGeneration, loadDataPerFile._loadData) );
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
    if (GL_API::GetStateTracker().setActiveShaderPipeline(_glHandle) == GLStateTracker::BindResult::JUST_BOUND)
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
